// localsend-gui: applicazione grafica nativa Haiku per LocalSend.
// Combina sender, receiver e discovery in un'unica finestra.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <Application.h>
#include <Alert.h>
#include <Bitmap.h>
#include <Box.h>
#include <Button.h>
#include <Entry.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Clipboard.h>
#include <File.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <Resources.h>
#include <Roster.h>
#include <ControlLook.h>
#include <FilePanel.h>
#include <Font.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <LocaleRoster.h>
#include <ListView.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <Notification.h>
#include <PopUpMenu.h>
#include <Path.h>
#include <MessageRunner.h>
#include <Region.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <Shape.h>
#include <StatusBar.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <OS.h>

#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>

#include <qrencode.h>

#include "app/DeskbarItem.h"
#include "replicant/DesktopReplicant.h"
#include "net/MulticastAnnouncer.h"
#include "net/TlsContext.h"
#include "net/haiku/SocketHttpClient.h"
#include "net/haiku/SocketHttpServer.h"
#include "protocol/Constants.h"
#include "protocol/Fingerprint.h"
#include "protocol/Models.h"
#include "client/FileSource.h"
#include "client/UploadSession.h"

// Contesto del Locale Kit: raggruppa le stringhe di questo file nel catalogo.
// Deve precedere il primo B_TRANSLATE. La lingua segue le preferenze di
// sistema (Preferenze > Locale); niente selettore in-app.
#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LocalSend"
#include "server/FileSink.h"
#include "server/ReceiveSession.h"

using namespace LocalSend;

// Codici messaggio.
enum {
	kMsgSendFile		= 'SEND',
	kMsgFileSelected	= 'FSEL',
	kMsgIncoming		= 'INCM',
	kMsgFileReceived	= 'FRCV',
	kMsgDeviceFound		= 'DSCV',
	kMsgSendDone		= 'SDNE',
	kMsgProgress		= 'PROG',
	kMsgDeviceInvoked	= 'DINV',
	kMsgDropOnDevice	= 'DROP',	// lista -> window: file lasciati su una riga
	kMsgShowSettings	= 'SETT',
	kMsgSettingsSave	= 'SSAV',
	kMsgBrowseDir		= 'BRWD',
	kMsgDirSelected		= 'DSEL',
	kMsgShowHistory		= 'HIST',
	kMsgClearHistory	= 'HCLR',
	kMsgSendText		= 'STXT',
	kMsgTextReady		= 'TXRD',
	kMsgTextReceived	= 'TXRC',
	kMsgToggleFavorite	= 'TFAV',
	// Contatti prioritari: cerchia ristretta che puo' vedere la bacheca
	// (lista dedicata, separata dai preferiti). Toggle dal menu contestuale
	// della lista dispositivi; gestione dalla finestra dedicata.
	kMsgTogglePriority	= 'TPRI',
	kMsgManagePriority	= 'MPRI',	// apri la finestra di gestione
	kMsgPriorityRemove	= 'PRMV',	// finestra -> window: rimuovi un contatto
	kMsgPriorityChanged	= 'PCHG',	// window -> finestra: lista aggiornata
	kMsgPendingFiles	= 'PEND',
	kMsgShareLink		= 'SLNK',
	kMsgStopShare		= 'SSHR',
	kMsgAbout			= 'ABOU',
	kMsgToggleDeskbar	= 'TDBR',
	// Inviato dal replicant Deskbar (voce di menu destro "Quit"): chiede
	// l'uscita vera, bypassando l'hide-on-close.
	kMsgQuitFromTray	= 'QFTR',
	// Tick periodico del BMessageRunner: rimuove i peer non sentiti
	// da troppo tempo (TTL "last-seen").
	kMsgPruneDevices	= 'PRUN',
	// Sonda periodica dei peer noti via HTTP GET /info: li tiene vivi anche
	// quando il multicast in ingresso non arriva (reti che non lo inoltrano).
	kMsgProbePeers		= 'PROB',
	// Pulsante Refresh nella UI: forza un annuncio extra cosi' i peer
	// rispondono in unicast e la lista si rinfresca subito.
	kMsgRefreshDevices	= 'RFSH',
	// Inviato da MainWindow a LocalSendApp per chiedere all'announcer
	// di sparare un burst (l'announcer e' di proprieta' dell'app).
	kMsgTriggerBurst	= 'BURS',
	// Pulsanti del dialog "Testo ricevuto".
	kMsgTxRxCopy		= 'TXCP',
	kMsgTxRxOpen		= 'TXOP',
	kMsgTxRxSave		= 'TXSV',
	kMsgTxRxSaveTo		= 'TXST',

	// Fase 1 "cerchia": invio in sequenza a tutti i preferiti online.
	// kMsgSendToCircle avvia il flusso (pulsante/file panel), kMsgCircleDone
	// arriva dal thread di invio con l'esito aggregato (ok/total).
	kMsgSendToCircle	= 'SCRC',
	kMsgCircleDone		= 'CDNE',

	// Fase 0 "bacheca": canale app <-> replicant e propagazione dei
	// cambiamenti. I replicant (Deskbar/desktop) si iscrivono con BSUB
	// passando il proprio BMessenger e ricevono BUPD a ogni cambiamento
	// di bacheca o peer; BDRP e' il drop di file sul replicant (refs +
	// bool "circle"), BOPN chiede di aprire la GUI/bacheca.
	kMsgReplicantSubscribe	= 'BSUB',	// replicant -> app
	kMsgReplicantUnsubscribe = 'BUNS',	// replicant -> app
	kMsgBoardUpdate			= 'BUPD',	// app -> replicant iscritti
	kMsgReplicantDrop		= 'BDRP',	// replicant -> app
	kMsgOpenBoard			= 'BOPN',	// replicant -> app
	kMsgBoardChanged		= 'BRDC',	// window -> app (rev/active/count)
	kMsgPeerStats			= 'PSTA',	// window -> app (peer online)
	kMsgIdentityChanged		= 'IDCH',	// window -> app (alias cambiato)
	kMsgPeerHeard			= 'PHRD',	// app -> window (peer sentito dal faro)
	kMsgAddToBoard			= 'BADD',	// app -> window (refs da pubblicare)

	// Fase 3 "bacheca remota": lato osservatore. Quando un peer annuncia un
	// boardRev diverso dall'ultimo noto la window fa il fetch della lista
	// (prepare-download, thread separato) e aggiorna la finestra Bacheca.
	kMsgShowBoard			= 'SBRD',	// apri/attiva la finestra Bacheca
	kMsgPeerBoardChanged	= 'PBCH',	// announcer -> window (rev cambiata)
	kMsgRemoteBoardFetched	= 'RBFE',	// thread fetch -> window (lista o errore)
	kMsgBoardDownload		= 'BDLD',	// BoardWindow/auto -> window (scarica file)
	kMsgBoardDownloadDone	= 'BDLE',	// thread download -> window (esito)
	kMsgBoardDataChanged	= 'BDCH',	// window -> BoardWindow (snapshot)
	kMsgBoardPickFiles		= 'BPIK',	// BoardWindow -> window (file panel)
	kMsgBoardDownloadAll	= 'BDLA',	// interno a BoardWindow
	kMsgBoardOpenLocal		= 'BOPL',	// interno a BoardWindow (apri file)
	kMsgBoardRemove			= 'BRMV',	// BoardWindow -> window (togli file)
	kMsgBoardDownloadOpen	= 'BDLO'	// interno a BoardWindow (scarica+apri)
};


// --- Impostazioni persistenti ----------------------------------------------

// Chi puo' vedere e scaricare dalla bacheca (porta 53318).
enum BoardVisibility {
	kBoardEveryone = 0,		// chiunque sulla LAN (comportamento storico)
	kBoardFavorites = 1,	// solo i preferiti (la cerchia)
	kBoardPriority = 2		// solo i contatti prioritari (lista dedicata)
};

struct AppSettings {
	std::string alias = "Haiku Box";
	std::string destDir = "./ricevuti";
	int port = 53317;
	std::string pin;
	bool quickSave = false;
	bool https = true;
	// True quando l'utente ha installato il replicant nella Deskbar.
	// Persistito perche' la Deskbar non sempre rigenera lo shelf al boot:
	// se il flag e' true e il replicant manca all'avvio dell'app, lo
	// re-installiamo noi.
	bool deskbarItem = false;
	// Se true, i trasferimenti dai fingerprint nei preferiti non aprono
	// il dialog di conferma: passano al ricevente immediatamente e
	// arriva solo la notifica di sistema. Default true per continuita'
	// col comportamento storico (era hard-coded).
	bool autoAcceptFavorites = true;
	// Se true, i file nuovi in bacheca sui preferiti vengono scaricati
	// da soli (con registro dei gia' scaricati). Default off: opt-in.
	bool autoDownloadBoard = false;
	// Chi puo' vedere/scaricare dalla bacheca (porta 53318): tutti, i
	// preferiti, o i soli contatti prioritari. Default: tutti (storico).
	// Il filtro guarda il fingerprint in query o l'IP di un peer noto
	// online; un estraneo riceve 403.
	int boardVisibility = kBoardEveryone;

	// Ritorna true se il file esisteva: il chiamante distingue il primo
	// avvio (nessun settings) per generare un alias casuale una volta sola.
	bool Load(const std::string& path)
	{
		FILE* f = fopen(path.c_str(), "r");
		if (!f)
			return false;
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			std::string l(line);
			size_t eq = l.find('=');
			if (eq == std::string::npos)
				continue;
			std::string key = l.substr(0, eq);
			std::string val = l.substr(eq + 1);
			while (!val.empty()
				&& (val.back() == '\n' || val.back() == '\r'))
				val.pop_back();
			if (key == "alias") alias = val;
			else if (key == "destDir") destDir = val;
			else if (key == "port") port = atoi(val.c_str());
			else if (key == "pin") pin = val;
			else if (key == "quickSave") quickSave = (val == "1");
			else if (key == "https") https = (val == "1");
			else if (key == "deskbarItem") deskbarItem = (val == "1");
			else if (key == "autoAcceptFavorites")
				autoAcceptFavorites = (val == "1");
			else if (key == "autoDownloadBoard")
				autoDownloadBoard = (val == "1");
			else if (key == "boardVisibility")
				boardVisibility = atoi(val.c_str());
			else if (key == "boardFavoritesOnly") {
				// Migrazione dalla vecchia chiave booleana.
				if (val == "1" && boardVisibility == kBoardEveryone)
					boardVisibility = kBoardFavorites;
			}
			// "language": chiave storica ignorata (ora la lingua segue
			// le preferenze di sistema tramite il Locale Kit).
		}
		fclose(f);
		return true;
	}

	void Save(const std::string& path) const
	{
		FILE* f = fopen(path.c_str(), "w");
		if (!f)
			return;
		fprintf(f, "alias=%s\n", alias.c_str());
		fprintf(f, "destDir=%s\n", destDir.c_str());
		fprintf(f, "port=%d\n", port);
		fprintf(f, "pin=%s\n", pin.c_str());
		fprintf(f, "quickSave=%d\n", quickSave ? 1 : 0);
		fprintf(f, "https=%d\n", https ? 1 : 0);
		fprintf(f, "deskbarItem=%d\n", deskbarItem ? 1 : 0);
		fprintf(f, "autoAcceptFavorites=%d\n",
			autoAcceptFavorites ? 1 : 0);
		fprintf(f, "autoDownloadBoard=%d\n", autoDownloadBoard ? 1 : 0);
		fprintf(f, "boardVisibility=%d\n", boardVisibility);
		fclose(f);
	}
};

static const char* kSettingsFile = "./localsend_settings";
static const char* kHistoryFile = "./localsend_history";
static const int kMaxHistoryEntries = 30;


// Nome del dispositivo generato al primo avvio: un aggettivo + un nome che
// strizza l'occhio a Haiku/BeOS. E' l'alias che gli altri vedono in rete;
// l'utente puo' sempre cambiarlo dalle Impostazioni.
static std::string
GenerateRandomAlias()
{
	static const char* kAdjectives[] = {
		"Blue", "Yellow", "Swift", "Cosmic", "Retro", "Turbo", "Pixel",
		"Amber", "Silent", "Vivid", "Nimble", "Electric", "Mellow",
		"Snappy", "Groovy"
	};
	static const char* kNouns[] = {
		"Haiku", "BeBox", "Deskbar", "Tracker", "Replicant", "Leaf",
		"Pulse", "NetPositive", "Dano", "Zeta", "GeekPort", "BFS",
		"WebPositive", "StyledEdit", "Gobe", "Bootman", "Sen", "Koan"
	};
	const int nAdj = sizeof(kAdjectives) / sizeof(kAdjectives[0]);
	const int nNoun = sizeof(kNouns) / sizeof(kNouns[0]);

	// Seed da system_time (microsecondi dall'avvio): niente rand() globale
	// da riseminare, basta e avanza per la scelta di due indici.
	bigtime_t t = system_time();
	unsigned r = (unsigned)(t ^ (t >> 21) ^ (t << 7));
	std::string alias = kAdjectives[r % nAdj];
	alias += " ";
	alias += kNouns[(r / nAdj) % nNoun];
	return alias;
}


// --- Cronologia trasferimenti ----------------------------------------------

struct HistoryEntry {
	bool sent;            // true = inviato, false = ricevuto
	std::string fileName;
	std::string peer;     // alias del dispositivo
	std::string date;     // YYYY-MM-DD HH:MM
	long long size;
};

struct TransferHistory {
	std::vector<HistoryEntry> entries;

	void Add(bool sent, const std::string& fileName,
		const std::string& peer, long long size)
	{
		HistoryEntry e;
		e.sent = sent;
		e.fileName = fileName;
		e.peer = peer;
		e.size = size;
		// Data/ora corrente.
		time_t now = time(nullptr);
		struct tm* t = localtime(&now);
		char buf[64];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
			t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
			t->tm_hour, t->tm_min);
		e.date = buf;
		entries.push_back(e);
		if ((int)entries.size() > kMaxHistoryEntries)
			entries.erase(entries.begin());
		Save();
	}

	void Clear()
	{
		entries.clear();
		remove(kHistoryFile);
	}

	void Load()
	{
		FILE* f = fopen(kHistoryFile, "r");
		if (!f)
			return;
		char line[1024];
		while (fgets(line, sizeof(line), f)) {
			std::string l(line);
			while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
				l.pop_back();
			// Formato: S|R<TAB>date<TAB>peer<TAB>size<TAB>fileName
			HistoryEntry e;
			size_t p1 = l.find('\t');
			if (p1 == std::string::npos) continue;
			e.sent = (l[0] == 'S');
			size_t p2 = l.find('\t', p1 + 1);
			if (p2 == std::string::npos) continue;
			e.date = l.substr(p1 + 1, p2 - p1 - 1);
			size_t p3 = l.find('\t', p2 + 1);
			if (p3 == std::string::npos) continue;
			e.peer = l.substr(p2 + 1, p3 - p2 - 1);
			size_t p4 = l.find('\t', p3 + 1);
			if (p4 == std::string::npos) continue;
			e.size = atoll(l.c_str() + p3 + 1);
			e.fileName = l.substr(p4 + 1);
			entries.push_back(e);
		}
		fclose(f);
	}

	void Save() const
	{
		FILE* f = fopen(kHistoryFile, "w");
		if (!f)
			return;
		for (const auto& e : entries) {
			fprintf(f, "%c\t%s\t%s\t%lld\t%s\n",
				e.sent ? 'S' : 'R', e.date.c_str(),
				e.peer.c_str(), e.size, e.fileName.c_str());
		}
		fclose(f);
	}
};


// --- Preferiti (dispositivi salvati) ----------------------------------------

static const char* kFavoritesFile = "./localsend_favorites";

struct Favorites {
	// Fingerprint dei dispositivi preferiti.
	std::vector<std::string> fingerprints;

	bool Contains(const std::string& fp) const
	{
		for (const auto& f : fingerprints) {
			if (f == fp)
				return true;
		}
		return false;
	}

	void Add(const std::string& fp)
	{
		if (!Contains(fp)) {
			fingerprints.push_back(fp);
			Save();
		}
	}

	void Remove(const std::string& fp)
	{
		for (auto it = fingerprints.begin();
			it != fingerprints.end(); ++it) {
			if (*it == fp) {
				fingerprints.erase(it);
				Save();
				return;
			}
		}
	}

	void Load()
	{
		FILE* f = fopen(kFavoritesFile, "r");
		if (!f)
			return;
		char line[256];
		while (fgets(line, sizeof(line), f)) {
			std::string l(line);
			while (!l.empty()
				&& (l.back() == '\n' || l.back() == '\r'))
				l.pop_back();
			if (!l.empty())
				fingerprints.push_back(l);
		}
		fclose(f);
	}

	void Save() const
	{
		FILE* f = fopen(kFavoritesFile, "w");
		if (!f)
			return;
		for (const auto& fp : fingerprints)
			fprintf(f, "%s\n", fp.c_str());
		fclose(f);
	}
};


// --- Contatti prioritari -----------------------------------------------------

static const char* kPriorityFile = "./localsend_priority";

// Cerchia ristretta e "profilata" che puo' vedere la bacheca quando la
// visibilita' e' impostata su "solo contatti prioritari". Lista dedicata,
// indipendente dai preferiti: qui l'identita' e' il fingerprint TLS (stabile
// e crittografico), l'alias e' il nome con cui il contatto e' stato aggiunto.
struct PriorityContact {
	std::string fingerprint;
	std::string alias;
};

struct PriorityContacts {
	std::vector<PriorityContact> contacts;

	bool Contains(const std::string& fp) const
	{
		for (const auto& c : contacts) {
			if (c.fingerprint == fp)
				return true;
		}
		return false;
	}

	// Aggiunge (o aggiorna l'alias di) un contatto identificato dal
	// fingerprint. L'alias e' il "profilo" leggibile mostrato all'utente.
	void Add(const std::string& fp, const std::string& alias)
	{
		if (fp.empty())
			return;
		for (auto& c : contacts) {
			if (c.fingerprint == fp) {
				if (!alias.empty() && c.alias != alias) {
					c.alias = alias;
					Save();
				}
				return;
			}
		}
		contacts.push_back({fp, alias});
		Save();
	}

	void Remove(const std::string& fp)
	{
		for (auto it = contacts.begin(); it != contacts.end(); ++it) {
			if (it->fingerprint == fp) {
				contacts.erase(it);
				Save();
				return;
			}
		}
	}

	void Load()
	{
		FILE* f = fopen(kPriorityFile, "r");
		if (!f)
			return;
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			std::string l(line);
			while (!l.empty()
				&& (l.back() == '\n' || l.back() == '\r'))
				l.pop_back();
			if (l.empty())
				continue;
			// Formato: fingerprint<TAB>alias. Alias opzionale.
			size_t tab = l.find('\t');
			PriorityContact c;
			if (tab == std::string::npos) {
				c.fingerprint = l;
			} else {
				c.fingerprint = l.substr(0, tab);
				c.alias = l.substr(tab + 1);
			}
			if (!c.fingerprint.empty())
				contacts.push_back(c);
		}
		fclose(f);
	}

	void Save() const
	{
		FILE* f = fopen(kPriorityFile, "w");
		if (!f)
			return;
		for (const auto& c : contacts)
			fprintf(f, "%s\t%s\n", c.fingerprint.c_str(), c.alias.c_str());
		fclose(f);
	}
};


// --- Registro bacheca: file gia' scaricati -----------------------------------

static const char* kBoardSeenFile = "./localsend_board_seen";

// Ricorda quali file di bacheca remota sono gia' stati scaricati
// (auto-download): il boardRev del peer cresce a ogni aggiunta e la lista
// e' cumulativa, senza registro riscaricheremmo tutto a ogni bump.
struct BoardSeen {
	std::set<std::string> keys;

	bool Contains(const std::string& k) const { return keys.count(k) > 0; }

	void Add(const std::string& k)
	{
		if (keys.insert(k).second)
			Save();
	}

	void Load()
	{
		FILE* f = fopen(kBoardSeenFile, "r");
		if (!f)
			return;
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			std::string l(line);
			while (!l.empty()
				&& (l.back() == '\n' || l.back() == '\r'))
				l.pop_back();
			if (!l.empty())
				keys.insert(l);
		}
		fclose(f);
	}

	void Save() const
	{
		FILE* f = fopen(kBoardSeenFile, "w");
		if (!f)
			return;
		for (const auto& k : keys)
			fprintf(f, "%s\n", k.c_str());
		fclose(f);
	}
};

// Chiave del registro: identifica un file di bacheca di un peer in modo
// stabile tra un fetch e l'altro (id + nome + dimensione).
static std::string
BoardSeenKey(const std::string& fingerprint, const std::string& fileId,
	const std::string& fileName, long long size)
{
	return fingerprint + "\t" + fileId + "|" + fileName + "|"
		+ std::to_string(size);
}


// --- Bacheca remota (lato osservatore) ---------------------------------------

struct RemoteBoardFile {
	std::string id;       // fileId del peer ("file-N")
	std::string fileName;
	std::string fileType; // MIME: icona in BoardWindow e drag negoziato
	long long size = 0;
};

// Snapshot della bacheca di un peer, aggiornato al cambio di boardRev.
// Toccato SOLO dal thread della MainWindow: i thread di fetch consegnano
// i dati via kMsgRemoteBoardFetched.
struct RemoteBoard {
	std::string alias;
	std::string host;
	std::string fingerprint;
	int rev = 0;
	std::vector<RemoteBoardFile> files;
};


// --- Dispositivo scoperto in LAN -------------------------------------------

struct DiscoveredDevice {
	std::string alias;
	std::string host;
	int port;
	std::string deviceType;
	// Piattaforma/OS del peer ("Windows", "Linux", "macOS", "iPhone",
	// "Haiku", ...): campo "deviceModel" del protocollo. Vuoto se assente.
	std::string deviceModel;
	std::string fingerprint;
	// Estensione Haiku: revisione della bacheca del peer (0 = nessuna).
	// Aggiornata a ogni annuncio; la Fase 3 la confronta con l'ultima
	// nota per capire quando rifare il fetch della lista.
	int boardRev = 0;
	// True se il peer gira la nostra app Haiku (badge a foglia in lista).
	bool isHaiku = false;
	// Ultima volta che ne abbiamo sentito parlare (annuncio o reply).
	// Aggiornato a ogni pacchetto multicast/unicast valido del peer; il
	// pruning periodico (kMsgPruneDevices) rimuove chi non si fa sentire
	// da kDeviceTimeoutSeconds.
	time_t lastSeen = 0;
};

// TTL: un peer non sentito da N secondi e' considerato offline.
// Tre tick di annuncio (3*5s) + un piccolo cuscinetto coprono jitter / pacchetti
// persi senza far sparire un peer ancora vivo.
static const int kDeviceTimeoutSeconds = 20;
// Ogni quanto sondare via HTTP i peer noti (< TTL, cosi' un peer vivo viene
// rinfrescato piu' volte prima di scadere).
static const int kPeerProbeIntervalSeconds = 7;
// Anti-flood del prepare-upload: al massimo N richieste per finestra di
// M secondi, oltre le quali si risponde 429 Too Many Requests. Soglia larga:
// un uso legittimo non arriva mai a tante prepare-upload cosi' ravvicinate.
static const int kPrepareRateWindowSeconds = 10;
static const int kPrepareRateMax = 15;
// Cadenza del pruning periodico.
static const int kDevicePruneIntervalSeconds = 5;


// --- Richiesta in arrivo (dal server thread alla GUI) ----------------------

struct IncomingRequest {
	IncomingPrepareUpload data;
	bool accepted = false;
	bool answered = false;
	std::mutex mtx;
	std::condition_variable cv;
};


// --- DeviceListItem: elemento personalizzato per la lista dispositivi ------

class DeviceListItem : public BListItem {
public:
	DeviceListItem(const char* name, const char* type, const char* model,
		const char* ip, const char* fingerprint, bool favorite = false,
		bool priority = false, bool haiku = false)
		:
		BListItem(),
		fName(name),
		fType(type),
		fModel(model != NULL ? model : ""),
		fIp(ip),
		fFingerprintId(fingerprint != NULL ? fingerprint : ""),
		fFavorite(favorite),
		fPriority(priority),
		fHaiku(haiku)
	{
	}

	void SetFavorite(bool fav) { fFavorite = fav; }
	void SetPriority(bool p) { fPriority = p; }
	const BString& FingerprintId() const { return fFingerprintId; }
	bool IsFavorite() const { return fFavorite; }
	bool IsPriority() const { return fPriority; }
	// La riga mostra le azioni inline quando e' sotto il mouse (impostato
	// dalla lista) o selezionata.
	void SetShowActions(bool s) { fShowActions = s; }

	// Rettangoli dei tre "pulsanti" inline (File… / Testo… / ★) dentro una
	// riga, da destra: [File…] [Testo…] [★]. Condivisi tra disegno e
	// hit-testing del click, cosi' restano allineati.
	static void ActionRects(BRect frame, BRect& fileR, BRect& textR,
		BRect& starR)
	{
		const float pad = 8, gap = 6, h = 22, starW = 24, btnW = 54;
		float top = frame.top + (frame.Height() - h) / 2;
		float bot = top + h;
		float x = frame.right - pad;
		starR.Set(x - starW, top, x, bot); x -= starW + gap;
		textR.Set(x - btnW, top, x, bot); x -= btnW + gap;
		fileR.Set(x - btnW, top, x, bot);
	}

	virtual void DrawItem(BView* owner, BRect frame, bool /*complete*/)
	{
		rgb_color bgColor;
		if (IsSelected())
			bgColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
		else
			bgColor = ui_color(B_LIST_BACKGROUND_COLOR);

		owner->SetHighColor(bgColor);
		owner->FillRect(frame);

		// Icona del tipo di dispositivo.
		float iconSize = frame.Height() - 8;
		BRect iconRect(frame.left + 8, frame.top + 4,
			frame.left + 8 + iconSize, frame.bottom - 4);

		// Disegna un cerchio colorato come icona.
		rgb_color iconColor;
		if (fType == "mobile")
			iconColor = (rgb_color){80, 160, 240, 255};
		else if (fType == "desktop")
			iconColor = (rgb_color){100, 200, 120, 255};
		else
			iconColor = (rgb_color){180, 180, 180, 255};

		owner->SetHighColor(iconColor);
		float cx = iconRect.left + iconSize / 2;
		float cy = iconRect.top + iconSize / 2;
		float r = iconSize / 2 - 1;
		owner->FillEllipse(BPoint(cx, cy), r, r);

		// Lettera iniziale nel cerchio.
		owner->SetHighColor(255, 255, 255);
		BFont smallFont(be_bold_font);
		smallFont.SetSize(iconSize * 0.5);
		owner->SetFont(&smallFont);
		char initial[2] = {fName.String()[0], 0};
		float charWidth = owner->StringWidth(initial);
		font_height fh;
		smallFont.GetHeight(&fh);
		owner->DrawString(initial,
			BPoint(cx - charWidth / 2,
				cy + (fh.ascent - fh.descent) / 2));

		// Nome del dispositivo.
		float textLeft = iconRect.right + 10;

		if (IsSelected())
			owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
		else
			owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));

		BFont nameFont(be_bold_font);
		nameFont.SetSize(be_plain_font->Size() + 1);
		owner->SetFont(&nameFont);
		nameFont.GetHeight(&fh);
		owner->DrawString(fName.String(),
			BPoint(textLeft, frame.top + 4 + fh.ascent));

		// Tipo e IP sotto il nome.
		BFont detailFont(be_plain_font);
		detailFont.SetSize(be_plain_font->Size() - 1);
		owner->SetFont(&detailFont);

		rgb_color detailColor;
		if (IsSelected())
			detailColor = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
		else
			detailColor = tint_color(ui_color(B_LIST_ITEM_TEXT_COLOR), 0.7);
		owner->SetHighColor(detailColor);

		// Riga di dettaglio: OS/piattaforma (se noto) · tipo · IP.
		BString detail;
		if (fModel.Length() > 0)
			detail << fModel << " \xc2\xb7 " << fType << " \xc2\xb7 " << fIp;
		else
			detail << fType << " \xc2\xb7 " << fIp;
		detailFont.GetHeight(&fh);
		owner->DrawString(detail.String(),
			BPoint(textLeft, frame.bottom - 4 - fh.descent));

		// Riga selezionata o sotto il mouse: le azioni (File… / Testo… / ★)
		// vivono sulla riga stessa. Altrimenti mostra i marcatori passivi.
		if (fShowActions || IsSelected()) {
			_DrawActions(owner, frame);
		} else {
			// Stella preferito.
			if (fFavorite) {
				owner->SetHighColor((rgb_color){240, 200, 40, 255});
				BFont starFont(be_bold_font);
				starFont.SetSize(14);
				owner->SetFont(&starFont);
				owner->DrawString("\xe2\x98\x85",
					BPoint(frame.right - 20, frame.top + 18));
			}

			// Pallino "contatto prioritario" (viola), a sinistra della
			// stella cosi' i due marcatori non si sovrappongono.
			if (fPriority) {
				owner->SetHighColor((rgb_color){150, 90, 220, 255});
				owner->FillEllipse(BRect(frame.right - 36, frame.top + 8,
					frame.right - 28, frame.top + 16));
			}

			// Badge a foglia se il peer gira la nostra app Haiku.
			if (fHaiku) {
				owner->PushState();
				_DrawLeaf(owner, BRect(frame.right - 21,
					frame.bottom - 20, frame.right - 9,
					frame.bottom - 6));
				owner->PopState();
			}
		}
	}

	virtual void Update(BView* owner, const BFont* /*font*/)
	{
		// Altezza fissa per ogni elemento.
		SetHeight(48);
		SetWidth(owner->Bounds().Width());
	}

	const BString& Name() const { return fName; }

private:
	BString fName;
	BString fType;
	BString fModel; // piattaforma/OS del peer (vuoto se sconosciuto)
	BString fIp;
	// Fingerprint del peer: chiave usata per ritrovare l'item da rimuovere
	// quando il pruning per TTL espelle un dispositivo offline.
	BString fFingerprintId;
	bool fFavorite;
	bool fPriority;
	bool fHaiku;
	bool fShowActions = false; // riga sotto il mouse (azioni inline visibili)

	// Disegna i pulsanti inline sulla riga: File… / Testo… / ★ (toggle
	// preferito). Sfondo chiaro cosi' restano leggibili anche sulla riga
	// selezionata (blu).
	void _DrawActions(BView* owner, BRect frame)
	{
		BRect fileR, textR, starR;
		ActionRects(frame, fileR, textR, starR);
		owner->PushState();
		_DrawButton(owner, fileR, B_TRANSLATE("File…"));
		_DrawButton(owner, textR, B_TRANSLATE("Text…"));

		// Stella toggle: gialla se preferito, grigia altrimenti.
		BFont sf(be_bold_font);
		sf.SetSize(16);
		owner->SetFont(&sf);
		owner->SetHighColor(fFavorite ? (rgb_color){240, 200, 40, 255}
			: (rgb_color){170, 170, 170, 255});
		const char* star = "\xe2\x98\x85";
		float sw = owner->StringWidth(star);
		font_height sfh;
		sf.GetHeight(&sfh);
		owner->DrawString(star, BPoint(starR.left + (starR.Width() - sw) / 2,
			starR.top + (starR.Height() + sfh.ascent - sfh.descent) / 2));
		owner->PopState();
	}

	static void _DrawButton(BView* owner, BRect r, const char* label)
	{
		owner->SetHighColor((rgb_color){255, 255, 255, 255});
		owner->FillRoundRect(r, 4, 4);
		owner->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		owner->StrokeRoundRect(r, 4, 4);
		owner->SetHighColor((rgb_color){50, 50, 50, 255});
		BFont f(be_plain_font);
		f.SetSize(be_plain_font->Size() - 1);
		owner->SetFont(&f);
		font_height fh;
		f.GetHeight(&fh);
		float lw = owner->StringWidth(label);
		owner->DrawString(label, BPoint(r.left + (r.Width() - lw) / 2,
			r.top + (r.Height() + fh.ascent - fh.descent) / 2));
	}

	// Disegna una piccola foglia (logo Haiku) dentro box: due curve di Bezier
	// che formano il profilo, riempimento verde Haiku, nervatura centrale.
	// Vettoriale: nitida a qualunque dimensione, nessuna dipendenza da font.
	static void _DrawLeaf(BView* owner, BRect box)
	{
		float w = box.Width();
		float h = box.Height();
		BPoint tip(box.right, box.top);      // punta in alto a destra
		BPoint base(box.left, box.bottom);   // base in basso a sinistra

		BShape leaf;
		leaf.MoveTo(tip);
		// Lato superiore: si gonfia verso l'alto-sinistra.
		BPoint up[3] = {
			BPoint(box.left + w * 0.15, box.top),
			BPoint(box.left, box.top + h * 0.35),
			base
		};
		leaf.BezierTo(up);
		// Lato inferiore: si gonfia verso il basso-destra, torna alla punta.
		BPoint down[3] = {
			BPoint(box.right - w * 0.15, box.bottom),
			BPoint(box.right, box.bottom - h * 0.35),
			tip
		};
		leaf.BezierTo(down);
		leaf.Close();

		owner->SetHighColor(106, 190, 69, 255); // verde foglia Haiku
		owner->FillShape(&leaf);

		// Nervatura centrale, dalla base alla punta.
		owner->SetHighColor(60, 130, 45, 255);
		owner->SetPenSize(1);
		owner->StrokeLine(base, tip);
	}
};


// Lista dei dispositivi scoperti: aggiunge il menu contestuale (tasto destro)
// per marcare un peer come preferito o come contatto prioritario. Le voci
// agiscono sulla selezione corrente, come i pulsanti gia' esistenti.
class DeviceListView : public BListView {
public:
	DeviceListView()
		:
		BListView("devices")
	{
	}

	virtual void MouseDown(BPoint where)
	{
		BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
		int32 buttons = 0;
		if (msg != nullptr)
			msg->FindInt32("buttons", &buttons);
		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			int32 index = IndexOf(where);
			DeviceListItem* item
				= dynamic_cast<DeviceListItem*>(ItemAt(index));
			if (item != nullptr) {
				Select(index);
				BPopUpMenu* menu = new BPopUpMenu("device-ctx",
					false, false);
				menu->SetAsyncAutoDestruct(true);
				menu->AddItem(new BMenuItem(item->IsFavorite()
					? B_TRANSLATE("Remove from favorites")
					: B_TRANSLATE("Add to favorites"),
					new BMessage(kMsgToggleFavorite)));
				menu->AddItem(new BMenuItem(item->IsPriority()
					? B_TRANSLATE("Remove from priority contacts")
					: B_TRANSLATE("Add to priority contacts"),
					new BMessage(kMsgTogglePriority)));
				menu->SetTargetForItems(BMessenger(Window()));
				menu->Go(ConvertToScreen(where), true, true, true);
			}
			return;
		}

		// Click primario su un "pulsante" inline della riga sotto il cursore
		// (visibile perche' hover o selezionata): seleziona la riga e lancia
		// l'azione, riusando gli stessi messaggi dei menu.
		int32 index = IndexOf(where);
		if (index >= 0
				&& (index == fHoverIndex || index == CurrentSelection())) {
			BRect fileR, textR, starR;
			DeviceListItem::ActionRects(ItemFrame(index), fileR, textR,
				starR);
			uint32 what = 0;
			if (fileR.Contains(where))
				what = kMsgSendFile;
			else if (textR.Contains(where))
				what = kMsgSendText;
			else if (starR.Contains(where))
				what = kMsgToggleFavorite;
			if (what != 0) {
				Select(index);
				if (Window() != nullptr)
					Window()->PostMessage(what);
				return;
			}
		}
		BListView::MouseDown(where);
	}

	virtual void MessageReceived(BMessage* msg)
	{
		// File trascinati dal Tracker su una riga: risolvi il dispositivo dal
		// punto di rilascio (o dalla selezione) e inoltra alla finestra un
		// messaggio pulito con refs + indice, che poi invia i file.
		if (msg->WasDropped() && msg->HasRef("refs")) {
			int32 idx = IndexOf(ConvertFromScreen(msg->DropPoint()));
			if (idx < 0)
				idx = CurrentSelection();
			if (idx >= 0 && Window() != nullptr) {
				Select(idx);
				BMessage fwd(kMsgDropOnDevice);
				fwd.AddInt32("deviceIndex", idx);
				entry_ref ref;
				for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++)
					fwd.AddRef("refs", &ref);
				Window()->PostMessage(&fwd);
				return;
			}
		}
		BListView::MessageReceived(msg);
	}

	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* drag)
	{
		BListView::MouseMoved(where, transit, drag);
		int32 idx = (transit == B_EXITED_VIEW) ? -1 : IndexOf(where);
		if (idx != fHoverIndex) {
			_SetHover(fHoverIndex, false);
			fHoverIndex = idx;
			_SetHover(fHoverIndex, true);
		}
	}

private:
	void _SetHover(int32 idx, bool on)
	{
		DeviceListItem* it = dynamic_cast<DeviceListItem*>(ItemAt(idx));
		if (it != nullptr) {
			it->SetShowActions(on);
			InvalidateItem(idx);
		}
	}

	int32 fHoverIndex = -1;
};


// --- HeaderView: pannello superiore con nome e stato -----------------------

class HeaderView : public BView {
public:
	HeaderView()
		:
		BView("header", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fIconBitmap(nullptr)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(350, 70));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 70));

		// Carica l'icona HVIF dall'eseguibile (risorsa BEOS:ICON).
		fIconBitmap = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
		app_info info;
		if (be_app->GetAppInfo(&info) == B_OK) {
			BFile file(&info.ref, B_READ_ONLY);
			BResources res(&file);
			size_t size;
			const void* data = res.LoadResource('VICN', "BEOS:ICON",
				&size);
			if (data) {
				BIconUtils::GetVectorIcon(
					static_cast<const uint8*>(data),
					size, fIconBitmap);
			}
		}
	}

	virtual ~HeaderView()
	{
		delete fIconBitmap;
	}

	void SetDeviceName(const char* name) { fDeviceName = name; Invalidate(); }
	void SetStatus(const char* status, bool good = false, bool error = false)
	{
		fStatus = status;
		fStatusGood = good;
		fStatusError = error;
		Invalidate();
	}
	void SetFingerprint(const char* fp) { fFingerprint = fp; Invalidate(); }

	virtual void Draw(BRect /*updateRect*/)
	{
		BRect bounds = Bounds();

		// Sfondo con leggero gradiente.
		rgb_color top = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), 0.95);
		rgb_color bottom = ui_color(B_PANEL_BACKGROUND_COLOR);
		for (float y = bounds.top; y <= bounds.bottom; y++) {
			float t = (y - bounds.top) / (bounds.bottom - bounds.top);
			rgb_color c;
			c.red = (uint8)(top.red + t * (bottom.red - top.red));
			c.green = (uint8)(top.green + t * (bottom.green - top.green));
			c.blue = (uint8)(top.blue + t * (bottom.blue - top.blue));
			c.alpha = 255;
			SetHighColor(c);
			StrokeLine(BPoint(bounds.left, y), BPoint(bounds.right, y));
		}

		float x = 16;
		float y = 20;

		// Icona dell'app.
		float iconSize = 32;
		if (fIconBitmap) {
			SetDrawingMode(B_OP_ALPHA);
			DrawBitmap(fIconBitmap,
				BPoint(x, y + (36 - iconSize) / 2 - 4));
			SetDrawingMode(B_OP_COPY);
		}

		float textX = x + iconSize + 14;

		// Nome dispositivo (grande).
		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		BFont nameFont(be_bold_font);
		nameFont.SetSize(16);
		SetFont(&nameFont);
		font_height nfh;
		nameFont.GetHeight(&nfh);
		DrawString(fDeviceName.String(), BPoint(textX, y + nfh.ascent));

		// Stato (sotto il nome).
		rgb_color statusColor;
		if (fStatusGood) {
			statusColor = (rgb_color){60, 180, 100, 255};
		} else if (fStatusError) {
			statusColor = (rgb_color){220, 60, 60, 255};
		} else {
			statusColor = tint_color(
				ui_color(B_PANEL_TEXT_COLOR), 0.7);
		}
		SetHighColor(statusColor);
		BFont statusFont(be_plain_font);
		statusFont.SetSize(11);
		SetFont(&statusFont);
		font_height sfh;
		statusFont.GetHeight(&sfh);
		DrawString(fStatus.String(),
			BPoint(textX, y + nfh.ascent + sfh.ascent + 4));

		// Pallino di stato a destra (verde=pronto, rosso=errore, grigio=neutro):
		// stesso codice colore del testo di stato.
		SetHighColor(statusColor);
		float dotR = 5;
		float dotCx = bounds.right - 18;
		float dotCy = bounds.top + bounds.Height() / 2;
		FillEllipse(BPoint(dotCx, dotCy), dotR, dotR);

		// Linea di separazione in basso.
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), 1.15));
		StrokeLine(BPoint(bounds.left, bounds.bottom),
			BPoint(bounds.right, bounds.bottom));
	}

private:
	BString fDeviceName = "LocalSend";
	BString fStatus;
	BString fFingerprint;
	bool fStatusGood = false;
	bool fStatusError = false;
	BBitmap* fIconBitmap;
};


// --- TextInputWindow -------------------------------------------------------

class TextInputWindow : public BWindow {
public:
	TextInputWindow(BWindow* target)
		:
		BWindow(BRect(150, 150, 530, 350), B_TRANSLATE("Send text…"),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target)
	{
		BStringView* label = new BStringView("hint", B_TRANSLATE("Type the message to send:"));

		fTextView = new BTextView("text");
		fTextView->SetStylable(false);
		fTextView->MakeEditable(true);
		fTextView->SetWordWrap(true);
		BScrollView* scroll = new BScrollView("tscroll", fTextView,
			0, false, true, B_FANCY_BORDER);

		BButton* sendBtn = new BButton(B_TRANSLATE("Send file…"),
			new BMessage(kMsgTextReady));
		BButton* cancelBtn = new BButton(B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(label)
			.Add(scroll, 1.0)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancelBtn)
				.Add(sendBtn)
			.End()
		.End();

		fTextView->MakeFocus(true);
		CenterIn(target->Frame());
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgTextReady:
			{
				const char* text = fTextView->Text();
				if (text && text[0] != '\0') {
					BMessage fwd(kMsgTextReady);
					fwd.AddString("text", text);
					fTarget->PostMessage(&fwd);
				}
				PostMessage(B_QUIT_REQUESTED);
				break;
			}
			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	BWindow* fTarget;
	BTextView* fTextView;
};


// --- HistoryListItem -------------------------------------------------------

class HistoryListItem : public BListItem {
public:
	HistoryListItem(const HistoryEntry& entry)
		:
		BListItem(),
		fSent(entry.sent),
		fFileName(entry.fileName.c_str()),
		fPeer(entry.peer.c_str()),
		fDate(entry.date.c_str()),
		fSize(entry.size)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool)
	{
		rgb_color bg = IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR);
		owner->SetHighColor(bg);
		owner->FillRect(frame);

		// Freccia colorata.
		float x = frame.left + 8;
		rgb_color arrow = fSent
			? (rgb_color){60, 160, 240, 255}
			: (rgb_color){80, 200, 100, 255};
		owner->SetHighColor(arrow);
		BFont arrowFont(be_bold_font);
		arrowFont.SetSize(14);
		owner->SetFont(&arrowFont);
		owner->DrawString(fSent ? "\xe2\x86\x91" : "\xe2\x86\x93",
			BPoint(x, frame.top + 18));

		float textX = x + 20;

		// Nome file (grassetto).
		rgb_color textColor = IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR);
		owner->SetHighColor(textColor);
		BFont nameFont(be_bold_font);
		nameFont.SetSize(be_plain_font->Size());
		owner->SetFont(&nameFont);
		font_height fh;
		nameFont.GetHeight(&fh);
		owner->DrawString(fFileName.String(),
			BPoint(textX, frame.top + 4 + fh.ascent));

		// Dettagli: peer, dimensione, data.
		BFont detailFont(be_plain_font);
		detailFont.SetSize(be_plain_font->Size() - 1);
		owner->SetFont(&detailFont);
		rgb_color detailColor = IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: tint_color(ui_color(B_LIST_ITEM_TEXT_COLOR), 0.7);
		owner->SetHighColor(detailColor);

		BString detail;
		detail << fPeer << " \xe2\x80\xa2 ";
		if (fSize >= 1024 * 1024)
			detail << (int)(fSize / (1024 * 1024)) << " MB";
		else
			detail << (int)(fSize / 1024) << " KB";
		detail << " \xe2\x80\xa2 " << fDate;

		detailFont.GetHeight(&fh);
		owner->DrawString(detail.String(),
			BPoint(textX, frame.bottom - 4 - fh.descent));
	}

	virtual void Update(BView* owner, const BFont*)
	{
		SetHeight(40);
		SetWidth(owner->Bounds().Width());
	}

private:
	bool fSent;
	BString fFileName;
	BString fPeer;
	BString fDate;
	long long fSize;
};


// --- QrCodeView ------------------------------------------------------------
//
// Piccola BView autonoma che codifica una stringa con libqrencode e la
// disegna alla dimensione richiesta. Il codice QR e' scalato in interi
// (nearest neighbor) per restare nitido; se lo scale a interi supera la
// larghezza disponibile, si adatta arrotondando per difetto.

class QrCodeView : public BView {
public:
	QrCodeView(const char* text, int preferredSide)
		:
		BView("qr", B_WILL_DRAW),
		fQr(QRcode_encodeString(text, 0, QR_ECLEVEL_L, QR_MODE_8, 1))
	{
		int modules = (fQr != NULL) ? fQr->width : 21;
		// Cornice bianca (quiet zone) di 4 moduli per la specifica QR.
		fScale = preferredSide / (modules + 8);
		if (fScale < 2)
			fScale = 2;
		int side = (modules + 8) * fScale;
		SetExplicitMinSize(BSize(side, side));
		SetExplicitMaxSize(BSize(side, side));
		SetExplicitPreferredSize(BSize(side, side));
		SetViewColor(255, 255, 255);
	}

	virtual ~QrCodeView()
	{
		if (fQr != NULL)
			QRcode_free(fQr);
	}

	virtual void Draw(BRect)
	{
		if (fQr == NULL)
			return;
		FillRect(Bounds(), B_SOLID_LOW); // sfondo bianco = view color
		SetHighColor(0, 0, 0);
		int m = fQr->width;
		int quiet = 4 * fScale;
		for (int y = 0; y < m; y++) {
			for (int x = 0; x < m; x++) {
				if (fQr->data[y * m + x] & 1) {
					BRect r(quiet + x * fScale, quiet + y * fScale,
						quiet + (x + 1) * fScale - 1,
						quiet + (y + 1) * fScale - 1);
					FillRect(r);
				}
			}
		}
	}

private:
	QRcode* fQr;
	int fScale;
};


// --- ShareLinkWindow -------------------------------------------------------
//
// Sostituisce l'alert del "Share via link": mostra il QR del download URL
// (utile con telefono/tablet: scansiona -> browser -> download) e i due
// pulsanti Chiudi / Stop condivisione.

class ShareLinkWindow : public BWindow {
public:
	enum { kMsgStop = 'STOP' };

	ShareLinkWindow(const BString& status, const BString& url,
			BMessenger target)
		:
		BWindow(BRect(160, 160, 560, 560), "LocalSend",
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target)
	{
		BStringView* statusView = new BStringView("status",
			status.String());
		BFont bold(be_bold_font);
		bold.SetSize(be_plain_font->Size());
		statusView->SetFont(&bold);

		QrCodeView* qr = new QrCodeView(url.String(), 260);

		BTextView* urlView = new BTextView("url");
		urlView->SetText(url.String());
		urlView->MakeEditable(false);
		urlView->SetWordWrap(false);
		urlView->SetStylable(false);
		urlView->SetExplicitMinSize(BSize(B_SIZE_UNSET, 24));

		BButton* stopBtn = new BButton(B_TRANSLATE("Stop sharing"),
			new BMessage(kMsgStop));
		BButton* closeBtn = new BButton(B_TRANSLATE("OK"),
			new BMessage(B_QUIT_REQUESTED));
		closeBtn->MakeDefault(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(statusView)
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(qr)
				.AddGlue()
			.End()
			.Add(urlView)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(stopBtn)
				.AddGlue()
				.Add(closeBtn)
			.End()
		.End();

		CenterOnScreen();
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgStop:
				// Ripassa la palla alla MainWindow che ferma il server
				// (StopDownloadServer non e' accessibile da qui).
				fTarget.SendMessage(kMsgStopShare);
				PostMessage(B_QUIT_REQUESTED);
				break;
			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	BMessenger fTarget;
};


// --- TextReceivedWindow ----------------------------------------------------
//
// Dialog per un messaggio di testo in arrivo. Sostituisce il BAlert
// precedente perche' BAlert cappa a 3 pulsanti e ci servono 4 azioni:
// Salva su file, Apri nell'editor, Copia negli appunti, Chiudi.

class TextReceivedWindow : public BWindow {
public:
	TextReceivedWindow(const BString& title, const char* text)
		:
		BWindow(BRect(140, 140, 640, 460), title.String(),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fText(text != NULL ? text : ""),
		fSavePanel(nullptr)
	{
		BTextView* view = new BTextView("text");
		view->SetText(fText.String());
		view->MakeEditable(false);
		view->SetWordWrap(true);
		view->SetInsets(6, 6, 6, 6);
		view->SetStylable(false);
		BScrollView* scroll = new BScrollView("tscroll", view,
			0, false, true, B_NO_BORDER);

		BButton* saveBtn = new BButton(B_TRANSLATE("Save to file"),
			new BMessage(kMsgTxRxSave));
		BButton* openBtn = new BButton(B_TRANSLATE("Open in editor"),
			new BMessage(kMsgTxRxOpen));
		BButton* copyBtn = new BButton(B_TRANSLATE("Copy"),
			new BMessage(kMsgTxRxCopy));
		BButton* closeBtn = new BButton(B_TRANSLATE("OK"),
			new BMessage(B_QUIT_REQUESTED));
		closeBtn->MakeDefault(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(scroll, 1.0)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(saveBtn)
				.Add(openBtn)
				.AddGlue()
				.Add(copyBtn)
				.Add(closeBtn)
			.End()
		.End();

		CenterOnScreen();
	}

	virtual ~TextReceivedWindow()
	{
		delete fSavePanel;
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgTxRxCopy:
				CopyToClipboard();
				PostMessage(B_QUIT_REQUESTED);
				break;

			case kMsgTxRxOpen:
				OpenInEditor();
				PostMessage(B_QUIT_REQUESTED);
				break;

			case kMsgTxRxSave:
			{
				if (fSavePanel == nullptr) {
					fSavePanel = new BFilePanel(B_SAVE_PANEL,
						new BMessenger(this), NULL, 0, false,
						new BMessage(kMsgTxRxSaveTo));
					fSavePanel->SetSaveText("message.txt");
				}
				fSavePanel->Show();
				break;
			}

			case kMsgTxRxSaveTo:
			{
				entry_ref dir;
				const char* name = nullptr;
				if (msg->FindRef("directory", &dir) == B_OK
						&& msg->FindString("name", &name) == B_OK) {
					BPath dirPath(&dir);
					BString path(dirPath.Path());
					path << "/" << name;
					BFile out(path.String(),
						B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
					if (out.InitCheck() == B_OK)
						out.Write(fText.String(), fText.Length());
				}
				PostMessage(B_QUIT_REQUESTED);
				break;
			}

			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	void CopyToClipboard()
	{
		if (be_clipboard->Lock()) {
			be_clipboard->Clear();
			BMessage* clip = be_clipboard->Data();
			if (clip != NULL) {
				clip->AddData("text/plain", B_MIME_TYPE,
					fText.String(), fText.Length());
			}
			be_clipboard->Commit();
			be_clipboard->Unlock();
		}
	}

	void OpenInEditor()
	{
		BPath tmpDir;
		if (find_directory(B_SYSTEM_TEMP_DIRECTORY, &tmpDir) != B_OK)
			tmpDir.SetTo("/tmp");
		char pathBuf[B_PATH_NAME_LENGTH];
		snprintf(pathBuf, sizeof(pathBuf),
			"%s/localsend-message-XXXXXX", tmpDir.Path());
		int fd = mkstemp(pathBuf);
		if (fd < 0)
			return;
		write(fd, fText.String(), fText.Length());
		close(fd);
		std::string finalPath = std::string(pathBuf) + ".txt";
		rename(pathBuf, finalPath.c_str());

		entry_ref ref;
		if (get_ref_for_path(finalPath.c_str(), &ref) != B_OK)
			return;
		BMessage refs(B_REFS_RECEIVED);
		refs.AddRef("refs", &ref);
		// Prova StyledEdit; fallback al preferred handler di text/plain.
		if (be_roster->Launch("application/x-vnd.Haiku-StyledEdit", &refs)
				!= B_OK) {
			be_roster->Launch(&ref);
		}
	}

	BString fText;
	BFilePanel* fSavePanel;
};


// --- HistoryWindow ---------------------------------------------------------

class HistoryWindow : public BWindow {
public:
	HistoryWindow(TransferHistory* history, BWindow* parent)
		:
		BWindow(BRect(120, 120, 530, 430), B_TRANSLATE("History"),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fHistory(history)
	{
		fList = new BListView("history");
		BScrollView* scroll = new BScrollView("hscroll", fList,
			0, false, true, B_NO_BORDER);

		BButton* clearBtn = new BButton(B_TRANSLATE("Clear history"),
			new BMessage(kMsgClearHistory));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(scroll, 1.0)
			.AddGroup(B_HORIZONTAL)
				.Add(clearBtn)
				.AddGlue()
			.End()
		.End();

		PopulateList();
		CenterIn(parent->Frame());
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgClearHistory:
				fHistory->Clear();
				while (fList->CountItems() > 0)
					delete fList->RemoveItem((int32)0);
				break;
			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	void PopulateList()
	{
		// Mostra dal piu' recente al piu' vecchio.
		for (int i = (int)fHistory->entries.size() - 1; i >= 0; i--)
			fList->AddItem(new HistoryListItem(fHistory->entries[i]));
	}

	TransferHistory* fHistory;
	BListView* fList;
};


// --- SettingsWindow --------------------------------------------------------

class SettingsWindow : public BWindow {
public:
	SettingsWindow(AppSettings* settings, BWindow* target)
		:
		BWindow(BRect(150, 150, 560, 530), B_TRANSLATE("Settings"),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fSettings(settings),
		fTarget(target)
	{
		// Campi.
		fAliasField = new BTextControl(B_TRANSLATE("Device name:"),
			settings->alias.c_str(), NULL);
		fDestDirField = new BTextControl(B_TRANSLATE("Save folder:"),
			settings->destDir.c_str(), NULL);
		BButton* browseBtn = new BButton(B_TRANSLATE("Browse…"),
			new BMessage(kMsgBrowseDir));

		BString portStr;
		portStr << settings->port;
		fPortField = new BTextControl(B_TRANSLATE("Port:"), portStr.String(), NULL);

		fPinField = new BTextControl(B_TRANSLATE("PIN (empty = none):"),
			settings->pin.c_str(), NULL);
		fQuickSaveBox = new BCheckBox(B_TRANSLATE("Quick save (accept all)"), NULL);
		fQuickSaveBox->SetValue(settings->quickSave ? B_CONTROL_ON
			: B_CONTROL_OFF);
		fAutoAcceptFavBox = new BCheckBox(B_TRANSLATE("Auto-accept from favorites"),
			NULL);
		fAutoAcceptFavBox->SetValue(settings->autoAcceptFavorites
			? B_CONTROL_ON : B_CONTROL_OFF);
		fAutoDownloadBoardBox = new BCheckBox(B_TRANSLATE("Auto-download from favorites' boards"),
			NULL);
		fAutoDownloadBoardBox->SetValue(settings->autoDownloadBoard
			? B_CONTROL_ON : B_CONTROL_OFF);
		// Visibilita' della bacheca: tutti / preferiti / contatti prioritari.
		fBoardVisMenu = new BPopUpMenu("boardvis");
		fBoardVisMenu->AddItem(new BMenuItem(B_TRANSLATE("Everyone"), NULL));
		fBoardVisMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Favorites only"), NULL));
		fBoardVisMenu->AddItem(new BMenuItem(
			B_TRANSLATE("Priority contacts only"), NULL));
		{
			int v = settings->boardVisibility;
			if (v < 0 || v > 2)
				v = 0;
			fBoardVisMenu->ItemAt(v)->SetMarked(true);
		}
		fBoardVisField = new BMenuField(B_TRANSLATE("Board visible to:"),
			fBoardVisMenu);
		fManagePriorityBtn = new BButton(
			B_TRANSLATE("Manage priority contacts…"),
			new BMessage(kMsgManagePriority));

		fHttpsBox = new BCheckBox(B_TRANSLATE("Enable HTTPS"), NULL);
		fHttpsBox->SetValue(B_CONTROL_ON);
		fHttpsBox->SetEnabled(false);

		// La lingua segue le preferenze di sistema (Locale Kit): nessun
		// selettore qui. Per cambiarla: Preferenze > Locale.

		// Pulsanti.
		BButton* saveBtn = new BButton(B_TRANSLATE("Save"),
			new BMessage(kMsgSettingsSave));
		BButton* cancelBtn = new BButton(B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));

		// Toggle Deskbar: etichetta scelta in base allo stato corrente.
		fDeskbarBtn = new BButton("deskbar",
			IsDeskbarItemInstalled()
				? B_TRANSLATE("Remove from Deskbar") : B_TRANSLATE("Add to Deskbar"),
			new BMessage(kMsgToggleDeskbar));

		// Intestazioni sezione (grassetto).
		auto MakeLabel = [](const char* text) {
			BStringView* sv = new BStringView("", text);
			sv->SetFont(be_bold_font);
			return sv;
		};

		// Layout senza BBox (piu' stabile).
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(MakeLabel(B_TRANSLATE("General")))
			.Add(fAliasField)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(fDestDirField, 1.0)
				.Add(browseBtn, 0.0)
			.End()
			.AddStrut(B_USE_ITEM_SPACING)
			.Add(MakeLabel(B_TRANSLATE("Network")))
			.Add(fPortField)
			.Add(fHttpsBox)
			.AddStrut(B_USE_ITEM_SPACING)
			.Add(MakeLabel(B_TRANSLATE("Security")))
			.Add(fPinField)
			.Add(fQuickSaveBox)
			.Add(fAutoAcceptFavBox)
			.Add(fAutoDownloadBoardBox)
			.Add(fBoardVisField)
			.AddGroup(B_HORIZONTAL)
				.Add(fManagePriorityBtn)
				.AddGlue()
			.End()
			.AddStrut(B_USE_ITEM_SPACING)
			.Add(MakeLabel(B_TRANSLATE("Integration")))
			.AddGroup(B_HORIZONTAL)
				.Add(fDeskbarBtn)
				.AddGlue()
			.End()
			// Replicant desktop: anteprima viva della drop-zone con la
			// maniglia BDragger. L'utente la trascina sul desktop (serve
			// "Show replicants" attivo nella Deskbar).
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(new BStringView("", B_TRANSLATE("Desktop replicant (drag to your desktop):")))
				.Add(NewLocalSendDropView(48))
				.AddGlue()
			.End()
			.AddGlue()
			.AddGroup(B_HORIZONTAL)
				.AddGlue()
				.Add(cancelBtn)
				.Add(saveBtn)
			.End()
		.End();

		fDirPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_DIRECTORY_NODE, false, new BMessage(kMsgDirSelected));

		CenterIn(target->Frame());
	}

	virtual ~SettingsWindow()
	{
		delete fDirPanel;
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgShowSettings:
				// Finestra gia' aperta: portala in primo piano (istanza unica).
				Activate(true);
				break;

			case kMsgBrowseDir:
				fDirPanel->Show();
				break;

			case kMsgToggleDeskbar:
			{
				status_t err;
				if (IsDeskbarItemInstalled()) {
					err = RemoveDeskbarItem();
				} else {
					err = InstallDeskbarItem();
				}
				bool installed = IsDeskbarItemInstalled();
				fDeskbarBtn->SetLabel(installed
					? B_TRANSLATE("Remove from Deskbar")
					: B_TRANSLATE("Add to Deskbar"));
				// Persisti subito la scelta: la Deskbar non garantisce
				// la rigenerazione del replicant al boot, quindi al
				// prossimo avvio dell'app lo re-installeremo da soli
				// se questo flag e' true.
				if (fSettings->deskbarItem != installed) {
					fSettings->deskbarItem = installed;
					fSettings->Save(kSettingsFile);
				}
				// Autostart al login agganciato al replicant: per essere
				// "tray app" davvero, l'app deve partire da sola al boot
				// (l'auto-restore del replicant agisce solo se l'app gira).
				// Errore non bloccante: solo log.
				status_t asErr = installed
					? EnableAutostart() : DisableAutostart();
				if (asErr != B_OK) {
					fprintf(stderr,
						"[autostart] toggle fallito: %s (%ld)\n",
						strerror(asErr), (long)asErr);
				}
				if (err != B_OK) {
					char buf[256];
					snprintf(buf, sizeof(buf),
						"Operazione Deskbar fallita: %s (%ld)",
						strerror(err), (long)err);
					BAlert* a = new BAlert("LocalSend", buf,
						B_TRANSLATE("OK"), NULL, NULL,
						B_WIDTH_AS_USUAL, B_WARNING_ALERT);
					a->Go();
				}
				break;
			}

			case kMsgDirSelected:
			{
				entry_ref ref;
				if (msg->FindRef("refs", &ref) == B_OK) {
					BPath path(&ref);
					if (path.InitCheck() == B_OK)
						fDestDirField->SetText(path.Path());
				}
				break;
			}

			case kMsgSettingsSave:
			{
				fSettings->alias = fAliasField->Text();
				fSettings->destDir = fDestDirField->Text();
				fSettings->port = atoi(fPortField->Text());
				if (fSettings->port <= 0 || fSettings->port > 65535)
					fSettings->port = 53317;
				fSettings->pin = fPinField->Text();
				fSettings->quickSave
					= (fQuickSaveBox->Value() == B_CONTROL_ON);
				fSettings->autoAcceptFavorites
					= (fAutoAcceptFavBox->Value() == B_CONTROL_ON);
				fSettings->autoDownloadBoard
					= (fAutoDownloadBoardBox->Value() == B_CONTROL_ON);
				{
					BMenuItem* vm = fBoardVisMenu->FindMarked();
					int idx = vm ? fBoardVisMenu->IndexOf(vm) : 0;
					if (idx < 0 || idx > 2)
						idx = 0;
					fSettings->boardVisibility = idx;
				}
				fSettings->Save(kSettingsFile);

				// Notifica la finestra principale.
				fTarget->PostMessage(new BMessage(kMsgSettingsSave));

				PostMessage(B_QUIT_REQUESTED);
				break;
			}

			case kMsgManagePriority:
				// La finestra di gestione la possiede la MainWindow (ha i
				// registri e i dispositivi): inoltra e basta.
				fTarget->PostMessage(kMsgManagePriority);
				break;

			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	AppSettings* fSettings;
	BWindow* fTarget;

	BTextControl* fAliasField;
	BTextControl* fDestDirField;
	BTextControl* fPortField;
	BTextControl* fPinField;
	BCheckBox* fQuickSaveBox;
	BCheckBox* fAutoAcceptFavBox;
	BCheckBox* fAutoDownloadBoardBox;
	BPopUpMenu* fBoardVisMenu;
	BMenuField* fBoardVisField;
	BButton* fManagePriorityBtn;
	BCheckBox* fHttpsBox;
	BFilePanel* fDirPanel;
	BButton* fDeskbarBtn;
};


// --- BoardWindow -------------------------------------------------------------

static BString
BoardSizeLabel(int64 size)
{
	BString s;
	if (size >= 1024 * 1024)
		s << (int32)(size / (1024 * 1024)) << " MB";
	else
		s << (int32)(size / 1024) << " KB";
	return s;
}


// Item della bacheca, locale o remoto: icona 32x32 + nome + sottotitolo
// (dimensione; per i remoti anche l'alias del peer). L'icona locale viene
// dal file vero (BNodeInfo), quella remota dal MIME type dei metadati.
// Un item remoto porta con se' tutto quello che serve per chiedere il
// download alla MainWindow (host, fileId, nome, dimensione).
class BoardFileItem : public BListItem {
public:
	// Bacheca locale: path pieno del file condiviso.
	BoardFileItem(const char* path, const char* name)
		:
		fLocal(true),
		fLocalPath(path),
		fName(name),
		fSize(0),
		fIcon(nullptr)
	{
		BEntry entry(path);
		off_t sz = 0;
		if (entry.GetSize(&sz) == B_OK)
			fSize = sz;
		fSubtitle = BoardSizeLabel(fSize);

		entry_ref ref;
		if (entry.GetRef(&ref) == B_OK) {
			BBitmap* bm = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
			if (BNodeInfo::GetTrackerIcon(&ref, bm,
					(icon_size)32) == B_OK) {
				fIcon = bm;
			} else
				delete bm;
		}
	}

	// Bacheca remota: metadati dal prepare-download del peer.
	BoardFileItem(const char* host, const char* alias,
		const char* fingerprint, const char* fileId,
		const char* fileName, const char* fileType, int64 size)
		:
		fLocal(false),
		fHost(host),
		fAlias(alias),
		fFingerprint(fingerprint),
		fFileId(fileId),
		fName(fileName),
		fFileType(fileType),
		fSize(size),
		fIcon(_MimeIcon(fileType))
	{
		fSubtitle << fAlias << " \xe2\x80\x94 " << BoardSizeLabel(size);
	}

	virtual ~BoardFileItem()
	{
		delete fIcon;
	}

	bool IsLocal() const { return fLocal; }
	const BString& LocalPath() const { return fLocalPath; }
	const char* Name() const { return fName.String(); }
	const BString& FileType() const { return fFileType; }
	const BBitmap* Icon() const { return fIcon; }

	void FillRequest(BMessage* req) const
	{
		req->AddString("host", fHost);
		req->AddString("alias", fAlias);
		req->AddString("fingerprint", fFingerprint);
		req->AddString("fileId", fFileId);
		req->AddString("fileName", fName);
		req->AddInt64("size", fSize);
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		BListItem::Update(owner, font);
		// Icona 32px + margini: due righe di testo ci stanno comode.
		if (Height() < 40)
			SetHeight(40);
	}

	virtual void DrawItem(BView* owner, BRect frame, bool /*complete*/)
	{
		rgb_color bg = IsSelected()
			? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
			: ui_color(B_LIST_BACKGROUND_COLOR);
		owner->SetHighColor(bg);
		owner->SetLowColor(bg);
		owner->FillRect(frame);

		if (fIcon) {
			owner->SetDrawingMode(B_OP_ALPHA);
			owner->DrawBitmap(fIcon, BPoint(frame.left + 5,
				frame.top + (frame.Height() - 31) / 2));
			owner->SetDrawingMode(B_OP_COPY);
		}

		rgb_color text = IsSelected()
			? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR)
			: ui_color(B_LIST_ITEM_TEXT_COLOR);

		float x = frame.left + 5 + 32 + 7;
		BFont font;
		owner->GetFont(&font);
		font_height fh;
		font.GetHeight(&fh);

		owner->SetHighColor(text);
		owner->DrawString(fName.String(),
			BPoint(x, frame.top + 4 + fh.ascent));

		// Sottotitolo: font ridotto e colore attenuato verso lo sfondo.
		BFont small(font);
		small.SetSize(font.Size() * 0.85f);
		owner->SetFont(&small);
		font_height sfh;
		small.GetHeight(&sfh);
		owner->SetHighColor(mix_color(text, bg, 110));
		owner->DrawString(fSubtitle.String(), BPoint(x,
			frame.top + 4 + fh.ascent + fh.descent + 2 + sfh.ascent));
		owner->SetFont(&font);
	}

private:
	// Icona vettoriale dal MIME database: tipo esatto, poi supertipo,
	// poi il generico application/octet-stream.
	static BBitmap* _MimeIcon(const char* type)
	{
		auto tryType = [](const char* t) -> BBitmap* {
			if (t == nullptr || *t == '\0')
				return nullptr;
			BMimeType mt(t);
			uint8* data = nullptr;
			size_t size = 0;
			if (mt.GetIcon(&data, &size) != B_OK || data == nullptr)
				return nullptr;
			BBitmap* bm = new BBitmap(BRect(0, 0, 31, 31), B_RGBA32);
			if (BIconUtils::GetVectorIcon(data, size, bm) != B_OK) {
				delete bm;
				free(data);
				return nullptr;
			}
			free(data);
			return bm;
		};

		BBitmap* bm = tryType(type);
		if (bm == nullptr && type != nullptr && *type != '\0') {
			BMimeType mt(type);
			BMimeType super;
			if (mt.GetSupertype(&super) == B_OK)
				bm = tryType(super.Type());
		}
		if (bm == nullptr)
			bm = tryType("application/octet-stream");
		return bm;
	}

	bool fLocal;
	BString fLocalPath;   // solo locale
	BString fHost;        // solo remoto
	BString fAlias;
	BString fFingerprint;
	BString fFileId;
	BString fName;
	BString fFileType;
	BString fSubtitle;
	int64 fSize;
	BBitmap* fIcon;
};


// Lista "La mia bacheca": i file si trascinano fuori come veri entry_ref
// (Tracker li copia da solo); tasto destro per Apri / Rimuovi.
class MyBoardListView : public BListView {
public:
	MyBoardListView()
		:
		BListView("myboard")
	{
	}

	virtual bool InitiateDrag(BPoint, int32 index, bool)
	{
		BoardFileItem* item
			= dynamic_cast<BoardFileItem*>(ItemAt(index));
		if (item == nullptr || item->LocalPath().IsEmpty())
			return false;
		entry_ref ref;
		if (get_ref_for_path(item->LocalPath().String(), &ref) != B_OK)
			return false;

		BMessage drag(B_SIMPLE_DATA);
		drag.AddRef("refs", &ref);
		drag.AddString("be:clip_name", item->Name());
		if (item->Icon() != nullptr) {
			DragMessage(&drag, new BBitmap(*item->Icon()),
				B_OP_ALPHA, BPoint(16, 16));
		} else
			DragMessage(&drag, ItemFrame(index));
		return true;
	}

	virtual void MouseDown(BPoint where)
	{
		BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
		int32 buttons = 0;
		if (msg != nullptr)
			msg->FindInt32("buttons", &buttons);
		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			int32 index = IndexOf(where);
			if (index >= 0
				&& dynamic_cast<BoardFileItem*>(ItemAt(index))) {
				Select(index);
				BPopUpMenu* menu = new BPopUpMenu("board-ctx",
					false, false);
				menu->SetAsyncAutoDestruct(true);
				menu->AddItem(new BMenuItem(B_TRANSLATE("Open"),
					new BMessage(kMsgBoardOpenLocal)));
				menu->AddItem(new BMenuItem(B_TRANSLATE("Remove from board"),
					new BMessage(kMsgBoardRemove)));
				menu->SetTargetForItems(BMessenger(Window()));
				menu->Go(ConvertToScreen(where), true, true, true);
			}
			return;
		}
		BListView::MouseDown(where);
	}
};


// Lista "Dalla cerchia": drag negoziato (B_COPY_TARGET) — trascini il file
// su una cartella di Tracker, Tracker risponde con la destinazione e il
// download parte direttamente li'. Tasto destro: Scarica / Scarica e apri.
class RemoteBoardListView : public BListView {
public:
	RemoteBoardListView()
		:
		BListView("remoteboard")
	{
	}

	virtual bool InitiateDrag(BPoint, int32 index, bool)
	{
		BoardFileItem* item
			= dynamic_cast<BoardFileItem*>(ItemAt(index));
		if (item == nullptr || item->IsLocal())
			return false;

		// Ricorda cosa stiamo trascinando: la risposta B_COPY_TARGET
		// di Tracker non riporta i nostri campi (un drag alla volta).
		fPending = BMessage(kMsgBoardDownload);
		item->FillRequest(&fPending);

		BMessage drag(B_SIMPLE_DATA);
		drag.AddInt32("be:actions", B_COPY_TARGET);
		drag.AddString("be:types", B_FILE_MIME_TYPE);
		if (item->FileType().Length() > 0)
			drag.AddString("be:filetypes", item->FileType());
		drag.AddString("be:clip_name", item->Name());
		if (item->Icon() != nullptr) {
			DragMessage(&drag, new BBitmap(*item->Icon()),
				B_OP_ALPHA, BPoint(16, 16));
		} else
			DragMessage(&drag, ItemFrame(index));
		return true;
	}

	virtual void MessageReceived(BMessage* msg)
	{
		if (msg->what == B_COPY_TARGET) {
			// Risposta di Tracker al drag negoziato: directory (e nome)
			// di destinazione. Completa la richiesta e passala alla
			// BoardWindow, che la inoltra alla MainWindow.
			entry_ref dirRef;
			if (msg->FindRef("directory", &dirRef) == B_OK
					&& fPending.HasString("host")) {
				BPath dir(&dirRef);
				const char* name = nullptr;
				msg->FindString("name", &name);
				BMessage req(fPending);
				if (dir.InitCheck() == B_OK)
					req.AddString("destdir", dir.Path());
				if (name != nullptr)
					req.AddString("destname", name);
				Window()->PostMessage(&req);
			}
			return;
		}
		BListView::MessageReceived(msg);
	}

	virtual void MouseDown(BPoint where)
	{
		BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
		int32 buttons = 0;
		if (msg != nullptr)
			msg->FindInt32("buttons", &buttons);
		if (buttons & B_SECONDARY_MOUSE_BUTTON) {
			int32 index = IndexOf(where);
			BoardFileItem* item
				= dynamic_cast<BoardFileItem*>(ItemAt(index));
			if (item != nullptr && !item->IsLocal()) {
				Select(index);
				BPopUpMenu* menu = new BPopUpMenu("board-ctx",
					false, false);
				menu->SetAsyncAutoDestruct(true);
				menu->AddItem(new BMenuItem(B_TRANSLATE("Download"),
					new BMessage(kMsgBoardDownload)));
				menu->AddItem(new BMenuItem(B_TRANSLATE("Download and open"),
					new BMessage(kMsgBoardDownloadOpen)));
				menu->SetTargetForItems(BMessenger(Window()));
				menu->Go(ConvertToScreen(where), true, true, true);
			}
			return;
		}
		BListView::MouseDown(where);
	}

private:
	BMessage fPending;
};


// Finestra Bacheca: sopra i file pubblicati da questo dispositivo, sotto
// quelli in bacheca sui peer della LAN. Nessun accesso alle strutture della
// MainWindow: riceve snapshot completi via kMsgBoardDataChanged e le rimanda
// le azioni (aggiungi/svuota/scarica) via BMessenger.
class BoardWindow : public BWindow {
public:
	BoardWindow(BMessenger target)
		:
		BWindow(BRect(160, 160, 600, 600), B_TRANSLATE("Board"), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target)
	{
		auto MakeLabel = [](const char* text) {
			BStringView* sv = new BStringView("", text);
			sv->SetFont(be_bold_font);
			return sv;
		};

		fMyList = new MyBoardListView();
		// Doppio click su un file locale = aprilo.
		fMyList->SetInvocationMessage(new BMessage(kMsgBoardOpenLocal));
		BScrollView* myScroll = new BScrollView("myscroll", fMyList,
			0, false, true, B_FANCY_BORDER);

		fRemoteList = new RemoteBoardListView();
		// Doppio click su un file remoto = scaricalo.
		fRemoteList->SetInvocationMessage(
			new BMessage(kMsgBoardDownload));
		BScrollView* remScroll = new BScrollView("remscroll", fRemoteList,
			0, false, true, B_FANCY_BORDER);

		BButton* addBtn = new BButton(B_TRANSLATE("Add files…"),
			new BMessage(kMsgBoardPickFiles));
		BButton* clearBtn = new BButton(B_TRANSLATE("Clear board"),
			new BMessage(kMsgStopShare));
		BButton* dlAllBtn = new BButton(B_TRANSLATE("Download all"),
			new BMessage(kMsgBoardDownloadAll));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(MakeLabel(B_TRANSLATE("My board")))
			.Add(myScroll, 1.0)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(addBtn)
				.Add(clearBtn)
				.AddGlue()
			.End()
			.AddStrut(B_USE_ITEM_SPACING)
			.Add(MakeLabel(B_TRANSLATE("From the circle")))
			.Add(remScroll, 1.0)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(dlAllBtn)
				.AddGlue()
			.End()
		.End();

		SetSizeLimits(340, B_SIZE_UNLIMITED, 320, B_SIZE_UNLIMITED);
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgShowBoard:
				// Gia' aperta: portala solo in primo piano.
				Activate(true);
				break;

			case kMsgBoardDataChanged:
				_Rebuild(msg);
				break;

			case kMsgBoardDownload:
			{
				// Richiesta gia' completa (drag negoziato): inoltrala.
				if (msg->HasString("host")) {
					fTarget.SendMessage(msg);
					break;
				}
				// Invocazione (doppio click / menu) della lista remota.
				int32 sel = fRemoteList->CurrentSelection();
				BoardFileItem* item = dynamic_cast<BoardFileItem*>(
					fRemoteList->ItemAt(sel));
				if (item && !item->IsLocal())
					_RequestDownload(item, false);
				break;
			}

			case kMsgBoardDownloadOpen:
			{
				int32 sel = fRemoteList->CurrentSelection();
				BoardFileItem* item = dynamic_cast<BoardFileItem*>(
					fRemoteList->ItemAt(sel));
				if (item && !item->IsLocal())
					_RequestDownload(item, true);
				break;
			}

			case kMsgBoardDownloadAll:
			{
				for (int32 i = 0; i < fRemoteList->CountItems(); i++) {
					BoardFileItem* item = dynamic_cast<BoardFileItem*>(
						fRemoteList->ItemAt(i));
					if (item && !item->IsLocal())
						_RequestDownload(item, false);
				}
				break;
			}

			case kMsgBoardOpenLocal:
			{
				// Doppio click / "Apri" sulla lista locale.
				int32 sel = fMyList->CurrentSelection();
				BoardFileItem* item = dynamic_cast<BoardFileItem*>(
					fMyList->ItemAt(sel));
				if (item && !item->LocalPath().IsEmpty()) {
					entry_ref ref;
					if (get_ref_for_path(item->LocalPath().String(),
							&ref) == B_OK)
						be_roster->Launch(&ref);
				}
				break;
			}

			case kMsgBoardRemove:
			{
				int32 sel = fMyList->CurrentSelection();
				BoardFileItem* item = dynamic_cast<BoardFileItem*>(
					fMyList->ItemAt(sel));
				if (item && !item->LocalPath().IsEmpty()) {
					BMessage req(kMsgBoardRemove);
					req.AddString("path", item->LocalPath());
					fTarget.SendMessage(&req);
				}
				break;
			}

			case kMsgBoardPickFiles:
			case kMsgStopShare:
				// Azioni sulla bacheca locale: le esegue la MainWindow.
				fTarget.SendMessage(msg->what);
				break;

			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	void _RequestDownload(BoardFileItem* item, bool openAfter)
	{
		BMessage req(kMsgBoardDownload);
		item->FillRequest(&req);
		if (openAfter)
			req.AddBool("open", true);
		fTarget.SendMessage(&req);
	}

	void _Rebuild(BMessage* msg)
	{
		while (fMyList->CountItems() > 0)
			delete fMyList->RemoveItem((int32)0);
		while (fRemoteList->CountItems() > 0)
			delete fRemoteList->RemoveItem((int32)0);

		for (int32 i = 0; ; i++) {
			const char* name = nullptr;
			if (msg->FindString("my", i, &name) != B_OK)
				break;
			const char* path = nullptr;
			msg->FindString("mypath", i, &path);
			fMyList->AddItem(new BoardFileItem(path ? path : "",
				name ? name : "?"));
		}
		if (fMyList->CountItems() == 0)
			fMyList->AddItem(new BStringItem(B_TRANSLATE("Board is empty")));

		for (int32 i = 0; ; i++) {
			const char* fname = nullptr;
			if (msg->FindString("rname", i, &fname) != B_OK)
				break;
			const char* alias = nullptr;
			const char* host = nullptr;
			const char* fp = nullptr;
			const char* id = nullptr;
			const char* type = nullptr;
			msg->FindString("ralias", i, &alias);
			msg->FindString("rhost", i, &host);
			msg->FindString("rfp", i, &fp);
			msg->FindString("rid", i, &id);
			msg->FindString("rtype", i, &type);
			int64 sz = 0;
			msg->FindInt64("rsize", i, &sz);

			fRemoteList->AddItem(new BoardFileItem(
				host ? host : "", alias ? alias : "?",
				fp ? fp : "", id ? id : "",
				fname ? fname : "?", type ? type : "", sz));
		}
		if (fRemoteList->CountItems() == 0)
			fRemoteList->AddItem(new BStringItem(B_TRANSLATE("Board is empty")));
	}

	BMessenger fTarget;
	BListView* fMyList;
	BListView* fRemoteList;
};


// --- PriorityContactsWindow --------------------------------------------------

// Item della lista contatti prioritari: alias + fingerprint (identita' TLS).
// Porta il fingerprint per la rimozione.
class PriorityItem : public BStringItem {
public:
	PriorityItem(const BString& label, const char* fingerprint)
		:
		BStringItem(label.String()),
		fFingerprint(fingerprint)
	{
	}
	const BString& Fingerprint() const { return fFingerprint; }
private:
	BString fFingerprint;
};


// Gestione della cerchia ristretta che vede la bacheca. Non tocca i registri
// della MainWindow: riceve lo snapshot via kMsgPriorityChanged e rimanda la
// rimozione via kMsgPriorityRemove.
class PriorityContactsWindow : public BWindow {
public:
	PriorityContactsWindow(BMessenger target)
		:
		BWindow(BRect(180, 180, 560, 520),
			B_TRANSLATE("Priority contacts"), B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target)
	{
		BStringView* hint = new BStringView("hint",
			B_TRANSLATE("These devices can see your board when visibility "
				"is set to \"Priority contacts only\"."));
		hint->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

		fList = new BListView("priority");
		BScrollView* scroll = new BScrollView("pscroll", fList,
			0, false, true, B_FANCY_BORDER);

		fRemoveBtn = new BButton(B_TRANSLATE("Remove"),
			new BMessage(kMsgPriorityRemove));
		BButton* closeBtn = new BButton(B_TRANSLATE("Close"),
			new BMessage(B_QUIT_REQUESTED));

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(hint)
			.Add(scroll, 1.0)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(fRemoveBtn)
				.AddGlue()
				.Add(closeBtn)
			.End()
		.End();

		SetSizeLimits(300, B_SIZE_UNLIMITED, 240, B_SIZE_UNLIMITED);
	}

	virtual void MessageReceived(BMessage* msg)
	{
		switch (msg->what) {
			case kMsgManagePriority:
				Activate(true);
				break;

			case kMsgPriorityChanged:
				_Rebuild(msg);
				break;

			case kMsgPriorityRemove:
			{
				PriorityItem* item = dynamic_cast<PriorityItem*>(
					fList->ItemAt(fList->CurrentSelection()));
				if (item != nullptr) {
					BMessage req(kMsgPriorityRemove);
					req.AddString("fingerprint", item->Fingerprint());
					fTarget.SendMessage(&req);
				}
				break;
			}

			default:
				BWindow::MessageReceived(msg);
		}
	}

private:
	void _Rebuild(BMessage* msg)
	{
		while (fList->CountItems() > 0)
			delete fList->RemoveItem((int32)0);

		for (int32 i = 0; ; i++) {
			const char* fp = nullptr;
			if (msg->FindString("fp", i, &fp) != B_OK)
				break;
			const char* alias = nullptr;
			msg->FindString("alias", i, &alias);
			BString label;
			BString shortFp(fp);
			if (shortFp.Length() > 16)
				shortFp.Truncate(16).Append("\xe2\x80\xa6");
			label << (alias && *alias ? alias : B_TRANSLATE("Unknown"))
				<< "  \xc2\xb7  " << shortFp;
			fList->AddItem(new PriorityItem(label, fp ? fp : ""));
		}
		if (fList->CountItems() == 0)
			fList->AddItem(new BStringItem(
				B_TRANSLATE("No priority contacts yet")));
	}

	BMessenger fTarget;
	BListView* fList;
	BButton* fRemoveBtn;
};


// --- PinPromptWindow -------------------------------------------------------
// Richiesta PIN sincrona e modale, sul modello di BAlert::Go(): il thread di
// invio la mostra e si blocca finche' l'utente non conferma o annulla. Serve a
// inviare verso un peer protetto da PIN (che risponde 401 al prepare-upload).
// Ritorna il PIN inserito, o "" se annullata.
class PinPromptWindow : public BWindow {
public:
	PinPromptWindow(const char* peerName)
		:
		BWindow(BRect(0, 0, 320, 130), B_TRANSLATE("PIN required"),
			B_MODAL_WINDOW,
			B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS
				| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
		fSem(create_sem(0, "pin_prompt")),
		fOut(NULL),
		fReleased(false)
	{
		BString prompt(B_TRANSLATE("\"%peer%\" requires a PIN:"));
		prompt.ReplaceFirst("%peer%",
			peerName && *peerName ? peerName : "?");
		BStringView* label = new BStringView("label", prompt.String());
		fField = new BTextControl("pin", NULL, "", NULL);
		BButton* ok = new BButton(B_TRANSLATE("Send"), new BMessage(kPinOk));
		BButton* cancel = new BButton(B_TRANSLATE("Cancel"),
			new BMessage(B_QUIT_REQUESTED));
		ok->MakeDefault(true);

		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(label)
			.Add(fField)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.AddGlue()
				.Add(cancel)
				.Add(ok)
			.End()
		.End();
	}

	// Chiamata dal thread di invio: mostra la finestra e attende la risposta.
	std::string Go()
	{
		std::string result;
		fOut = &result;
		sem_id sem = fSem; // copia locale: la finestra non tocca il sem dopo
		                   // il rilascio, quindi possiamo eliminarlo qui.
		CenterOnScreen();
		Show();
		while (acquire_sem(sem) == B_INTERRUPTED)
			;
		delete_sem(sem);
		return result;
	}

	virtual void MessageReceived(BMessage* msg)
	{
		if (msg->what == kPinOk) {
			if (fOut != NULL)
				*fOut = fField->Text();
			_Release();
			Quit(); // bypassa QuitRequested: _Release e' gia' avvenuto
			return;
		}
		BWindow::MessageReceived(msg);
	}

	virtual bool QuitRequested()
	{
		// Annulla / Esc / chiusura: risultato vuoto, sblocca comunque Go().
		_Release();
		return true;
	}

private:
	static const uint32 kPinOk = 'PnOK';

	void _Release()
	{
		if (!fReleased) {
			fReleased = true;
			release_sem(fSem);
		}
	}

	sem_id fSem;
	std::string* fOut;
	bool fReleased;
	BTextControl* fField;
};


// Invia con ritento sul PIN: se il peer risponde 401 al prepare-upload, chiede
// il PIN all'utente (modale) e riprova una volta. Il PIN vuoto = annullato.
static SendReport
SendWithPinRetry(UploadSession& session, const std::string& host, int port,
	const std::vector<FileMetadata>& files, const char* peerName,
	UploadSession::ProgressFn progress = nullptr)
{
	SendReport r = session.Send(host, port, files, "", progress);
	if (r.prepareStatus == 401) {
		PinPromptWindow* w = new PinPromptWindow(peerName);
		std::string pin = w->Go();
		if (!pin.empty())
			r = session.Send(host, port, files, pin, progress);
	}
	return r;
}


// --- DropZoneView ----------------------------------------------------------
// Banda tratteggiata sotto la lista: invita a trascinare i file. Un drop qui
// invia al dispositivo selezionato (in alternativa i file si possono lasciare
// direttamente su una riga, gestito dalla DeviceListView).
class DropZoneView : public BView {
public:
	DropZoneView()
		:
		BView("dropzone", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(B_SIZE_UNSET, 46));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 60));
	}

	virtual void Draw(BRect)
	{
		BRect b = Bounds();
		b.InsetBy(3, 3);
		// Bordo tratteggiato arrotondato.
		static const pattern kDash = { { 0xf0, 0xf0, 0x0f, 0x0f,
			0xf0, 0xf0, 0x0f, 0x0f } };
		SetLowColor(ViewColor());
		SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
			B_DARKEN_2_TINT));
		StrokeRoundRect(b, 8, 8, kDash);
		// Testo d'invito centrato, attenuato.
		SetHighColor(tint_color(ui_color(B_PANEL_TEXT_COLOR), 0.55));
		BFont f(be_plain_font);
		SetFont(&f);
		const char* hint = B_TRANSLATE(
			"Drag files here, or straight onto a device");
		float w = StringWidth(hint);
		font_height fh;
		f.GetHeight(&fh);
		DrawString(hint, BPoint(b.left + (b.Width() - w) / 2,
			b.top + (b.Height() + fh.ascent - fh.descent) / 2));
	}

	virtual void MessageReceived(BMessage* msg)
	{
		// Drop sulla banda: invia al dispositivo selezionato riusando il
		// percorso di kMsgFileSelected della finestra.
		if (msg->WasDropped() && msg->HasRef("refs")) {
			BMessage fwd(kMsgFileSelected);
			entry_ref ref;
			for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++)
				fwd.AddRef("refs", &ref);
			if (Window() != nullptr)
				Window()->PostMessage(&fwd);
			return;
		}
		BView::MessageReceived(msg);
	}
};


// --- MainWindow ------------------------------------------------------------

class MainWindow : public BWindow {
public:
	MainWindow(DeviceInfo* info, AppSettings* settings);
	virtual ~MainWindow();

	virtual void MessageReceived(BMessage* msg);
	virtual bool QuitRequested();

	void StartServer(void* sslCtx);
	void StopServer();

	void AddDevice(const DiscoveredDevice& dev);
	void HandleIncoming(IncomingRequest* req);
	void SetStatus(const char* text);
	void AddPendingFiles(const std::vector<std::string>& paths);
	// Pubblica file in bacheca: avvia il download server se fermo,
	// altrimenti accoda (dedupe per path) e notifica il cambio.
	void AddToBoard(const std::vector<std::string>& paths);

	// Permette al prossimo QuitRequested di chiudere davvero (saltando
	// il pattern hide-on-close usato quando il replicant Deskbar e' presente).
	void SetAllowQuit(bool v) { fAllowQuit = v; }

private:
	void SendToSelected();
	void SendPendingOrBrowse();
	void SendText(const std::string& host, int port,
		const std::string& text);
	void SendFiles(const std::string& host, int port,
		const std::vector<std::string>& paths);
	void SendToCircle(const std::vector<std::string>& paths);
	// showLinkUi: con true (Share via link) mostra la finestra QR e copia
	// il link negli appunti; con false (drop in bacheca) parte in silenzio.
	void StartDownloadServer(const std::vector<std::string>& files,
		bool showLinkUi = true);
	void StopDownloadServer();
	// Serve i byte di un file della bacheca (indice in fSharedFiles).
	// Chiamato dagli handler del download server (thread server).
	HttpServerResponse _ServeSharedFile(int id);
	// Filtro "bacheca solo ai preferiti" per le route del download server
	// (thread server): fingerprint in query o IP di un preferito online.
	bool _BoardAccessAllowed(const HttpRequest& req);
	// Esito d'accesso alle route del download server come codice HTTP:
	// 0 = ammesso, 401 = PIN richiesto/errato, 403 = rifiutato. Il PIN (se
	// impostato) e' una via d'accesso ALTERNATIVA all'identita', cosi' i peer
	// gia' autorizzati (auto-sync bacheca) non lo devono conoscere.
	int _BoardGate(const HttpRequest& req);
	// Manda lo snapshot completo (mia bacheca + bacheche remote) alla
	// BoardWindow, se aperta (UI-thread).
	void _PushBoardData();
	// Dopo ogni mutazione della bacheca (UI-thread): bump di fBoardRev
	// e notifica all'app (annuncio multicast + replicant iscritti).
	void _BoardChanged();
	// Posta all'app il conteggio peer online / preferiti online (UI-thread).
	void _PostPeerStats();
	// Manda lo snapshot dei contatti prioritari alla finestra di gestione,
	// se aperta (UI-thread).
	void _PushPriorityData();
	// Rimuove peer non sentiti da kDeviceTimeoutSeconds (UI-thread).
	void PruneStaleDevices();
	// Snapshot thread-safe del nostro DeviceInfo come JSON. Serve ai lettori
	// off-thread (route del server, register-back): l'alias e' una
	// std::string mutata sul looper (kMsgSettingsSave), leggerla dal thread
	// del server senza lock sarebbe una data race. boardRev/download restano
	// senza lock di proposito (word-sized, torn read benigno).
	std::string _MyInfoJson();

	DeviceInfo* fInfo;
	std::mutex fInfoMtx; // protegge l'alias di fInfo (lettori off-thread)
	AppSettings* fSettings;

	HeaderView* fHeader;
	BStatusBar* fProgressBar;
	BListView* fDeviceList;
	BFilePanel* fFilePanel;

	// Barra di stato in basso: conteggio dispositivi + IP locale.
	BStringView* fStatusLeft = nullptr;
	BStringView* fStatusRight = nullptr;
	void _UpdateDeviceCount();
	BString _LocalIpv4();

	// Server in background.
	SocketHttpServer fServer;
	ReceiveSession fSession;
	FileSink fSink;
	std::thread fServerThread;

	// Progresso ricezione.
	int fRecvTotal = 0;
	int fRecvDone = 0;

	// Anti-flood prepare-upload (stato toccato solo dal thread del server).
	time_t fPrepWindowStart = 0;
	int fPrepCount = 0;

	// Cronologia e preferiti.
	TransferHistory fHistory;
	Favorites fFavorites;
	PriorityContacts fPriority;
	std::string fLastSenderAlias;
	std::string fLastSenderFingerprint;

	// File in attesa (da argomento CLI o drag&drop prima della selezione).
	std::vector<std::string> fPendingPaths;

	// Download API (L5): server HTTP plain per condivisione via browser.
	// Da Fase 0 e' la "bacheca": fSharedFiles muta a runtime (AddToBoard)
	// mentre il thread del server la legge, quindi va protetta da mutex.
	SocketHttpServer fDownloadServer;
	std::thread fDownloadThread;
	std::mutex fSharedMtx;
	std::vector<std::string> fSharedFiles; // path dei file in bacheca
	bool fDownloadActive = false;
	int fBoardRev = 0; // revisione bacheca, annunciata via multicast

	// Bacheche remote (Fase 3): per fingerprint, solo UI-thread.
	std::map<std::string, RemoteBoard> fRemoteBoards;
	// Registro dei file gia' scaricati in auto-download.
	BoardSeen fBoardSeen;
	// Finestra Bacheca, se aperta (invalida quando l'utente la chiude).
	BMessenger fBoardWinMsgr;
	// Finestra di gestione contatti prioritari, se aperta.
	BMessenger fPriorityWinMsgr;
	// Finestra Impostazioni, se aperta (singola istanza: invalida alla chiusura).
	BMessenger fSettingsWinMsgr;

	// Dispositivi scoperti.
	std::mutex fDevicesMtx;
	std::vector<DiscoveredDevice> fDevices;

	// True quando l'app sta uscendo davvero (es. da "Quit" del replicant):
	// QuitRequested deve chiudere invece di nascondere la finestra.
	bool fAllowQuit = false;

	// Timer periodico: posta kMsgPruneDevices ogni kDevicePruneIntervalSeconds.
	BMessageRunner* fPruneRunner = nullptr;
	BMessageRunner* fProbeRunner = nullptr; // sonda HTTP periodica dei peer
};


MainWindow::MainWindow(DeviceInfo* info, AppSettings* settings)
	:
	BWindow(BRect(100, 100, 590, 480), "LocalSend",
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_QUIT_ON_WINDOW_CLOSE
			| B_AUTO_UPDATE_SIZE_LIMITS),
	fInfo(info),
	fSettings(settings),
	fFilePanel(nullptr),
	fSink(settings->destDir)
{
	fSink.EnsureDir();
	fHistory.Load();
	fFavorites.Load();
	fPriority.Load();
	fBoardSeen.Load();

	// Header.
	fHeader = new HeaderView();
	fHeader->SetDeviceName(info->alias.c_str());
	fHeader->SetFingerprint(info->fingerprint.c_str());
	fHeader->SetStatus(B_TRANSLATE("Ready to receive"), true, false);

	// Lista dispositivi con scroll.
	fDeviceList = new DeviceListView();
	fDeviceList->SetInvocationMessage(new BMessage(kMsgDeviceInvoked));
	BScrollView* scroll = new BScrollView("scroll", fDeviceList,
		0, false, true, B_NO_BORDER);

	// Barra di progresso (nascosta finche' non c'e' un trasferimento).
	fProgressBar = new BStatusBar("progress");
	fProgressBar->SetBarHeight(10);
	fProgressBar->SetMaxValue(1.0);
	fProgressBar->Hide();

	// Barra dei menu: le azioni globali vivono qui, non piu' come file di
	// pulsanti sotto la lista ("la lista e' l'interfaccia").
	BMenuBar* menuBar = new BMenuBar("menubar");

	BMenu* sendMenu = new BMenu(B_TRANSLATE("Send"));
	sendMenu->AddItem(new BMenuItem(B_TRANSLATE("Send file…"),
		new BMessage(kMsgSendFile)));
	sendMenu->AddItem(new BMenuItem(B_TRANSLATE("Send text…"),
		new BMessage(kMsgSendText)));
	sendMenu->AddItem(new BMenuItem(B_TRANSLATE("Send to circle…"),
		new BMessage(kMsgSendToCircle)));
	sendMenu->AddSeparatorItem();
	sendMenu->AddItem(new BMenuItem(B_TRANSLATE("Share via link…"),
		new BMessage(kMsgShareLink)));
	menuBar->AddItem(sendMenu);

	BMenu* netMenu = new BMenu(B_TRANSLATE("Network"));
	netMenu->AddItem(new BMenuItem(B_TRANSLATE("Refresh"),
		new BMessage(kMsgRefreshDevices)));
	netMenu->AddSeparatorItem();
	netMenu->AddItem(new BMenuItem(B_TRANSLATE("Add or remove favorite"),
		new BMessage(kMsgToggleFavorite)));
	menuBar->AddItem(netMenu);

	BMenu* toolsMenu = new BMenu(B_TRANSLATE("Tools"));
	toolsMenu->AddItem(new BMenuItem(B_TRANSLATE("History"),
		new BMessage(kMsgShowHistory)));
	toolsMenu->AddItem(new BMenuItem(B_TRANSLATE("Board"),
		new BMessage(kMsgShowBoard)));
	menuBar->AddItem(toolsMenu);

	BMenu* setMenu = new BMenu(B_TRANSLATE("Settings"));
	setMenu->AddItem(new BMenuItem(B_TRANSLATE("Settings…"),
		new BMessage(kMsgShowSettings)));
	setMenu->AddSeparatorItem();
	setMenu->AddItem(new BMenuItem(B_TRANSLATE("About"),
		new BMessage(kMsgAbout)));
	menuBar->AddItem(setMenu);

	sendMenu->SetTargetForItems(this);
	netMenu->SetTargetForItems(this);
	toolsMenu->SetTargetForItems(this);
	setMenu->SetTargetForItems(this);

	// Barra di stato in basso: dispositivi in rete (sinistra) + IP locale
	// (destra), in carattere piccolo e attenuato.
	fStatusLeft = new BStringView("stleft", "");
	fStatusRight = new BStringView("stright", _LocalIpv4().String());
	BFont smallFont(be_plain_font);
	smallFont.SetSize(be_plain_font->Size() - 1);
	rgb_color muted = tint_color(ui_color(B_PANEL_TEXT_COLOR), 0.6);
	fStatusLeft->SetFont(&smallFont);
	fStatusRight->SetFont(&smallFont);
	fStatusLeft->SetHighColor(muted);
	fStatusRight->SetHighColor(muted);
	fStatusRight->SetAlignment(B_ALIGN_RIGHT);

	// Banda tratteggiata "trascina qui" sotto la lista.
	DropZoneView* dropZone = new DropZoneView();

	// Layout: menu bar -> header -> lista/progresso -> drop-zone -> stato.
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(fHeader)
		.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(scroll, 1.0)
			.Add(fProgressBar)
		.End()
		.AddGroup(B_HORIZONTAL, 0)
			.SetInsets(B_USE_WINDOW_INSETS, 0, B_USE_WINDOW_INSETS,
				B_USE_HALF_ITEM_SPACING)
			.Add(dropZone)
		.End()
		.Add(new BSeparatorView(B_HORIZONTAL))
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS, B_USE_HALF_ITEM_SPACING,
				B_USE_WINDOW_INSETS, B_USE_HALF_ITEM_SPACING)
			.Add(fStatusLeft)
			.AddGlue()
			.Add(fStatusRight)
		.End()
	.End();

	_UpdateDeviceCount();

	SetSizeLimits(380, B_SIZE_UNLIMITED, 300, B_SIZE_UNLIMITED);
	CenterOnScreen();

	// Pruning periodico per il TTL "last-seen": ogni kDevicePruneIntervalSeconds
	// la window riceve kMsgPruneDevices e rimuove i peer scaduti.
	BMessage prune(kMsgPruneDevices);
	fPruneRunner = new BMessageRunner(BMessenger(this), &prune,
		(bigtime_t)kDevicePruneIntervalSeconds * 1000000LL);

	// Sonda periodica dei peer noti via HTTP (piu' frequente del TTL): li
	// tiene vivi quando il multicast in ingresso non arriva.
	BMessage probe(kMsgProbePeers);
	fProbeRunner = new BMessageRunner(BMessenger(this), &probe,
		(bigtime_t)kPeerProbeIntervalSeconds * 1000000LL);
}


MainWindow::~MainWindow()
{
	delete fPruneRunner;
	delete fProbeRunner;
	StopServer();
	delete fFilePanel;
}


void
MainWindow::SetStatus(const char* text)
{
	if (LockLooper()) {
		fHeader->SetStatus(text, false, false);
		UnlockLooper();
	}
}


// IPv4 della LAN via "trucco" del socket UDP connesso al gruppo multicast:
// il kernel sceglie l'IP sorgente giusto senza getifaddrs (assente su Haiku).
BString
MainWindow::_LocalIpv4()
{
	BString host;
	int probe = socket(AF_INET, SOCK_DGRAM, 0);
	if (probe >= 0) {
		sockaddr_in dest{};
		dest.sin_family = AF_INET;
		dest.sin_addr.s_addr = inet_addr(kMulticastGroup);
		dest.sin_port = htons(kDefaultPort);
		if (connect(probe, (sockaddr*)&dest, sizeof(dest)) == 0) {
			sockaddr_in local{};
			socklen_t len = sizeof(local);
			if (getsockname(probe, (sockaddr*)&local, &len) == 0
					&& local.sin_addr.s_addr != 0) {
				char buf[INET_ADDRSTRLEN] = {0};
				if (inet_ntop(AF_INET, &local.sin_addr, buf,
						sizeof(buf)) != nullptr)
					host = buf;
			}
		}
		close(probe);
	}
	return host;
}


// Aggiorna il conteggio "N dispositivi in rete" nella barra di stato.
void
MainWindow::_UpdateDeviceCount()
{
	if (fStatusLeft == nullptr)
		return;
	int32 n = fDeviceList->CountItems();
	char buf[64];
	snprintf(buf, sizeof(buf),
		n == 1 ? B_TRANSLATE("%d device on network")
			: B_TRANSLATE("%d devices on network"), (int)n);
	fStatusLeft->SetText(buf);
}


void
MainWindow::StartServer(void* sslCtx)
{
	fServer.EnableTls(sslCtx);

	// Progresso byte-level per la ricezione.
	fServer.SetBodyProgressFn([this](long long received, long long total) {
		if (total <= 0)
			return;
		BMessage prog(kMsgProgress);
		float pct = (float)received / (float)total;
		prog.AddFloat("value", pct);
		BString label;
		label << (int)(pct * 100) << "%";
		prog.AddString("label", label.String());
		PostMessage(&prog);
	});

	// Route: prepare-upload.
	fServer.Route("POST", kApiPrepareUpload, [this](const HttpRequest& req) {
		// Anti-flood: troppe prepare-upload ravvicinate -> 429.
		time_t now = time(nullptr);
		if (now - fPrepWindowStart >= kPrepareRateWindowSeconds) {
			fPrepWindowStart = now;
			fPrepCount = 0;
		}
		if (++fPrepCount > kPrepareRateMax)
			return HttpServerResponse::Empty(429);

		// Verifica PIN se configurato.
		if (!fSettings->pin.empty()) {
			std::string pin = req.Query("pin");
			if (pin != fSettings->pin)
				return HttpServerResponse::Empty(401);
		}

		IncomingPrepareUpload in;
		try {
			in = ParsePrepareUploadRequest(req.body);
		} catch (...) {
			return HttpServerResponse::Empty(400);
		}

		// Auto-accept: quickSave accetta tutto, i favoriti accettano
		// se il flag autoAcceptFavorites (Impostazioni > Sicurezza)
		// e' attivo. Cosi' l'utente puo' disabilitare il salta-dialog
		// per i preferiti senza dover rimuoverli dall'elenco.
		bool autoAccept = fSettings->quickSave;
		if (!autoAccept && fSettings->autoAcceptFavorites
				&& fFavorites.Contains(in.sender.fingerprint))
			autoAccept = true;

		if (!autoAccept) {
			IncomingRequest incoming;
			incoming.data = in;

			BMessage msg(kMsgIncoming);
			msg.AddPointer("request", &incoming);
			PostMessage(&msg);

			std::unique_lock<std::mutex> lock(incoming.mtx);
			incoming.cv.wait(lock, [&] { return incoming.answered; });

			if (!incoming.accepted)
				return HttpServerResponse::Empty(403);
		}

		PrepareOutcome out = fSession.Prepare(in,
			ReceiveSession::AcceptPolicy(), req.clientHost);
		switch (out.status) {
			case PrepareStatus::SessionBusy:
				return HttpServerResponse::Empty(409);
			case PrepareStatus::NothingAccepted:
				return HttpServerResponse::Empty(204);
			case PrepareStatus::Accepted:
				break;
		}

		// Salva il mittente per la cronologia e i favoriti.
		fLastSenderAlias = in.sender.alias;
		fLastSenderFingerprint = in.sender.fingerprint;

		// Inizializza progresso ricezione.
		fRecvTotal = static_cast<int>(out.result.fileTokens.size());
		fRecvDone = 0;
		{
			BMessage prog(kMsgProgress);
			prog.AddFloat("value", 0.0f);
			BString label;
			label << "0/" << (int32)fRecvTotal;
			prog.AddString("label", label.String());
			PostMessage(&prog);
		}

		return HttpServerResponse::Json(200,
			BuildPrepareUploadResponse(out.result.sessionId,
				out.result.fileTokens).Dump());
	});

	// Route: upload.
	fServer.Route("POST", kApiUpload, [this](const HttpRequest& req) {
		std::string sessionId = req.Query("sessionId");
		std::string fileId = req.Query("fileId");
		std::string token = req.Query("token");

		// Parametri obbligatori mancanti -> 400 (non 403).
		if (sessionId.empty() || fileId.empty() || token.empty())
			return HttpServerResponse::Empty(400);

		if (!fSession.ValidateUpload(sessionId, fileId, token,
				req.clientHost))
			return HttpServerResponse::Empty(403);

		const FileMetadata* meta = fSession.File(fileId);
		std::string name = meta ? meta->fileName : fileId;
		std::string mimeType = meta ? meta->fileType : "";

		// Verifica integrita': se il mittente ha dichiarato uno sha256, i byte
		// ricevuti devono corrispondervi, altrimenti scartiamo (niente file
		// corrotti su disco). Confronto case-insensitive; sha256 vuoto = skip.
		if (meta && !meta->sha256.empty()) {
			std::string want = meta->sha256;
			for (char& c : want) {
				if (c >= 'A' && c <= 'Z')
					c += 32;
			}
			if (Sha256Hex(req.body) != want) {
				fprintf(stderr, "sha256 non corrisponde per %s: scartato\n",
					name.c_str());
				return HttpServerResponse::Empty(500);
			}
		}

		std::string outPath, werr;
		if (!fSink.Save(name, req.body, mimeType, &outPath, &werr))
			return HttpServerResponse::Empty(500);

		fSession.MarkReceived(fileId);
		fRecvDone++;

		// Aggiorna barra di progresso.
		{
			BMessage prog(kMsgProgress);
			prog.AddFloat("value", fRecvTotal > 0
				? (float)fRecvDone / (float)fRecvTotal : 1.0f);
			BString label;
			label << (int32)fRecvDone << "/" << (int32)fRecvTotal;
			prog.AddString("label", label.String());
			PostMessage(&prog);
		}

		bool isTextMsg = (mimeType == "text/plain"
			&& req.body.size() < 10240);

		if (isTextMsg) {
			// Messaggio di testo: mostra solo il dialogo, niente
			// notifica "file ricevuto".
			BMessage tmsg(kMsgTextReceived);
			tmsg.AddString("text", req.body.c_str());
			tmsg.AddString("sender", fLastSenderAlias.c_str());
			PostMessage(&tmsg);
		} else {
			// File normale: notifica standard.
			BMessage msg(kMsgFileReceived);
			msg.AddString("name", name.c_str());
			msg.AddString("path", outPath.c_str());
			msg.AddInt64("size", static_cast<int64>(req.body.size()));
			PostMessage(&msg);
		}

		// Cronologia per entrambi.
		fHistory.Add(false, name, fLastSenderAlias,
			static_cast<long long>(req.body.size()));

		if (fSession.IsComplete()) {
			fSession.Reset();
			fRecvTotal = 0;
			fRecvDone = 0;
		}

		return HttpServerResponse::Empty(200);
	});

	// Route: cancel.
	fServer.Route("POST", kApiCancel, [this](const HttpRequest& req) {
		fSession.Cancel(req.Query("sessionId"));
		return HttpServerResponse::Empty(200);
	});

	// Route: info (GET). Ritorna il nostro DeviceInfo.
	auto infoHandler = [this](const HttpRequest&) {
		return HttpServerResponse::Json(200, _MyInfoJson());
	};
	fServer.Route("GET", kApiInfo, infoHandler);
	fServer.Route("GET", "/api/localsend/v1/info", infoHandler);

	// Route: register (POST). Fondamentale per la scoperta bidirezionale:
	// molti client (Android, Windows recenti) non annunciano periodicamente
	// via multicast — dopo aver sentito UN nostro annuncio ci contattano
	// direttamente qui col loro DeviceInfo. Il body contiene i loro dati,
	// l'IP lo prendiamo dal socket (req.clientHost). Se non lo aggiungiamo
	// da qui il peer resta invisibile nella nostra lista, pur potendo
	// mandarci file lui.
	fServer.Route("POST", kApiRegister, [this](const HttpRequest& req) {
		try {
			JsonValue msg = JsonValue::Parse(req.body);
			if (msg.Has("alias") && msg.Has("fingerprint")
					&& !req.clientHost.empty()) {
				std::string fp = msg.At("fingerprint").AsString();
				if (fp != fInfo->fingerprint) {
					DiscoveredDevice dev;
					dev.alias = msg.At("alias").AsString();
					dev.fingerprint = fp;
					dev.host = req.clientHost;
					dev.port = msg.Has("port")
						? static_cast<int>(msg.At("port").AsInt(kDefaultPort))
						: kDefaultPort;
					dev.deviceType = msg.Has("deviceType")
						? msg.At("deviceType").AsString()
						: std::string("unknown");
					// Riconoscimento Haiku (marcatore "app" o deviceModel).
					std::string app = msg.Has("app")
						? msg.At("app").AsString() : std::string();
					std::string model = msg.Has("deviceModel")
						? msg.At("deviceModel").AsString() : std::string();
					dev.deviceModel = model;
					dev.isHaiku = (app == kAppId) || (model == "Haiku");
					AddDevice(dev);
				}
			}
		} catch (...) {
			// Body non-JSON: rispondiamo comunque con le nostre info.
		}
		return HttpServerResponse::Json(200, _MyInfoJson());
	});

	if (!fServer.Start(fInfo->port)) {
		BAlert* alert = new BAlert("Errore",
			B_TRANSLATE("Cannot open port (already in use?)"),
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	fServerThread = std::thread([this]() { fServer.Run(); });
}


void
MainWindow::StopServer()
{
	fServer.Stop();
	if (fServerThread.joinable())
		fServerThread.join();
}


void
MainWindow::AddDevice(const DiscoveredDevice& dev)
{
	// Chiamato dal thread del MulticastAnnouncer per ogni peer sentito
	// (annuncio o reply). Se gia' presente aggiorniamo solo lastSeen
	// per evitare che il pruning per TTL lo espella; se nuovo postiamo
	// il messaggio alla UI per crearne l'item nella BListView.
	DiscoveredDevice copy = dev;
	copy.lastSeen = time(nullptr);

	bool isNew = true;
	bool boardChanged = false;
	{
		std::lock_guard<std::mutex> lock(fDevicesMtx);
		for (auto& d : fDevices) {
			if (d.fingerprint == copy.fingerprint) {
				d.lastSeen = copy.lastSeen;
				// Host puo' cambiare (DHCP): tienilo aggiornato.
				d.host = copy.host;
				d.port = copy.port;
				if (d.boardRev != copy.boardRev) {
					d.boardRev = copy.boardRev;
					boardChanged = true;
				}
				isNew = false;
				break;
			}
		}
		if (isNew) {
			fDevices.push_back(copy);
			boardChanged = copy.boardRev > 0;
		}
	}

	// Bacheca del peer cambiata (o peer nuovo con bacheca attiva): la
	// window fara' il fetch della lista sul proprio thread.
	if (boardChanged) {
		BMessage bm(kMsgPeerBoardChanged);
		bm.AddString("alias", copy.alias.c_str());
		bm.AddString("host", copy.host.c_str());
		bm.AddString("fingerprint", copy.fingerprint.c_str());
		bm.AddInt32("rev", copy.boardRev);
		PostMessage(&bm);
	}

	if (!isNew)
		return;

	BMessage msg(kMsgDeviceFound);
	msg.AddString("alias", copy.alias.c_str());
	msg.AddString("type", copy.deviceType.c_str());
	msg.AddString("model", copy.deviceModel.c_str());
	msg.AddString("ip", copy.host.c_str());
	msg.AddString("fingerprint", copy.fingerprint.c_str());
	msg.AddBool("haiku", copy.isHaiku);
	PostMessage(&msg);

	// Registrazione reciproca: appena scopriamo un peer, gli POSTiamo il
	// nostro DeviceInfo su /api/localsend/v2/register. Cosi' anche i client
	// che ascoltano soltanto (mai un annuncio multicast) ci vedono subito
	// nella loro lista, senza aspettare l'inversione di flusso. Fire &
	// forget in un thread detached: la POST puo' bloccare qualche secondo
	// su TLS handshake e non deve congelare la UI.
	std::string host = copy.host;
	int port = copy.port;
	std::string myInfo = _MyInfoJson();
	std::thread([host, port, myInfo]() {
		SocketHttpClient client;
		client.EnableTls();
		client.Post(host, port, kApiRegister,
			"application/json", myInfo);
	}).detach();
}


void
MainWindow::PruneStaleDevices()
{
	// Esegue il TTL: rimuove dalla lista interna e dalla BListView i peer
	// non sentiti da kDeviceTimeoutSeconds. Eseguito dal looper della
	// window (post kMsgPruneDevices da MessageRunner), quindi UI-thread.
	time_t cutoff = time(nullptr) - kDeviceTimeoutSeconds;
	std::vector<std::string> dropped;
	{
		std::lock_guard<std::mutex> lock(fDevicesMtx);
		for (auto it = fDevices.begin(); it != fDevices.end();) {
			if (it->lastSeen != 0 && it->lastSeen < cutoff) {
				dropped.push_back(it->fingerprint);
				it = fDevices.erase(it);
			} else {
				++it;
			}
		}
	}
	if (dropped.empty())
		return;

	// Rimuovi gli item corrispondenti dalla BListView (chiave: fingerprint).
	for (const auto& fp : dropped) {
		for (int32 i = fDeviceList->CountItems() - 1; i >= 0; i--) {
			DeviceListItem* item
				= dynamic_cast<DeviceListItem*>(fDeviceList->ItemAt(i));
			if (item != nullptr && item->FingerprintId() == fp.c_str()) {
				fDeviceList->RemoveItem(i);
				delete item;
				break;
			}
		}
	}
	_PostPeerStats();
	_UpdateDeviceCount();
}


std::string
MainWindow::_MyInfoJson()
{
	std::lock_guard<std::mutex> lock(fInfoMtx);
	return fInfo->ToJson().Dump();
}


void
MainWindow::HandleIncoming(IncomingRequest* req)
{
	BString text(B_TRANSLATE("From: "));
	text << req->data.sender.alias.c_str() << "\n\n";

	long long totalSize = 0;
	for (const auto& f : req->data.files) {
		text << "  \xE2\x80\xA2 " << f.fileName.c_str();
		if (f.size >= 1024 * 1024)
			text << " (" << (int)(f.size / (1024 * 1024)) << " MB)";
		else
			text << " (" << (int)(f.size / 1024) << " KB)";
		text << "\n";
		totalSize += f.size;
	}

	if (req->data.files.size() > 1) {
		text << "\nTotale: ";
		if (totalSize >= 1024 * 1024)
			text << (int)(totalSize / (1024 * 1024)) << " MB";
		else
			text << (int)(totalSize / 1024) << " KB";
	}

	BAlert* alert = new BAlert(B_TRANSLATE("Incoming transfer"),
		text.String(), B_TRANSLATE("Accept"), B_TRANSLATE("Reject"), NULL,
		B_WIDTH_AS_USUAL, B_INFO_ALERT);
	alert->SetShortcut(1, B_ESCAPE);
	int32 result = alert->Go();

	std::lock_guard<std::mutex> lock(req->mtx);
	req->accepted = (result == 0);
	req->answered = true;
	req->cv.notify_one();
}


void
MainWindow::MessageReceived(BMessage* msg)
{
	// Drag & drop dal Tracker: stessa logica di kMsgFileSelected.
	if (msg->WasDropped()) {
		entry_ref ref;
		if (msg->FindRef("refs", &ref) == B_OK) {
			std::vector<std::string> paths;
			for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					paths.push_back(path.Path());
			}
			if (!paths.empty()) {
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				// Bersaglio: la riga SOTTO il punto di rilascio se il drop e'
				// su un dispositivo, altrimenti quello selezionato.
				int32 target = fDeviceList->CurrentSelection();
				BPoint drop;
				if (msg->FindPoint("_drop_point_", &drop) == B_OK) {
					int32 idx = fDeviceList->IndexOf(
						fDeviceList->ConvertFromScreen(drop));
					if (idx >= 0) {
						target = idx;
						fDeviceList->Select(idx); // feedback visivo
					}
				}
				if (target < 0 || target >= (int32)fDevices.size()) {
					BAlert* alert = new BAlert("LocalSend",
						B_TRANSLATE("Drop the files on a device, or select "
							"one first."), B_TRANSLATE("OK"),
						NULL, NULL, B_WIDTH_AS_USUAL,
						B_INFO_ALERT);
					alert->Go();
				} else {
					auto& dev = fDevices[target];
					SendFiles(dev.host, dev.port, paths);
				}
			}
			return;
		}
	}

	switch (msg->what) {
		case kMsgSendFile:
		case kMsgDeviceInvoked:
			SendToSelected();
			break;

		case kMsgPeerHeard:
		{
			// Peer sentito dal faro (inoltrato dall'app): ricostruisci il
			// device ed elabora la scoperta sul looper della window.
			DiscoveredDevice dev;
			const char* s = nullptr;
			if (msg->FindString("alias", &s) == B_OK) dev.alias = s;
			if (msg->FindString("host", &s) == B_OK) dev.host = s;
			if (msg->FindString("deviceType", &s) == B_OK) dev.deviceType = s;
			if (msg->FindString("deviceModel", &s) == B_OK) dev.deviceModel = s;
			if (msg->FindString("fingerprint", &s) == B_OK)
				dev.fingerprint = s;
			int32 port = kDefaultPort;
			msg->FindInt32("port", &port);
			dev.port = port;
			int32 rev = 0;
			msg->FindInt32("boardRev", &rev);
			dev.boardRev = rev;
			bool haiku = false;
			msg->FindBool("haiku", &haiku);
			dev.isHaiku = haiku;
			if (!dev.fingerprint.empty())
				AddDevice(dev);
			break;
		}

		case kMsgPruneDevices:
			PruneStaleDevices();
			break;

		case kMsgProbePeers:
		{
			// Snapshot dei peer noti (host/porta) sotto lock.
			struct Probe { std::string host; int port; };
			std::vector<Probe> peers;
			{
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				for (const auto& d : fDevices)
					peers.push_back({d.host, d.port});
			}
			if (peers.empty())
				break;
			// Thread detached: GET /info a ciascun peer; se risponde,
			// rinfrescalo via kMsgPeerHeard (il percorso di scoperta
			// esistente, elaborato sul looper). Il BMessenger diventa
			// invalido se la window muore -> SendMessage fallisce senza
			// crash. Cosi' i peer restano vivi anche senza multicast in
			// ingresso, finche' il loro server HTTP e' raggiungibile.
			BMessenger msgr(this);
			std::thread([msgr, peers]() {
				for (const auto& p : peers) {
					SocketHttpClient client;
					client.EnableTls();
					HttpResponse r = client.Get(p.host, p.port, kApiInfo);
					if (!r.IsOk())
						continue;
					try {
						DeviceInfo info = DeviceInfo::FromJson(
							JsonValue::Parse(r.body));
						if (info.fingerprint.empty())
							continue;
						BMessage m(kMsgPeerHeard);
						m.AddString("alias", info.alias.c_str());
						m.AddString("host", p.host.c_str());
						m.AddInt32("port", p.port);
						m.AddString("deviceType", info.deviceType.c_str());
						m.AddString("deviceModel",
							info.deviceModel.c_str());
						m.AddString("fingerprint",
							info.fingerprint.c_str());
						m.AddBool("haiku", info.app == kAppId
							|| info.deviceModel == "Haiku");
						if (msgr.IsValid())
							msgr.SendMessage(&m);
					} catch (...) {
					}
				}
			}).detach();
			break;
		}

		case kMsgRefreshDevices:
			// Inoltra all'app: l'announcer e' di sua proprieta'.
			be_app->PostMessage(kMsgTriggerBurst);
			break;

		case kMsgFileSelected:
		{
			entry_ref ref;
			std::vector<std::string> paths;
			for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					paths.push_back(path.Path());
			}
			if (!paths.empty()) {
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				int32 sel = fDeviceList->CurrentSelection();
				if (sel >= 0 && sel < (int32)fDevices.size()) {
					auto& dev = fDevices[sel];
					SendFiles(dev.host, dev.port, paths);
				}
			}
			break;
		}

		case kMsgDropOnDevice:
		{
			// File lasciati su una riga specifica (dalla DeviceListView):
			// invia al dispositivo di quell'indice.
			int32 idx = -1;
			msg->FindInt32("deviceIndex", &idx);
			entry_ref ref;
			std::vector<std::string> paths;
			for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					paths.push_back(path.Path());
			}
			if (!paths.empty()) {
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				if (idx >= 0 && idx < (int32)fDevices.size()) {
					auto& dev = fDevices[idx];
					SendFiles(dev.host, dev.port, paths);
				}
			}
			break;
		}

		case kMsgIncoming:
		{
			IncomingRequest* req = nullptr;
			msg->FindPointer("request", (void**)&req);
			if (req)
				HandleIncoming(req);
			break;
		}

		case kMsgFileReceived:
		{
			const char* name = nullptr;
			msg->FindString("name", &name);
			if (name) {
				BString status(B_TRANSLATE("Received: "));
				status << name;
				fHeader->SetStatus(status.String(), true, false);

				BNotification notif(B_INFORMATION_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(B_TRANSLATE("File received"));
				notif.SetContent(name);
				notif.Send();

				// Nascondi la barra se la ricezione e' completa.
				if (fRecvTotal > 0 && fRecvDone >= fRecvTotal
					&& !fProgressBar->IsHidden())
					fProgressBar->Hide();
			}
			break;
		}

		case kMsgDeviceFound:
		{
			const char* alias = nullptr;
			const char* type = nullptr;
			const char* model = nullptr;
			const char* ip = nullptr;
			const char* fp = nullptr;
			msg->FindString("alias", &alias);
			msg->FindString("type", &type);
			msg->FindString("model", &model);
			msg->FindString("ip", &ip);
			msg->FindString("fingerprint", &fp);
			if (alias) {
				bool fav = fp ? fFavorites.Contains(fp) : false;
				bool prio = fp ? fPriority.Contains(fp) : false;
				bool haiku = false;
				msg->FindBool("haiku", &haiku);
				fDeviceList->AddItem(new DeviceListItem(
					alias, type ? type : "", model ? model : "",
					ip ? ip : "", fp ? fp : "", fav, prio, haiku));
				_PostPeerStats();
				_UpdateDeviceCount();
			}
			break;
		}

		case kMsgShareLink:
		{
			if (fDownloadActive) {
				StopDownloadServer();
				fHeader->SetStatus(B_TRANSLATE("Ready to receive"), true, false);
			} else {
				// Apri il file picker per scegliere i file da condividere.
				if (!fFilePanel) {
					fFilePanel = new BFilePanel(B_OPEN_PANEL,
						new BMessenger(this), NULL, B_FILE_NODE,
						true, new BMessage(kMsgShareLink));
					fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON,
						B_TRANSLATE("Share via link…"));
					fFilePanel->Window()->SetTitle(
						B_TRANSLATE("Choose files to send"));
				} else {
					fFilePanel->SetMessage(
						new BMessage(kMsgShareLink));
				}
				// Controlla se ci sono refs (dal file picker).
				entry_ref ref;
				if (msg->FindRef("refs", &ref) == B_OK) {
					std::vector<std::string> paths;
					for (int i = 0;
						msg->FindRef("refs", i, &ref) == B_OK;
						i++) {
						BPath path(&ref);
						if (path.InitCheck() == B_OK)
							paths.push_back(path.Path());
					}
					if (!paths.empty())
						StartDownloadServer(paths);
				} else {
					fFilePanel->Show();
				}
			}
			break;
		}

		case kMsgStopShare:
			StopDownloadServer();
			fHeader->SetStatus(B_TRANSLATE("Ready to receive"), true, false);
			break;

		case kMsgAbout:
		{
			BAlert* alert = new BAlert("About LocalSend",
				"LocalSend for Haiku v1.4.0\n\n"
				"Native LocalSend v2.1 client.\n"
				"Share files over LAN with any device.\n\n"
				"by atomozero\n"
				"https://github.com/atomozero/LocalSend\n\n"
				"This software may contain\n"
				"traces of peanuts and LLM.\n\n"
				"MIT License",
				B_TRANSLATE("OK"), NULL, NULL,
				B_WIDTH_AS_USUAL, B_INFO_ALERT);
			alert->Go();
			break;
		}

		case kMsgToggleFavorite:
		{
			// Scope interno: _PostPeerStats riprende fDevicesMtx, il lock
			// deve essere gia' rilasciato quando la chiamiamo.
			{
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				int32 sel = fDeviceList->CurrentSelection();
				if (sel >= 0 && sel < (int32)fDevices.size()) {
					auto& dev = fDevices[sel];
					DeviceListItem* item = dynamic_cast<DeviceListItem*>(
						fDeviceList->ItemAt(sel));
					if (item) {
						if (fFavorites.Contains(dev.fingerprint)) {
							fFavorites.Remove(dev.fingerprint);
							item->SetFavorite(false);
						} else {
							fFavorites.Add(dev.fingerprint);
							item->SetFavorite(true);
						}
						fDeviceList->InvalidateItem(sel);
					}
				}
			}
			_PostPeerStats();
			break;
		}

		case kMsgTogglePriority:
		{
			// Marca/smarca il dispositivo selezionato come contatto
			// prioritario, catturandone l'alias come "profilo".
			std::lock_guard<std::mutex> lock(fDevicesMtx);
			int32 sel = fDeviceList->CurrentSelection();
			if (sel >= 0 && sel < (int32)fDevices.size()) {
				auto& dev = fDevices[sel];
				DeviceListItem* item = dynamic_cast<DeviceListItem*>(
					fDeviceList->ItemAt(sel));
				if (item && !dev.fingerprint.empty()) {
					if (fPriority.Contains(dev.fingerprint)) {
						fPriority.Remove(dev.fingerprint);
						item->SetPriority(false);
					} else {
						fPriority.Add(dev.fingerprint, dev.alias);
						item->SetPriority(true);
					}
					fDeviceList->InvalidateItem(sel);
					_PushPriorityData();
				}
			}
			break;
		}

		case kMsgManagePriority:
		{
			if (fPriorityWinMsgr.IsValid()) {
				fPriorityWinMsgr.SendMessage(kMsgManagePriority);
			} else {
				PriorityContactsWindow* pw
					= new PriorityContactsWindow(BMessenger(this));
				fPriorityWinMsgr = BMessenger(pw);
				pw->Show();
				_PushPriorityData();
			}
			break;
		}

		case kMsgPriorityRemove:
		{
			// Dalla finestra di gestione: rimuovi un contatto per fingerprint.
			const char* fp = nullptr;
			msg->FindString("fingerprint", &fp);
			if (fp && *fp) {
				fPriority.Remove(fp);
				// Aggiorna il marcatore se il dispositivo e' in lista.
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				for (int32 i = 0; i < fDeviceList->CountItems(); i++) {
					DeviceListItem* it = dynamic_cast<DeviceListItem*>(
						fDeviceList->ItemAt(i));
					if (it && it->FingerprintId() == fp) {
						it->SetPriority(false);
						fDeviceList->InvalidateItem(i);
						break;
					}
				}
			}
			_PushPriorityData();
			break;
		}

		case kMsgProgress:
		{
			float value = 0;
			const char* label = nullptr;
			msg->FindFloat("value", &value);
			msg->FindString("label", &label);
			if (fProgressBar->IsHidden())
				fProgressBar->Show();
			fProgressBar->Reset();
			fProgressBar->SetMaxValue(1.0);
			fProgressBar->SetTo(value);
			if (label)
				fProgressBar->SetTrailingText(label);
			break;
		}

		case kMsgSendDone:
		{
			// "quiet": esito intermedio di un invio alla cerchia. Registra
			// solo la cronologia; status e barra li chiude kMsgCircleDone.
			bool quiet = false;
			msg->FindBool("quiet", &quiet);

			const char* status = nullptr;
			const char* checkSent = nullptr;
			msg->FindString("status", &status);
			bool ok = (msg->FindString("sent_file", &checkSent) == B_OK);
			if (!quiet) {
				if (status)
					fHeader->SetStatus(status, ok, !ok);
				if (!fProgressBar->IsHidden())
					fProgressBar->Hide();
			}

			// Cronologia: registra ogni file inviato.
			const char* peer = nullptr;
			int64 totalSize = 0;
			msg->FindString("peer", &peer);
			msg->FindInt64("total_size", &totalSize);
			const char* sentFile = nullptr;
			for (int i = 0;
				msg->FindString("sent_file", i, &sentFile) == B_OK;
				i++) {
				fHistory.Add(true, sentFile,
					peer ? peer : "?", totalSize);
			}
			break;
		}

		case kMsgSendToCircle:
		{
			// Dal file panel: refs presenti, invia subito.
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK) {
				std::vector<std::string> paths;
				for (int i = 0;
					msg->FindRef("refs", i, &ref) == B_OK; i++) {
					BPath path(&ref);
					if (path.InitCheck() == B_OK)
						paths.push_back(path.Path());
				}
				if (!paths.empty())
					SendToCircle(paths);
				break;
			}

			// File pendenti (da CLI o drag&drop): usali subito.
			if (!fPendingPaths.empty()) {
				std::vector<std::string> paths = fPendingPaths;
				fPendingPaths.clear();
				SendToCircle(paths);
				break;
			}

			// Altrimenti scegli i file: il panel riposta kMsgSendToCircle.
			if (!fFilePanel) {
				fFilePanel = new BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE,
					true, new BMessage(kMsgSendToCircle));
				fFilePanel->Window()->SetTitle(B_TRANSLATE("Choose files to send"));
			} else {
				fFilePanel->SetMessage(
					new BMessage(kMsgSendToCircle));
			}
			fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON,
				B_TRANSLATE("Send to circle…"));
			fFilePanel->Show();
			break;
		}

		case kMsgAddToBoard:
		{
			// Dall'app (drop sul replicant): pubblica i refs in bacheca.
			entry_ref ref;
			std::vector<std::string> paths;
			for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
				BPath path(&ref);
				if (path.InitCheck() == B_OK)
					paths.push_back(path.Path());
			}
			if (!paths.empty())
				AddToBoard(paths);
			break;
		}

		case kMsgBoardRemove:
		{
			// Dalla BoardWindow: togli un singolo file dalla bacheca.
			const char* path = nullptr;
			msg->FindString("path", &path);
			if (!path)
				break;
			bool removed = false;
			{
				std::lock_guard<std::mutex> lock(fSharedMtx);
				for (auto it = fSharedFiles.begin();
						it != fSharedFiles.end(); ++it) {
					if (*it == path) {
						fSharedFiles.erase(it);
						removed = true;
						break;
					}
				}
			}
			if (removed)
				_BoardChanged();
			break;
		}

		case kMsgShowBoard:
		{
			if (fBoardWinMsgr.IsValid()) {
				// Gia' aperta: portala in primo piano.
				fBoardWinMsgr.SendMessage(kMsgShowBoard);
			} else {
				BoardWindow* bw = new BoardWindow(BMessenger(this));
				fBoardWinMsgr = BMessenger(bw);
				bw->Show();
				_PushBoardData();
			}
			break;
		}

		case kMsgBoardPickFiles:
		{
			// "Aggiungi file..." dalla BoardWindow: il panel riposta
			// kMsgAddToBoard con i refs scelti.
			if (!fFilePanel) {
				fFilePanel = new BFilePanel(B_OPEN_PANEL,
					new BMessenger(this), NULL, B_FILE_NODE,
					true, new BMessage(kMsgAddToBoard));
				fFilePanel->Window()->SetTitle(B_TRANSLATE("Choose files to send"));
			} else {
				fFilePanel->SetMessage(new BMessage(kMsgAddToBoard));
			}
			fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Add files…"));
			fFilePanel->Show();
			break;
		}

		case kMsgPeerBoardChanged:
		{
			const char* alias = nullptr;
			const char* host = nullptr;
			const char* fp = nullptr;
			int32 rev = 0;
			msg->FindString("alias", &alias);
			msg->FindString("host", &host);
			msg->FindString("fingerprint", &fp);
			msg->FindInt32("rev", &rev);
			if (!fp)
				break;

			if (rev <= 0) {
				// Bacheca chiusa/svuotata dal peer.
				fRemoteBoards.erase(fp);
				_PushBoardData();
				break;
			}

			// Fetch della lista in un thread: prepare-download (HTTP
			// plain, porta 53318). Il fingerprint in query serve al
			// filtro "solo preferiti" del peer.
			std::string a = alias ? alias : "";
			std::string h = host ? host : "";
			std::string f = fp;
			std::string myFp = fInfo->fingerprint;
			std::thread([this, a, h, f, rev, myFp]() {
				SocketHttpClient http;
				std::string path = std::string(kApiPrepareDownload)
					+ "?fingerprint=" + UrlEncode(myFp);
				HttpResponse resp = http.Post(h, kDownloadPort, path,
					"application/json", "");

				BMessage out(kMsgRemoteBoardFetched);
				out.AddString("alias", a.c_str());
				out.AddString("host", h.c_str());
				out.AddString("fingerprint", f.c_str());
				out.AddInt32("rev", rev);
				bool ok = resp.IsOk();
				if (ok) {
					try {
						JsonValue root = JsonValue::Parse(resp.body);
						if (root.Has("files")) {
							for (const auto& kv
									: root.At("files").Items()) {
								FileMetadata fm
									= FileMetadata::FromJson(kv.second);
								std::string id = fm.id.empty()
									? kv.first : fm.id;
								out.AddString("fid", id.c_str());
								out.AddString("fname",
									fm.fileName.c_str());
								out.AddString("ftype",
									fm.fileType.c_str());
								out.AddInt64("fsize", fm.size);
							}
						}
					} catch (...) {
						ok = false;
					}
				}
				out.AddBool("ok", ok);
				PostMessage(&out);
			}).detach();
			break;
		}

		case kMsgRemoteBoardFetched:
		{
			bool ok = false;
			msg->FindBool("ok", &ok);
			const char* fp = nullptr;
			const char* alias = nullptr;
			const char* host = nullptr;
			int32 rev = 0;
			msg->FindString("fingerprint", &fp);
			msg->FindString("alias", &alias);
			msg->FindString("host", &host);
			msg->FindInt32("rev", &rev);
			if (!fp)
				break;

			if (!ok) {
				// Fetch fallito (server del peer non ancora su?):
				// azzera la rev nota cosi' il prossimo annuncio
				// periodico (max 5 s) fa scattare un nuovo tentativo.
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				for (auto& d : fDevices) {
					if (d.fingerprint == fp) {
						d.boardRev = -1;
						break;
					}
				}
				break;
			}

			RemoteBoard& rb = fRemoteBoards[fp];
			rb.alias = alias ? alias : "?";
			rb.host = host ? host : "";
			rb.fingerprint = fp;
			rb.rev = rev;
			rb.files.clear();
			for (int32 i = 0; ; i++) {
				const char* id = nullptr;
				if (msg->FindString("fid", i, &id) != B_OK)
					break;
				RemoteBoardFile rf;
				rf.id = id;
				const char* nm = nullptr;
				if (msg->FindString("fname", i, &nm) == B_OK && nm)
					rf.fileName = nm;
				const char* ty = nullptr;
				if (msg->FindString("ftype", i, &ty) == B_OK && ty)
					rf.fileType = ty;
				int64 sz = 0;
				msg->FindInt64("fsize", i, &sz);
				rf.size = sz;
				rb.files.push_back(rf);
			}
			_PushBoardData();

			if (!rb.files.empty()) {
				char buf[128];
				snprintf(buf, sizeof(buf), B_TRANSLATE("%d files on the board"),
					(int)rb.files.size());
				BNotification notif(B_INFORMATION_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(rb.alias.c_str());
				notif.SetContent(buf);
				notif.Send();
			}

			// Auto-download dai preferiti: solo i file non ancora visti.
			if (fSettings->autoDownloadBoard && fFavorites.Contains(fp)) {
				for (const auto& rf : rb.files) {
					if (fBoardSeen.Contains(BoardSeenKey(fp, rf.id,
							rf.fileName, rf.size)))
						continue;
					BMessage dl(kMsgBoardDownload);
					dl.AddString("host", rb.host.c_str());
					dl.AddString("alias", rb.alias.c_str());
					dl.AddString("fingerprint", fp);
					dl.AddString("fileId", rf.id.c_str());
					dl.AddString("fileName", rf.fileName.c_str());
					dl.AddInt64("size", rf.size);
					PostMessage(&dl);
				}
			}
			break;
		}

		case kMsgBoardDownload:
		{
			const char* host = nullptr;
			const char* alias = nullptr;
			const char* fp = nullptr;
			const char* fileId = nullptr;
			const char* fileName = nullptr;
			int64 size = 0;
			msg->FindString("host", &host);
			msg->FindString("alias", &alias);
			msg->FindString("fingerprint", &fp);
			msg->FindString("fileId", &fileId);
			msg->FindString("fileName", &fileName);
			msg->FindInt64("size", &size);
			if (!host || !fileId)
				break;

			// Destinazione custom (drag negoziato su una cartella di
			// Tracker) e apertura post-download (menu "Scarica e apri").
			const char* destDirIn = nullptr;
			const char* destNameIn = nullptr;
			bool openAfter = false;
			msg->FindString("destdir", &destDirIn);
			msg->FindString("destname", &destNameIn);
			msg->FindBool("open", &openAfter);

			std::string h = host;
			std::string a = alias ? alias : "?";
			std::string f = fp ? fp : "";
			std::string id = fileId;
			std::string name = (destNameIn && *destNameIn)
				? destNameIn
				: ((fileName && *fileName) ? fileName : fileId);
			std::string destDir = (destDirIn && *destDirIn)
				? destDirIn : fSettings->destDir;
			std::string myFp = fInfo->fingerprint;
			// La chiave del registro usa il nome ORIGINALE del peer:
			// deve restare stabile anche se il drop rinomina il file.
			std::string seenKey = BoardSeenKey(f, id,
				(fileName && *fileName) ? fileName : fileId, size);

			std::thread([this, h, a, id, name, size, destDir, myFp,
					seenKey, openAfter]() {
				SocketHttpClient http;
				std::string path = std::string(kApiDownload)
					+ "?sessionId=board&fileId=" + UrlEncode(id)
					+ "&fingerprint=" + UrlEncode(myFp);
				HttpResponse resp = http.Get(h, kDownloadPort, path);

				bool ok = resp.IsOk();
				std::string finalName = name;
				std::string full = destDir + "/" + finalName;
				if (ok) {
					// Evita di sovrascrivere: suffisso numerico.
					int n = 2;
					while (true) {
						FILE* probe = fopen(full.c_str(), "rb");
						if (!probe)
							break;
						fclose(probe);
						finalName = name + " (" + std::to_string(n)
							+ ")";
						full = destDir + "/" + finalName;
						n++;
					}
					FILE* out = fopen(full.c_str(), "wb");
					if (out) {
						fwrite(resp.body.data(), 1, resp.body.size(),
							out);
						fclose(out);
					} else {
						ok = false;
					}
				}

				BMessage done(kMsgBoardDownloadDone);
				done.AddBool("ok", ok);
				done.AddString("name", finalName.c_str());
				done.AddString("alias", a.c_str());
				done.AddInt64("size", size);
				done.AddString("seenkey", seenKey.c_str());
				done.AddString("path", full.c_str());
				done.AddBool("open", openAfter);
				PostMessage(&done);
			}).detach();
			break;
		}

		case kMsgBoardDownloadDone:
		{
			bool ok = false;
			msg->FindBool("ok", &ok);
			const char* name = nullptr;
			const char* alias = nullptr;
			const char* seenKey = nullptr;
			int64 size = 0;
			msg->FindString("name", &name);
			msg->FindString("alias", &alias);
			msg->FindString("seenkey", &seenKey);
			msg->FindInt64("size", &size);

			if (ok && name) {
				if (seenKey)
					fBoardSeen.Add(seenKey);
				fHistory.Add(false, name, alias ? alias : "?", size);

				BString status(B_TRANSLATE("Received: "));
				status << name;
				fHeader->SetStatus(status.String(), true, false);

				BNotification notif(B_INFORMATION_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(B_TRANSLATE("File received"));
				notif.SetContent(name);
				notif.Send();

				// "Scarica e apri": lancia il file appena salvato con
				// l'applicazione preferita del suo tipo.
				bool openAfter = false;
				msg->FindBool("open", &openAfter);
				const char* fullPath = nullptr;
				msg->FindString("path", &fullPath);
				if (openAfter && fullPath) {
					entry_ref ref;
					if (get_ref_for_path(fullPath, &ref) == B_OK)
						be_roster->Launch(&ref);
				}
			} else if (name) {
				BNotification notif(B_ERROR_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(name);
				notif.SetContent(B_TRANSLATE("Send failed"));
				notif.Send();
			}
			break;
		}

		case kMsgCircleDone:
		{
			int32 ok = 0;
			int32 total = 0;
			msg->FindInt32("ok", &ok);
			msg->FindInt32("total", &total);

			char buf[160];
			if (ok == total)
				snprintf(buf, sizeof(buf), B_TRANSLATE("Sent to %d devices"), (int)ok);
			else
				snprintf(buf, sizeof(buf), B_TRANSLATE("Sent to %d of %d devices"),
					(int)ok, (int)total);
			fHeader->SetStatus(buf, ok > 0, ok == 0);
			if (!fProgressBar->IsHidden())
				fProgressBar->Hide();

			BNotification notif(ok == total
				? B_INFORMATION_NOTIFICATION : B_ERROR_NOTIFICATION);
			notif.SetGroup("LocalSend");
			notif.SetTitle("LocalSend");
			notif.SetContent(buf);
			notif.Send();
			break;
		}

		case kMsgSendText:
		{
			int32 sel = fDeviceList->CurrentSelection();
			if (sel < 0) {
				BAlert* alert = new BAlert("LocalSend",
					B_TRANSLATE("Select a device from the list."), B_TRANSLATE("OK"),
					NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
				alert->Go();
			} else {
				TextInputWindow* tw = new TextInputWindow(this);
				tw->Show();
			}
			break;
		}

		case kMsgTextReady:
		{
			const char* text = nullptr;
			msg->FindString("text", &text);
			if (text) {
				std::lock_guard<std::mutex> lock(fDevicesMtx);
				int32 sel = fDeviceList->CurrentSelection();
				if (sel >= 0 && sel < (int32)fDevices.size()) {
					auto& dev = fDevices[sel];
					SendText(dev.host, dev.port, text);
				}
			}
			break;
		}

		case kMsgTextReceived:
		{
			const char* text = nullptr;
			const char* sender = nullptr;
			msg->FindString("text", &text);
			msg->FindString("sender", &sender);
			if (text) {
				BString title(B_TRANSLATE("Message received"));
				if (sender)
					title << " - " << sender;

				fHeader->SetStatus(title.String(), true, false);

				// Finestra dedicata (non modale) con quattro azioni:
				// Salva su file, Apri nell'editor, Copia, Chiudi.
				TextReceivedWindow* w = new TextReceivedWindow(title, text);
				w->Show();

				BNotification notif(B_INFORMATION_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(B_TRANSLATE("Message received"));
				notif.SetContent(text);
				notif.Send();

				if (!fProgressBar->IsHidden())
					fProgressBar->Hide();
			}
			break;
		}

		case kMsgShowHistory:
		{
			HistoryWindow* hw = new HistoryWindow(&fHistory, this);
			hw->Show();
			break;
		}

		case kMsgShowSettings:
		{
			// Istanza unica: se e' gia' aperta, la riattiva; il messenger
			// diventa invalido da solo quando l'utente la chiude.
			if (fSettingsWinMsgr.IsValid()) {
				fSettingsWinMsgr.SendMessage(kMsgShowSettings);
			} else {
				SettingsWindow* sw = new SettingsWindow(fSettings, this);
				fSettingsWinMsgr = BMessenger(sw);
				sw->Show();
			}
			break;
		}

		case kMsgSettingsSave:
		{
			// Applica le impostazioni modificate.

			// Nome dispositivo. La scrittura dell'alias e' sotto lock: i
			// lettori off-thread (route del server) usano _MyInfoJson().
			{
				std::lock_guard<std::mutex> lock(fInfoMtx);
				fInfo->alias = fSettings->alias;
			}
			fHeader->SetDeviceName(fSettings->alias.c_str());
			SetTitle(BString("LocalSend - ")
				<< fSettings->alias.c_str());
			// Propaga il nuovo nome al faro multicast (che ha la sua copia):
			// senza questo continuerebbe ad annunciare il nome vecchio.
			{
				BMessage idm(kMsgIdentityChanged);
				idm.AddString("alias", fSettings->alias.c_str());
				be_app->PostMessage(&idm);
			}

			// Cartella di destinazione.
			fSink.SetDir(fSettings->destDir);
			fSink.EnsureDir();

			// Porta: richiede riavvio.
			if (fSettings->port != fInfo->port) {
				fInfo->port = fSettings->port;
				BAlert* alert = new BAlert("LocalSend",
					B_TRANSLATE("Port change will take effect after restart."),
					B_TRANSLATE("OK"), NULL, NULL,
					B_WIDTH_AS_USUAL, B_INFO_ALERT);
				alert->Go();
			}

			// PIN e quickSave: letti direttamente da fSettings
			// nelle route, nessuna azione extra.

			break;
		}

		default:
			BWindow::MessageReceived(msg);
	}
}


bool
MainWindow::QuitRequested()
{
	// Se l'utente ha installato il replicant Deskbar, la chiusura della
	// finestra non termina l'app: nascondiamo la finestra e lasciamo
	// vivi l'announcer multicast e il server di ricezione, cosi' il
	// dispositivo continua ad essere visibile sulla LAN. Per uscire
	// davvero serve "Quit" dal menu destro del replicant (kMsgQuitFromTray)
	// o rimuovere prima il replicant dalle impostazioni.
	if (!fAllowQuit && LocalSend::IsDeskbarItemInstalled()) {
		Hide();
		return false;
	}

	StopDownloadServer();
	StopServer();
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::SendToSelected()
{
	int32 sel = fDeviceList->CurrentSelection();
	if (sel < 0) {
		BAlert* alert = new BAlert("LocalSend",
			B_TRANSLATE("Select a device from the list."),
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
		alert->Go();
		return;
	}

	// Se ci sono file pendenti (da CLI o drag&drop), inviali subito.
	if (!fPendingPaths.empty()) {
		std::lock_guard<std::mutex> lock(fDevicesMtx);
		if (sel >= 0 && sel < (int32)fDevices.size()) {
			auto& dev = fDevices[sel];
			SendFiles(dev.host, dev.port, fPendingPaths);
			fPendingPaths.clear();
			return;
		}
	}

	SendPendingOrBrowse();
}


void
MainWindow::SendPendingOrBrowse()
{
	if (!fFilePanel) {
		fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_FILE_NODE, true, new BMessage(kMsgFileSelected));
		fFilePanel->Window()->SetTitle(B_TRANSLATE("Choose files to send"));
	} else {
		// Il panel e' condiviso con Share-link e cerchia: ripristina
		// messaggio ed etichetta di questo flusso.
		fFilePanel->SetMessage(new BMessage(kMsgFileSelected));
	}
	fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Send file…"));
	fFilePanel->Show();
}


void
MainWindow::AddPendingFiles(const std::vector<std::string>& paths)
{
	for (const auto& p : paths)
		fPendingPaths.push_back(p);

	// Aggiorna lo stato per mostrare che ci sono file pronti.
	BString status;
	status << fPendingPaths.size();
	if (fPendingPaths.size() == 1)
		status << " file ready";
	else
		status << " files ready";
	fHeader->SetStatus(status.String(), true, false);
}


void
MainWindow::SendText(const std::string& host, int port,
	const std::string& text)
{
	fHeader->SetStatus(B_TRANSLATE("Sending…"));

	std::thread([this, host, port, text]() {
		// Scrivi il testo in un file temporaneo.
		std::string tmpPath = "/tmp/localsend_text_msg.txt";
		FILE* f = fopen(tmpPath.c_str(), "w");
		if (!f)
			return;
		fwrite(text.data(), 1, text.size(), f);
		fclose(f);

		FileMetadata m;
		m.id = "text-1";
		// L'app ufficiale usa il testo stesso come fileName.
		std::string shortName = text.substr(0,
			text.size() > 64 ? 64 : text.size());
		// Rimuovi newline dal nome.
		for (char& c : shortName) {
			if (c == '\n' || c == '\r')
				c = ' ';
		}
		m.fileName = shortName;
		m.size = static_cast<long long>(text.size());
		m.fileType = "text/plain";
		m.preview = text;
		m.localPath = tmpPath;

		std::vector<FileMetadata> files;
		files.push_back(m);

		SocketHttpClient http;
		http.EnableTls();
		UploadSession session(http, *fInfo);
		SendReport report = SendWithPinRetry(session, host, port, files,
			host.c_str());

		remove(tmpPath.c_str());

		BMessage msg(kMsgSendDone);
		if (report.AllSent()) {
			msg.AddString("status", B_TRANSLATE(" files sent successfully"));
			msg.AddString("sent_file", "message.txt");
		} else {
			msg.AddString("status", B_TRANSLATE("Send failed"));
		}
		msg.AddInt64("total_size", (int64)text.size());
		msg.AddString("peer", host.c_str());
		PostMessage(&msg);
	}).detach();
}


void
MainWindow::SendFiles(const std::string& host, int port,
	const std::vector<std::string>& paths)
{
	fHeader->SetStatus(B_TRANSLATE("Sending…"));

	std::thread([this, host, port, paths]() {
		std::vector<FileMetadata> files;
		for (size_t i = 0; i < paths.size(); i++) {
			FileMetadata m;
			std::string id = "file-" + std::to_string(i + 1);
			if (BuildFileMetadata(paths[i], id, m))
				files.push_back(m);
		}
		if (files.empty())
			return;

		// Progresso: mostra la barra a 0%.
		{
			BMessage prog(kMsgProgress);
			prog.AddFloat("value", 0.0f);
			prog.AddString("label", "0%");
			PostMessage(&prog);
		}

		SocketHttpClient http;
		http.EnableTls();
		UploadSession session(http, *fInfo);
		SendReport report = SendWithPinRetry(session, host, port, files,
			host.c_str(),
			[this](long long sent, long long total) {
				if (total <= 0)
					return;
				BMessage prog(kMsgProgress);
				float pct = (float)sent / (float)total;
				prog.AddFloat("value", pct);
				BString label;
				label << (int)(pct * 100) << "%";
				prog.AddString("label", label.String());
				PostMessage(&prog);
			});

		BMessage msg(kMsgSendDone);
		if (report.AllSent()) {
			BString s;
			s << files.size() << B_TRANSLATE(" files sent successfully");
			msg.AddString("status", s.String());
		} else {
			msg.AddString("status", B_TRANSLATE("Send failed"));
		}
		// Passa i file inviati per la cronologia.
		for (const auto& fo : report.files) {
			if (fo.status == FileOutcome::Status::Sent) {
				msg.AddString("sent_file", fo.fileName.c_str());
			}
		}
		// Passa il totale dei byte e il peer.
		long long totalSent = 0;
		for (const auto& f : files)
			totalSent += f.size;
		msg.AddInt64("total_size", totalSent);
		msg.AddString("peer", host.c_str());
		PostMessage(&msg);
	}).detach();
}


void
MainWindow::SendToCircle(const std::vector<std::string>& paths)
{
	if (paths.empty())
		return;

	// Destinatari: i preferiti attualmente online. Copiati sotto lock,
	// il thread di invio lavora sulla fotografia (un peer che sparisce
	// durante il giro fallisce il suo invio, non il giro intero).
	struct Target {
		std::string alias;
		std::string host;
		int port;
	};
	std::vector<Target> targets;
	{
		std::lock_guard<std::mutex> lock(fDevicesMtx);
		for (const auto& d : fDevices) {
			if (fFavorites.Contains(d.fingerprint))
				targets.push_back({d.alias, d.host, d.port});
		}
	}

	if (targets.empty()) {
		BAlert* alert = new BAlert("LocalSend",
			B_TRANSLATE("No favorites online right now."), B_TRANSLATE("OK"),
			NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
		alert->Go();
		return;
	}

	fHeader->SetStatus(B_TRANSLATE("Sending…"));

	// Un solo thread, invii sequenziali: una UploadSession per destinatario.
	std::thread([this, targets, paths]() {
		std::vector<FileMetadata> files;
		for (size_t i = 0; i < paths.size(); i++) {
			FileMetadata m;
			std::string id = "file-" + std::to_string(i + 1);
			if (BuildFileMetadata(paths[i], id, m))
				files.push_back(m);
		}
		if (files.empty())
			return;

		long long totalSize = 0;
		for (const auto& f : files)
			totalSize += f.size;

		int okCount = 0;
		int n = (int)targets.size();
		for (int t = 0; t < n; t++) {
			const Target& tgt = targets[t];
			{
				BMessage prog(kMsgProgress);
				prog.AddFloat("value", (float)t / (float)n);
				BString label;
				label << tgt.alias.c_str()
					<< " (" << t + 1 << "/" << n << ")";
				prog.AddString("label", label.String());
				PostMessage(&prog);
			}

			SocketHttpClient http;
			http.EnableTls();
			UploadSession session(http, *fInfo);
			SendReport report = SendWithPinRetry(session, tgt.host,
				tgt.port, files, tgt.alias.c_str(),
				[this, t, n](long long sent, long long total) {
					if (total <= 0)
						return;
					// Barra unica per tutto il giro: il
					// destinatario t occupa la fetta [t/n, (t+1)/n).
					BMessage prog(kMsgProgress);
					float frac = (float)sent / (float)total;
					prog.AddFloat("value",
						((float)t + frac) / (float)n);
					BString label;
					label << t + 1 << "/" << n << " - "
						<< (int)(frac * 100) << "%";
					prog.AddString("label", label.String());
					PostMessage(&prog);
				});

			if (report.AllSent())
				okCount++;

			// Cronologia per-destinatario: kMsgSendDone "quiet" registra
			// i file inviati senza toccare status/barra (chiude tutto
			// kMsgCircleDone a fine giro).
			BMessage done(kMsgSendDone);
			done.AddBool("quiet", true);
			for (const auto& fo : report.files) {
				if (fo.status == FileOutcome::Status::Sent)
					done.AddString("sent_file", fo.fileName.c_str());
			}
			done.AddInt64("total_size", totalSize);
			done.AddString("peer", tgt.alias.c_str());
			PostMessage(&done);
		}

		BMessage end(kMsgCircleDone);
		end.AddInt32("ok", okCount);
		end.AddInt32("total", n);
		PostMessage(&end);
	}).detach();
}


void
MainWindow::StartDownloadServer(const std::vector<std::string>& files,
	bool showLinkUi)
{
	if (fDownloadActive)
		StopDownloadServer();

	{
		std::lock_guard<std::mutex> lock(fSharedMtx);
		fSharedFiles = files;
	}

	// Pagina HTML con la lista dei file scaricabili.
	fDownloadServer.Route("GET", "/", [this](const HttpRequest& req) {
		int gate = _BoardGate(req);
		if (gate != 0)
			return HttpServerResponse::Empty(gate);
		BString html;
		html << "<!DOCTYPE html><html><head>"
			"<meta charset=\"utf-8\">"
			"<meta name=\"viewport\" content=\"width=device-width\">"
			"<title>LocalSend - Haiku Box</title>"
			"<style>"
			"body{font-family:sans-serif;max-width:600px;margin:40px auto;"
			"padding:0 20px;background:#f5f5f5}"
			"h1{color:#3c8cdc}"
			"a{display:block;padding:12px 16px;margin:8px 0;"
			"background:#fff;border-radius:8px;color:#333;"
			"text-decoration:none;box-shadow:0 1px 3px rgba(0,0,0,.1)}"
			"a:hover{background:#e8f0fe}"
			".size{color:#888;font-size:0.9em}"
			"</style></head><body>"
			"<h1>LocalSend</h1>"
			"<p>Haiku Box</p>";
		std::lock_guard<std::mutex> lock(fSharedMtx);
		for (size_t i = 0; i < fSharedFiles.size(); i++) {
			std::string name = fSharedFiles[i];
			size_t slash = name.find_last_of('/');
			if (slash != std::string::npos)
				name = name.substr(slash + 1);
			// Dimensione file.
			FILE* f = fopen(fSharedFiles[i].c_str(), "rb");
			long long sz = 0;
			if (f) {
				fseek(f, 0, SEEK_END);
				sz = ftell(f);
				fclose(f);
			}
			html << "<a href=\"/download?id=" << (int32)i << "\">"
				<< name.c_str();
			if (sz >= 1024 * 1024)
				html << " <span class=\"size\">(" << (int)(sz / (1024*1024)) << " MB)</span>";
			else
				html << " <span class=\"size\">(" << (int)(sz / 1024) << " KB)</span>";
			html << "</a>";
		}
		html << "</body></html>";
		return HttpServerResponse{200, "text/html; charset=utf-8",
			html.String(), ""};
	});

	// Download del file (link della pagina HTML: id = indice).
	fDownloadServer.Route("GET", "/download",
		[this](const HttpRequest& req) {
		int gate = _BoardGate(req);
		if (gate != 0)
			return HttpServerResponse::Empty(gate);
		return _ServeSharedFile(atoi(req.Query("id").c_str()));
	});

	// Info del dispositivo: usata dai client API prima del prepare-download.
	fDownloadServer.Route("GET", kApiInfo, [this](const HttpRequest&) {
		return HttpServerResponse::Json(200, _MyInfoJson());
	});

	// prepare-download conforme a LocalSend v2.1 ("download mode"): info del
	// dispositivo + sessionId + mappa fileId -> metadati. E' l'endpoint che i
	// peer (Fase 3) e i client ufficiali usano per sfogliare la bacheca.
	// Accesso via identita' o ?pin= (gate -> 401 se il PIN e' richiesto/errato).
	// La sessionId e' stabile ("board"): un client che rinfresca e rimanda il
	// suo ?sessionId= ottiene la stessa sessione, cioe' il resume del protocollo.
	fDownloadServer.Route("POST", kApiPrepareDownload,
		[this](const HttpRequest& req) {
		int gate = _BoardGate(req);
		if (gate != 0)
			return HttpServerResponse::Empty(gate);
		JsonValue root = JsonValue::Object();
		root["info"] = JsonValue::Parse(_MyInfoJson());
		root["sessionId"] = "board";
		JsonValue filesJson = JsonValue::Object();
		{
			std::lock_guard<std::mutex> lock(fSharedMtx);
			for (size_t i = 0; i < fSharedFiles.size(); i++) {
				FileMetadata m;
				std::string id = "file-" + std::to_string(i + 1);
				if (BuildFileMetadata(fSharedFiles[i], id, m))
					filesJson[id] = m.ToJson();
			}
		}
		root["files"] = filesJson;
		return HttpServerResponse::Json(200, root.Dump());
	});

	// download conforme a LocalSend v2.1: fileId "file-N" -> indice N-1.
	fDownloadServer.Route("GET", kApiDownload,
		[this](const HttpRequest& req) {
		int gate = _BoardGate(req);
		if (gate != 0)
			return HttpServerResponse::Empty(gate);
		if (req.Query("sessionId") != "board")
			return HttpServerResponse::Empty(403);
		std::string fileId = req.Query("fileId");
		int id = -1;
		if (fileId.rfind("file-", 0) == 0)
			id = atoi(fileId.c_str() + 5) - 1;
		return _ServeSharedFile(id);
	});

	if (!fDownloadServer.Start(kDownloadPort)) {
		BAlert* alert = new BAlert("LocalSend",
			B_TRANSLATE("Cannot open port (already in use?)"), B_TRANSLATE("OK"), NULL, NULL,
			B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		return;
	}

	fDownloadActive = true;
	fDownloadThread = std::thread([this]() { fDownloadServer.Run(); });

	if (!showLinkUi) {
		// Avvio silenzioso (drop in bacheca): niente finestra QR ne' link
		// negli appunti. Status essenziale; badge del replicant e peer
		// vengono avvisati da _BoardChanged.
		int count;
		{
			std::lock_guard<std::mutex> lock(fSharedMtx);
			count = (int)fSharedFiles.size();
		}
		char buf[64];
		snprintf(buf, sizeof(buf), B_TRANSLATE("%d files on the board"), count);
		fHeader->SetStatus(buf, true, false);
		_BoardChanged();
		return;
	}

	// Costruisci il link con l'IPv4 dell'interfaccia LAN: gli hostname
	// (gethostname) non si risolvono sulla LAN da telefoni/altri PC,
	// il QR scansionato punterebbe a un nome inesistente. Trucco: un
	// socket UDP "connesso" (senza inviare) al gruppo multicast
	// LocalSend fa scegliere al kernel l'IP sorgente giusto per la
	// LAN, senza dipendenze da getifaddrs (assente su Haiku). Fallback
	// al hostname solo se il trucco fallisce.
	BString host;
	{
		int probe = socket(AF_INET, SOCK_DGRAM, 0);
		if (probe >= 0) {
			sockaddr_in dest{};
			dest.sin_family = AF_INET;
			dest.sin_addr.s_addr = inet_addr(kMulticastGroup);
			dest.sin_port = htons(kDefaultPort);
			if (connect(probe, (sockaddr*)&dest, sizeof(dest)) == 0) {
				sockaddr_in local{};
				socklen_t len = sizeof(local);
				if (getsockname(probe, (sockaddr*)&local, &len) == 0
						&& local.sin_addr.s_addr != 0) {
					char buf[INET_ADDRSTRLEN] = {0};
					if (inet_ntop(AF_INET, &local.sin_addr, buf,
							sizeof(buf)) != nullptr) {
						host = buf;
					}
				}
			}
			close(probe);
		}
	}
	if (host.IsEmpty()) {
		char hostname[256] = {};
		gethostname(hostname, sizeof(hostname));
		host = hostname;
	}

	BString url;
	url << "http://" << host << ":" << kDownloadPort;

	BString status;
	char buf[256];
	snprintf(buf, sizeof(buf), B_TRANSLATE("Link active on port %d. Open in browser:"), kDownloadPort);
	status << buf;
	fHeader->SetStatus(status.String(), true, false);

	// Copia il link negli appunti.
	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		BMessage* clip = be_clipboard->Data();
		clip->AddData("text/plain", B_MIME_TYPE,
			url.String(), url.Length());
		be_clipboard->Commit();
		be_clipboard->Unlock();
	}

	// Finestra dedicata (non modale): QR del link (scan da mobile),
	// URL selezionabile, Stop condivisione + Chiudi. Il link e' gia'
	// negli appunti (l'alert precedente lo diceva; ora ce lo tiene
	// senza notifica ridondante).
	ShareLinkWindow* w = new ShareLinkWindow(status, url,
		BMessenger(this));
	w->Show();

	_BoardChanged();
}


void
MainWindow::StopDownloadServer()
{
	if (!fDownloadActive)
		return;
	fDownloadServer.Stop();
	if (fDownloadThread.joinable())
		fDownloadThread.join();
	fDownloadActive = false;
	{
		std::lock_guard<std::mutex> lock(fSharedMtx);
		fSharedFiles.clear();
	}
	_BoardChanged();
}


void
MainWindow::AddToBoard(const std::vector<std::string>& paths)
{
	if (paths.empty())
		return;

	// Server fermo: primo drop, avvia la bacheca con questi file, in
	// silenzio (niente finestra QR: quella e' del flusso Share-via-link).
	// StartDownloadServer chiama gia' _BoardChanged a fine avvio.
	if (!fDownloadActive) {
		StartDownloadServer(paths, false);
		return;
	}

	// Server attivo: accoda i nuovi path (dedupe) e notifica il cambio.
	bool changed = false;
	{
		std::lock_guard<std::mutex> lock(fSharedMtx);
		for (const auto& p : paths) {
			bool dup = false;
			for (const auto& s : fSharedFiles) {
				if (s == p) {
					dup = true;
					break;
				}
			}
			if (!dup) {
				fSharedFiles.push_back(p);
				changed = true;
			}
		}
	}
	if (changed)
		_BoardChanged();
}


HttpServerResponse
MainWindow::_ServeSharedFile(int id)
{
	// Copia il path sotto lock, poi leggi il file fuori dal lock: il
	// fread di un file grosso non deve bloccare le mutazioni della bacheca.
	std::string path;
	{
		std::lock_guard<std::mutex> lock(fSharedMtx);
		if (id < 0 || id >= (int)fSharedFiles.size())
			return HttpServerResponse::Empty(404);
		path = fSharedFiles[id];
	}

	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return HttpServerResponse::Empty(404);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::string body(size, '\0');
	fread(&body[0], 1, size, f);
	fclose(f);

	// Nome file per il Content-Disposition.
	std::string name = path;
	size_t slash = name.find_last_of('/');
	if (slash != std::string::npos)
		name = name.substr(slash + 1);

	HttpServerResponse resp;
	resp.status = 200;
	resp.contentType = "application/octet-stream";
	resp.extraHeaders = "Content-Disposition: attachment; filename=\""
		+ name + "\"\r\n";
	resp.body = std::move(body);
	return resp;
}


void
MainWindow::_BoardChanged()
{
	int count;
	{
		std::lock_guard<std::mutex> lock(fSharedMtx);
		count = (int)fSharedFiles.size();
	}
	fBoardRev++;

	BMessage m(kMsgBoardChanged);
	m.AddInt32("rev", fBoardRev);
	m.AddBool("active", fDownloadActive);
	m.AddInt32("count", count);
	be_app->PostMessage(&m);

	_PushBoardData();
}


void
MainWindow::_PushBoardData()
{
	if (!fBoardWinMsgr.IsValid())
		return;

	BMessage data(kMsgBoardDataChanged);

	// La mia bacheca: nome (display) + path pieno (icona e drag-out).
	{
		std::lock_guard<std::mutex> lock(fSharedMtx);
		for (const auto& p : fSharedFiles) {
			std::string name = p;
			size_t slash = name.find_last_of('/');
			if (slash != std::string::npos)
				name = name.substr(slash + 1);
			data.AddString("my", name.c_str());
			data.AddString("mypath", p.c_str());
		}
	}

	// Bacheche remote: array paralleli, un elemento per file.
	for (const auto& kv : fRemoteBoards) {
		const RemoteBoard& rb = kv.second;
		for (const auto& f : rb.files) {
			data.AddString("ralias", rb.alias.c_str());
			data.AddString("rhost", rb.host.c_str());
			data.AddString("rfp", rb.fingerprint.c_str());
			data.AddString("rid", f.id.c_str());
			data.AddString("rname", f.fileName.c_str());
			data.AddString("rtype", f.fileType.c_str());
			data.AddInt64("rsize", f.size);
		}
	}

	fBoardWinMsgr.SendMessage(&data);
}


bool
MainWindow::_BoardAccessAllowed(const HttpRequest& req)
{
	if (fSettings->boardVisibility == kBoardEveryone)
		return true;

	// Predicato "questo fingerprint e' ammesso?" secondo la modalita'.
	auto allowed = [this](const std::string& fp) {
		if (fp.empty())
			return false;
		if (fSettings->boardVisibility == kBoardPriority)
			return fPriority.Contains(fp);
		return fFavorites.Contains(fp); // kBoardFavorites
	};

	// Peer Haiku: manda il proprio fingerprint in query (estensione).
	if (allowed(req.Query("fingerprint")))
		return true;

	// Fallback (browser di un contatto ammesso): IP di un peer online noto.
	std::lock_guard<std::mutex> lock(fDevicesMtx);
	for (const auto& d : fDevices) {
		if (d.host == req.clientHost && allowed(d.fingerprint))
			return true;
	}
	return false;
}


int
MainWindow::_BoardGate(const HttpRequest& req)
{
	// Identita' (fingerprint/IP di un contatto) o, in alternativa, ?pin=.
	// Logica pura in DownloadAccessStatus, cosi' e' testabile senza rete.
	return DownloadAccessStatus(_BoardAccessAllowed(req), fSettings->pin,
		req.Query("pin"));
}


void
MainWindow::_PushPriorityData()
{
	if (!fPriorityWinMsgr.IsValid())
		return;
	BMessage data(kMsgPriorityChanged);
	for (const auto& c : fPriority.contacts) {
		data.AddString("fp", c.fingerprint.c_str());
		data.AddString("alias", c.alias.c_str());
	}
	fPriorityWinMsgr.SendMessage(&data);
}


void
MainWindow::_PostPeerStats()
{
	int32 online = 0;
	int32 favOnline = 0;
	{
		std::lock_guard<std::mutex> lock(fDevicesMtx);
		online = (int32)fDevices.size();
		for (const auto& d : fDevices) {
			if (fFavorites.Contains(d.fingerprint))
				favOnline++;
		}
	}
	BMessage m(kMsgPeerStats);
	m.AddInt32("online", online);
	m.AddInt32("favonline", favOnline);
	be_app->PostMessage(&m);
}


// --- LocalSendApp ----------------------------------------------------------

class LocalSendApp : public BApplication {
public:
	LocalSendApp();
	virtual ~LocalSendApp();
	virtual void ReadyToRun();
	virtual void ArgvReceived(int32 argc, char** argv);
	virtual void RefsReceived(BMessage* msg);
	virtual void MessageReceived(BMessage* msg);
	virtual bool QuitRequested();

	// Settato da main() prima di Run() quando l'app e' lanciata con
	// --background (es. dall'autostart al login): la finestra parte
	// nascosta, solo il replicant Deskbar e' visibile.
	void SetStartHidden(bool v) { fStartHidden = v; }

private:
	// Ritrasmette lo stato corrente (bacheca + peer) a tutti i replicant
	// iscritti; rimuove chi non e' piu' raggiungibile.
	void _BroadcastState();

	AppSettings fSettings;
	DeviceInfo fInfo;
	TlsIdentity fTls;
	MulticastAnnouncer* fAnnouncer;
	MainWindow* fWindow;
	// Messenger verso la window: la callback dell'announcer (thread separato)
	// smista i peer di qui. A differenza del puntatore raw, una SendMessage a
	// un looper morto (window distrutta in chiusura) fallisce senza crash.
	BMessenger fWindowMsgr;
	bool fStartHidden = false;

	// Canale replicant (Fase 0): iscritti via kMsgReplicantSubscribe e
	// ultimo snapshot noto di bacheca/peer, inviato con kMsgBoardUpdate.
	std::vector<BMessenger> fSubscribers;
	int32 fBoardRev = 0;
	int32 fBoardCount = 0;
	bool fBoardActive = false;
	int32 fPeersOnline = 0;
	int32 fFavsOnline = 0;
};


LocalSendApp::LocalSendApp()
	:
	BApplication("application/x-vnd.LocalSend"),
	fAnnouncer(nullptr),
	fWindow(nullptr)
{
	// Primo avvio (nessun file settings): assegna un nome a tema Haiku/BeOS
	// e persistilo subito, cosi' resta stabile ai riavvii successivi.
	if (!fSettings.Load(kSettingsFile)) {
		fSettings.alias = GenerateRandomAlias();
		fSettings.Save(kSettingsFile);
	}
	fTls = CreateSelfSignedTls("localsend");

	fInfo.alias = fSettings.alias;
	fInfo.port = fSettings.port;
	fInfo.fingerprint = fTls.fingerprint;
	fInfo.protocol = "https";
}


LocalSendApp::~LocalSendApp()
{
	delete fAnnouncer;
	FreeTlsIdentity(fTls);
}


bool
LocalSendApp::QuitRequested()
{
	// Ferma il faro PRIMA che la finestra venga distrutta: il thread
	// dell'announcer invoca una callback che tocca fWindow, quindi va
	// spento (join) mentre la window e' ancora viva, per evitare la
	// dereferenziazione di un puntatore penzolante in chiusura.
	if (fAnnouncer != nullptr)
		fAnnouncer->Stop();
	return BApplication::QuitRequested();
}


void
LocalSendApp::ReadyToRun()
{
	if (!fTls.ctx) {
		BAlert* alert = new BAlert("Errore",
			B_TRANSLATE("Cannot create TLS certificate."),
			B_TRANSLATE("Quit"), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fWindow = new MainWindow(&fInfo, &fSettings);
	fWindowMsgr = BMessenger(fWindow);
	fWindow->StartServer(fTls.ctx);
	// In modalita' --background partiamo con la finestra nascosta:
	// solo l'icona del replicant e' visibile, il server di ricezione
	// e' attivo, l'utente apre la GUI cliccando il replicant.
	if (fStartHidden)
		fWindow->Hide();
	fWindow->Show();

	fAnnouncer = new MulticastAnnouncer(fInfo);
	// Callback dal thread dell'announcer: impacchetta il peer e lo INOLTRA
	// alla window via BMessenger, invece di chiamarla direttamente col
	// puntatore raw. Cosi', se la window e' stata distrutta in chiusura,
	// la SendMessage fallisce senza crash invece di dereferenziare memoria
	// liberata. AddDevice gira quindi sul looper della window.
	fAnnouncer->SetPeerHeardCallback(
		[this](const MulticastAnnouncer::Peer& p) {
			if (!fWindowMsgr.IsValid())
				return;
			BMessage m(kMsgPeerHeard);
			m.AddString("alias", p.alias.c_str());
			m.AddString("host", p.host.c_str());
			m.AddInt32("port", p.port);
			m.AddString("deviceType", p.deviceType.c_str());
			m.AddString("deviceModel", p.deviceModel.c_str());
			m.AddString("fingerprint", p.fingerprint.c_str());
			m.AddInt32("boardRev", p.boardRev);
			m.AddBool("haiku", p.isHaiku);
			fWindowMsgr.SendMessage(&m);
		});
	fAnnouncer->Start();

	// Ripristino del replicant Deskbar: la Deskbar non sempre rigenera lo
	// shelf al reboot, quindi se l'utente l'aveva installato (flag in
	// settings) e ora manca, lo reinstalliamo silenziosamente. Errori
	// non bloccanti: solo log su stderr.
	if (fSettings.deskbarItem && !LocalSend::IsDeskbarItemInstalled()) {
		status_t err = LocalSend::InstallDeskbarItem();
		if (err != B_OK) {
			fprintf(stderr, "[deskbar] auto-restore fallito: %s (%ld)\n",
				strerror(err), (long)err);
		}
	}
}


void
LocalSendApp::ArgvReceived(int32 argc, char** argv)
{
	std::vector<std::string> paths;
	for (int32 i = 1; i < argc; i++) {
		// Ignora le opzioni (iniziano con --).
		if (argv[i][0] == '-' && argv[i][1] == '-')
			continue;
		paths.push_back(argv[i]);
	}
	if (!paths.empty() && fWindow) {
		if (fWindow->LockLooper()) {
			fWindow->AddPendingFiles(paths);
			fWindow->UnlockLooper();
		}
	}
}


void
LocalSendApp::_BroadcastState()
{
	BMessage upd(kMsgBoardUpdate);
	upd.AddInt32("rev", fBoardRev);
	upd.AddInt32("count", fBoardCount);
	upd.AddBool("active", fBoardActive);
	upd.AddInt32("peers", fPeersOnline);
	upd.AddInt32("favonline", fFavsOnline);

	// Chi non risponde piu' (es. Tracker riavviato senza ripristinare il
	// replicant) esce dalla lista: nessun retry, si re-iscrivera' da solo.
	for (auto it = fSubscribers.begin(); it != fSubscribers.end();) {
		if (it->SendMessage(&upd) != B_OK)
			it = fSubscribers.erase(it);
		else
			++it;
	}
}


void
LocalSendApp::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgReplicantSubscribe:
		{
			BMessenger target;
			if (msg->FindMessenger("target", &target) != B_OK)
				return;
			bool present = false;
			for (const auto& m : fSubscribers) {
				if (m == target) {
					present = true;
					break;
				}
			}
			if (!present)
				fSubscribers.push_back(target);
			// Il nuovo iscritto riceve subito lo stato corrente.
			_BroadcastState();
			return;
		}

		case kMsgReplicantUnsubscribe:
		{
			BMessenger target;
			if (msg->FindMessenger("target", &target) != B_OK)
				return;
			for (auto it = fSubscribers.begin();
					it != fSubscribers.end(); ++it) {
				if (*it == target) {
					fSubscribers.erase(it);
					break;
				}
			}
			return;
		}

		case kMsgBoardChanged:
		{
			// Dalla MainWindow: bacheca cambiata. Aggiorna l'annuncio
			// multicast (boardRev + download) e avvisa subito LAN e
			// replicant iscritti.
			int32 rev = 0;
			int32 count = 0;
			bool active = false;
			msg->FindInt32("rev", &rev);
			msg->FindInt32("count", &count);
			msg->FindBool("active", &active);
			fBoardRev = rev;
			fBoardCount = count;
			fBoardActive = active;
			fInfo.boardRev = (int)rev;
			fInfo.download = active;
			if (fAnnouncer != nullptr) {
				fAnnouncer->SetBoard((int)rev, active);
				fAnnouncer->TriggerBurst();
			}
			_BroadcastState();
			return;
		}

		case kMsgPeerStats:
		{
			msg->FindInt32("online", &fPeersOnline);
			msg->FindInt32("favonline", &fFavsOnline);
			_BroadcastState();
			return;
		}

		case kMsgIdentityChanged:
		{
			// Dalla MainWindow: il nome dispositivo e' cambiato. Aggiorna
			// la copia annunciata dal faro e ri-annuncia subito, cosi' i
			// peer vedono il nuovo nome senza aspettare il riavvio.
			const char* alias = nullptr;
			if (msg->FindString("alias", &alias) == B_OK && alias) {
				fInfo.alias = alias;
				if (fAnnouncer != nullptr) {
					fAnnouncer->SetAlias(alias);
					fAnnouncer->TriggerBurst();
				}
			}
			return;
		}


		case kMsgStopShare:
		{
			// Dal menu del replicant desktop: ferma/svuota la bacheca.
			// Inoltrato alla window, che possiede il download server.
			if (fWindow != nullptr)
				fWindow->PostMessage(kMsgStopShare);
			return;
		}

		case kMsgReplicantDrop:
		{
			// Drop di file sul replicant. Default: pubblica in bacheca;
			// con "circle" true (Shift al drop) invia subito alla cerchia.
			if (fWindow == nullptr)
				return;
			bool circle = false;
			msg->FindBool("circle", &circle);
			BMessage fwd(circle ? kMsgSendToCircle : kMsgAddToBoard);
			entry_ref ref;
			for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++)
				fwd.AddRef("refs", &ref);
			fWindow->PostMessage(&fwd);
			return;
		}

		case kMsgOpenBoard:
		{
			// Click sul replicant desktop: GUI in primo piano + Bacheca.
			if (fWindow != nullptr && fWindow->LockLooper()) {
				if (fWindow->IsHidden())
					fWindow->Show();
				fWindow->Activate(true);
				fWindow->UnlockLooper();
				fWindow->PostMessage(kMsgShowBoard);
			}
			if (fAnnouncer != nullptr)
				fAnnouncer->TriggerBurst();
			return;
		}

		case B_SILENT_RELAUNCH:
		{
			// Click sul replicant Deskbar (o riavvio singolo): se la
			// finestra esiste, riportala visibile e in primo piano.
			if (fWindow != nullptr && fWindow->LockLooper()) {
				if (fWindow->IsHidden())
					fWindow->Show();
				fWindow->Activate(true);
				fWindow->UnlockLooper();
			}
			// Riportare a video la finestra dopo background e' anche
			// un buon momento per ri-scoprire chi c'e' in rete: gli
			// announce periodici da 5s sembrano eterni in questo caso.
			if (fAnnouncer != nullptr)
				fAnnouncer->TriggerBurst();
			return;
		}

		case kMsgTriggerBurst:
		{
			// Pulsante Refresh nella UI: l'announcer fa scattare un
			// annuncio extra, i peer attivi rispondono in unicast e
			// la lista si popola in pochi ms.
			if (fAnnouncer != nullptr)
				fAnnouncer->TriggerBurst();
			return;
		}

		case kMsgQuitFromTray:
		{
			// "Quit" dal menu destro del replicant: aggira l'hide-on-close
			// abilitando la chiusura vera della finestra, poi termina l'app.
			if (fWindow != nullptr && fWindow->LockLooper()) {
				fWindow->SetAllowQuit(true);
				fWindow->UnlockLooper();
			}
			PostMessage(B_QUIT_REQUESTED);
			return;
		}
	}
	BApplication::MessageReceived(msg);
}


void
LocalSendApp::RefsReceived(BMessage* msg)
{
	std::vector<std::string> paths;
	entry_ref ref;
	for (int i = 0; msg->FindRef("refs", i, &ref) == B_OK; i++) {
		BPath path(&ref);
		if (path.InitCheck() == B_OK)
			paths.push_back(path.Path());
	}
	if (!paths.empty() && fWindow) {
		if (fWindow->LockLooper()) {
			fWindow->AddPendingFiles(paths);
			fWindow->UnlockLooper();
		}
	}
}


// --- main ------------------------------------------------------------------

int
main(int argc, char** argv)
{
	bool startHidden = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--background") == 0)
			startHidden = true;
	}
	LocalSendApp app;
	app.SetStartHidden(startHidden);
	app.Run();
	return 0;
}
