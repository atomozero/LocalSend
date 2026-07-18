// Annuncio e scoperta multicast UDP per LocalSend v2.1 (L2).
// Un thread in background invia periodicamente il DeviceInfo al gruppo
// multicast 224.0.0.167:53317 e risponde agli annunci di altri dispositivi.
#ifndef _LOCALSEND_MULTICAST_ANNOUNCER_H
#define _LOCALSEND_MULTICAST_ANNOUNCER_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "protocol/Models.h"

namespace LocalSend {

class MulticastAnnouncer {
public:
	// Callback invocato dal thread del announcer ogni volta che si sente
	// un peer (annuncio o reply). Chi imposta la callback deve smistare
	// thread-safe verso la UI (es. BLooper::PostMessage).
	struct Peer {
		std::string alias;
		std::string fingerprint;
		std::string host;
		int port = 0;
		std::string deviceType;
		// Piattaforma/OS del peer (campo "deviceModel" del protocollo:
		// "Windows", "Linux", "macOS", "iPhone", "Haiku", ...). Vuoto se
		// il peer non lo annuncia.
		std::string deviceModel;
		// Estensione Haiku: revisione della bacheca del peer (0 = nessuna).
		int boardRev = 0;
		// True se il peer gira la nostra app Haiku (marcatore "app" o
		// deviceModel "Haiku"): abilita badge e funzioni Haiku-to-Haiku.
		bool isHaiku = false;
	};
	using PeerHeardCallback = std::function<void(const Peer&)>;

	MulticastAnnouncer(const DeviceInfo& info);
	~MulticastAnnouncer();

	// Da chiamare prima di Start(). La callback puo' venire invocata da un
	// thread non-GUI: chi la registra deve usare PostMessage o lock interni.
	void SetPeerHeardCallback(PeerHeardCallback cb) { fPeerCb = cb; }

	// Apre il socket UDP, unisce il gruppo multicast e avvia il thread.
	// Ritorna false se il socket non puo' essere aperto.
	bool Start();

	// Ferma il thread e chiude il socket.
	void Stop();

	// Spara subito un annuncio multicast extra (oltre al tick periodico).
	// Usato all'apertura della finestra e dal pulsante Refresh per
	// forzare le risposte unicast immediate dai peer gia' attivi.
	void TriggerBurst();

	// Aggiorna a caldo la parte "bacheca" del DeviceInfo annunciato
	// (boardRev + flag download). Thread-safe: il prossimo annuncio
	// (tick periodico o TriggerBurst) esce gia' con i valori nuovi.
	void SetBoard(int boardRev, bool downloadActive);

	// Aggiorna a caldo l'alias annunciato (nome dispositivo). Senza questo,
	// rinominando il dispositivo il faro continuerebbe ad annunciare il
	// vecchio nome fino al riavvio. Thread-safe.
	void SetAlias(const std::string& alias);

private:
	void Run();
	// Apre e configura il socket multicast (bind + join + opzioni). Ritorna
	// false se la rete non e' pronta: il thread ritenta finche' non riesce.
	bool _OpenSocket();
	// Serializza l'annuncio corrente sotto lock (fInfo puo' cambiare
	// via SetBoard mentre il thread invia).
	std::string _AnnounceJson(bool announce);

	std::mutex fInfoMtx;
	DeviceInfo fInfo;
	int fFd;
	std::atomic<bool> fRunning;
	std::thread fThread;
	PeerHeardCallback fPeerCb;
};

} // namespace LocalSend

#endif // _LOCALSEND_MULTICAST_ANNOUNCER_H
