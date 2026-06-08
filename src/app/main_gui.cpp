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
#include <Catalog.h>
#include <CheckBox.h>
#include <ControlLook.h>
#include <FilePanel.h>
#include <Font.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <LocaleRoster.h>
#include <ListView.h>
#include <MenuItem.h>
#include <MenuField.h>
#include <Notification.h>
#include <PopUpMenu.h>
#include <Path.h>
#include <Region.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StatusBar.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>

#include "app/Locale.h"
#include "net/MulticastAnnouncer.h"
#include "net/TlsContext.h"
#include "net/haiku/SocketHttpClient.h"
#include "net/haiku/SocketHttpServer.h"
#include "protocol/Constants.h"
#include "protocol/Fingerprint.h"
#include "protocol/Models.h"
#include "client/FileSource.h"
#include "client/UploadSession.h"
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
	kMsgShowSettings	= 'SETT',
	kMsgSettingsSave	= 'SSAV',
	kMsgBrowseDir		= 'BRWD',
	kMsgDirSelected		= 'DSEL',
	kMsgShowHistory		= 'HIST',
	kMsgClearHistory	= 'HCLR',
	kMsgSendText		= 'STXT',
	kMsgTextReady		= 'TXRD',
	kMsgTextReceived	= 'TXRC'
};


// --- Impostazioni persistenti ----------------------------------------------

struct AppSettings {
	std::string alias = "Haiku Box";
	std::string destDir = "./ricevuti";
	int port = 53317;
	std::string pin;
	bool quickSave = false;
	bool https = true;
	std::string language;

	void DetectSystemLanguage()
	{
		if (!language.empty())
			return; // gia' impostata dall'utente
		BMessage preferred;
		if (BLocaleRoster::Default()->GetPreferredLanguages(&preferred)
				== B_OK) {
			const char* lang = nullptr;
			if (preferred.FindString("language", &lang) == B_OK && lang)
				language = std::string(lang, 2); // primi 2 char
		}
		if (language.empty())
			language = "en"; // fallback
	}

	void Load(const std::string& path)
	{
		bool hasLang = false;
		FILE* f = fopen(path.c_str(), "r");
		if (!f) {
			DetectSystemLanguage();
			SetLanguageFromName(language.c_str());
			return;
		}
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
			else if (key == "language") {
				language = val;
				hasLang = true;
			}
		}
		fclose(f);
		if (!hasLang)
			DetectSystemLanguage();
		SetLanguageFromName(language.c_str());
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
		fprintf(f, "language=%s\n", language.c_str());
		fclose(f);
	}
};

static const char* kSettingsFile = "./localsend_settings";
static const char* kHistoryFile = "./localsend_history";
static const int kMaxHistoryEntries = 30;


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
		char buf[32];
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


// --- Dispositivo scoperto in LAN -------------------------------------------

struct DiscoveredDevice {
	std::string alias;
	std::string host;
	int port;
	std::string deviceType;
	std::string fingerprint;
};


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
	DeviceListItem(const char* name, const char* type, const char* ip)
		:
		BListItem(),
		fName(name),
		fType(type),
		fIp(ip)
	{
	}

	virtual void DrawItem(BView* owner, BRect frame, bool complete)
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

		BString detail;
		detail << fType << " - " << fIp;
		detailFont.GetHeight(&fh);
		owner->DrawString(detail.String(),
			BPoint(textLeft, frame.bottom - 4 - fh.descent));
	}

	virtual void Update(BView* owner, const BFont* font)
	{
		// Altezza fissa per ogni elemento.
		SetHeight(48);
		SetWidth(owner->Bounds().Width());
	}

	const BString& Name() const { return fName; }

private:
	BString fName;
	BString fType;
	BString fIp;
};


// --- HeaderView: pannello superiore con nome e stato -----------------------

class HeaderView : public BView {
public:
	HeaderView()
		:
		BView("header", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
	{
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
		SetExplicitMinSize(BSize(350, 70));
		SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, 70));
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

	virtual void Draw(BRect updateRect)
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

		// Cerchio con iniziale.
		float circleR = 18;
		rgb_color accent = (rgb_color){60, 140, 220, 255};
		SetHighColor(accent);
		FillEllipse(BPoint(x + circleR, y + circleR - 4), circleR, circleR);

		SetHighColor(255, 255, 255);
		BFont initialFont(be_bold_font);
		initialFont.SetSize(18);
		SetFont(&initialFont);
		char initial[2] = {fDeviceName.Length() > 0
			? fDeviceName.String()[0] : '?', 0};
		float cw = StringWidth(initial);
		font_height ifh;
		initialFont.GetHeight(&ifh);
		DrawString(initial,
			BPoint(x + circleR - cw / 2,
				y + circleR - 4 + (ifh.ascent - ifh.descent) / 2));

		float textX = x + circleR * 2 + 14;

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
};


// --- TextInputWindow -------------------------------------------------------

class TextInputWindow : public BWindow {
public:
	TextInputWindow(BWindow* target)
		:
		BWindow(BRect(150, 150, 530, 350), Tr(S_SEND_TEXT),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fTarget(target)
	{
		BStringView* label = new BStringView("hint", Tr(S_TEXT_HINT));

		fTextView = new BTextView("text");
		fTextView->SetStylable(false);
		fTextView->MakeEditable(true);
		fTextView->SetWordWrap(true);
		BScrollView* scroll = new BScrollView("tscroll", fTextView,
			0, false, true, B_FANCY_BORDER);

		BButton* sendBtn = new BButton(Tr(S_SEND_FILE),
			new BMessage(kMsgTextReady));
		BButton* cancelBtn = new BButton(Tr(S_CANCEL),
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


// --- HistoryWindow ---------------------------------------------------------

class HistoryWindow : public BWindow {
public:
	HistoryWindow(TransferHistory* history, BWindow* parent)
		:
		BWindow(BRect(120, 120, 530, 430), Tr(S_HISTORY),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fHistory(history)
	{
		fList = new BListView("history");
		BScrollView* scroll = new BScrollView("hscroll", fList,
			0, false, true, B_NO_BORDER);

		BButton* clearBtn = new BButton(Tr(S_CLEAR_HISTORY),
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
		BWindow(BRect(150, 150, 560, 530), Tr(S_SETTINGS_TITLE),
			B_TITLED_WINDOW,
			B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
				| B_CLOSE_ON_ESCAPE),
		fSettings(settings),
		fTarget(target)
	{
		// Campi.
		fAliasField = new BTextControl(Tr(S_DEVICE_NAME),
			settings->alias.c_str(), NULL);
		fDestDirField = new BTextControl(Tr(S_SAVE_FOLDER),
			settings->destDir.c_str(), NULL);
		BButton* browseBtn = new BButton(Tr(S_BROWSE),
			new BMessage(kMsgBrowseDir));

		BString portStr;
		portStr << settings->port;
		fPortField = new BTextControl(Tr(S_PORT), portStr.String(), NULL);

		fPinField = new BTextControl(Tr(S_PIN_LABEL),
			settings->pin.c_str(), NULL);
		fQuickSaveBox = new BCheckBox(Tr(S_QUICK_SAVE), NULL);
		fQuickSaveBox->SetValue(settings->quickSave ? B_CONTROL_ON
			: B_CONTROL_OFF);
		fHttpsBox = new BCheckBox(Tr(S_ENABLE_HTTPS), NULL);
		fHttpsBox->SetValue(B_CONTROL_ON);
		fHttpsBox->SetEnabled(false);

		// Lingua.
		fLangMenu = new BPopUpMenu("lang");
		for (int i = 0; i < kLangCount; i++) {
			BMenuItem* item = new BMenuItem(
				LanguageName((Language)i), NULL);
			if (strcmp(LanguageCode((Language)i),
					settings->language.c_str()) == 0)
				item->SetMarked(true);
			fLangMenu->AddItem(item);
		}
		fLangField = new BMenuField(Tr(S_LANGUAGE), fLangMenu);

		// Pulsanti.
		BButton* saveBtn = new BButton(Tr(S_SAVE),
			new BMessage(kMsgSettingsSave));
		BButton* cancelBtn = new BButton(Tr(S_CANCEL),
			new BMessage(B_QUIT_REQUESTED));

		// Intestazioni sezione (grassetto).
		auto MakeLabel = [](const char* text) {
			BStringView* sv = new BStringView("", text);
			sv->SetFont(be_bold_font);
			return sv;
		};

		// Layout senza BBox (piu' stabile).
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(MakeLabel(Tr(S_GENERAL)))
			.Add(fAliasField)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(fDestDirField, 1.0)
				.Add(browseBtn, 0.0)
			.End()
			.Add(fLangField)
			.AddStrut(B_USE_ITEM_SPACING)
			.Add(MakeLabel(Tr(S_NETWORK)))
			.Add(fPortField)
			.Add(fHttpsBox)
			.AddStrut(B_USE_ITEM_SPACING)
			.Add(MakeLabel(Tr(S_SECURITY)))
			.Add(fPinField)
			.Add(fQuickSaveBox)
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
			case kMsgBrowseDir:
				fDirPanel->Show();
				break;

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
				// Lingua.
				bool langChanged = false;
				BMenuItem* marked = fLangMenu->FindMarked();
				if (marked) {
					int idx = fLangMenu->IndexOf(marked);
					if (idx >= 0 && idx < kLangCount) {
						const char* code = LanguageCode((Language)idx);
						if (fSettings->language != code) {
							fSettings->language = code;
							SetLanguage((Language)idx);
							langChanged = true;
						}
					}
				}
				fSettings->Save(kSettingsFile);

				// Notifica la finestra principale.
				fTarget->PostMessage(new BMessage(kMsgSettingsSave));

				if (langChanged) {
					BAlert* alert = new BAlert("LocalSend",
						Tr(S_LANG_RESTART),
						Tr(S_OK), NULL, NULL,
						B_WIDTH_AS_USUAL, B_INFO_ALERT);
					alert->Go();
				}
				PostMessage(B_QUIT_REQUESTED);
				break;
			}

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
	BCheckBox* fHttpsBox;
	BPopUpMenu* fLangMenu;
	BMenuField* fLangField;
	BFilePanel* fDirPanel;
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

private:
	void SendToSelected();
	void SendText(const std::string& host, int port,
		const std::string& text);
	void SendFiles(const std::string& host, int port,
		const std::vector<std::string>& paths);

	DeviceInfo* fInfo;
	AppSettings* fSettings;

	HeaderView* fHeader;
	BStatusBar* fProgressBar;
	BListView* fDeviceList;
	BFilePanel* fFilePanel;

	// Server in background.
	SocketHttpServer fServer;
	ReceiveSession fSession;
	FileSink fSink;
	std::thread fServerThread;

	// Progresso ricezione.
	int fRecvTotal = 0;
	int fRecvDone = 0;

	// Cronologia.
	TransferHistory fHistory;
	std::string fLastSenderAlias;

	// Dispositivi scoperti.
	std::mutex fDevicesMtx;
	std::vector<DiscoveredDevice> fDevices;
};


MainWindow::MainWindow(DeviceInfo* info, AppSettings* settings)
	:
	BWindow(BRect(100, 100, 550, 480), "LocalSend",
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

	// Header.
	fHeader = new HeaderView();
	fHeader->SetDeviceName(info->alias.c_str());
	fHeader->SetFingerprint(info->fingerprint.c_str());
	fHeader->SetStatus(Tr(S_READY), true, false);

	// Lista dispositivi con scroll.
	fDeviceList = new BListView("devices");
	fDeviceList->SetInvocationMessage(new BMessage(kMsgDeviceInvoked));
	BScrollView* scroll = new BScrollView("scroll", fDeviceList,
		0, false, true, B_NO_BORDER);

	// Etichetta sezione dispositivi.
	BStringView* devLabel = new BStringView("devlabel",
		Tr(S_DEVICES_IN_NETWORK));
	BFont labelFont(be_bold_font);
	labelFont.SetSize(be_plain_font->Size());
	devLabel->SetFont(&labelFont);

	// Barra di progresso.
	fProgressBar = new BStatusBar("progress");
	fProgressBar->SetBarHeight(10);
	fProgressBar->SetMaxValue(1.0);
	fProgressBar->Hide();

	// Pulsanti.
	BButton* sendBtn = new BButton(Tr(S_SEND_FILE),
		new BMessage(kMsgSendFile));
	BButton* textBtn = new BButton(Tr(S_SEND_TEXT),
		new BMessage(kMsgSendText));
	BButton* settingsBtn = new BButton(Tr(S_SETTINGS),
		new BMessage(kMsgShowSettings));
	BButton* historyBtn = new BButton(Tr(S_HISTORY),
		new BMessage(kMsgShowHistory));

	// Layout.
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fHeader)
		.AddGroup(B_VERTICAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_WINDOW_INSETS)
			.Add(devLabel)
			.Add(scroll, 1.0)
			.Add(fProgressBar)
			.AddStrut(B_USE_HALF_ITEM_SPACING)
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(sendBtn, 1.0)
				.Add(textBtn, 1.0)
			.End()
			.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
				.Add(historyBtn, 1.0)
				.Add(settingsBtn, 1.0)
			.End()
		.End()
	.End();

	SetSizeLimits(380, B_SIZE_UNLIMITED, 300, B_SIZE_UNLIMITED);
	CenterOnScreen();
}


MainWindow::~MainWindow()
{
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

		// Se quickSave e' attivo, accetta senza chiedere.
		if (!fSettings->quickSave) {
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

		PrepareOutcome out = fSession.Prepare(in);
		switch (out.status) {
			case PrepareStatus::SessionBusy:
				return HttpServerResponse::Empty(409);
			case PrepareStatus::NothingAccepted:
				return HttpServerResponse::Empty(204);
			case PrepareStatus::Accepted:
				break;
		}

		// Salva il mittente per la cronologia.
		fLastSenderAlias = in.sender.alias;

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

		if (!fSession.ValidateUpload(sessionId, fileId, token))
			return HttpServerResponse::Empty(403);

		const FileMetadata* meta = fSession.File(fileId);
		std::string name = meta ? meta->fileName : fileId;
		std::string mimeType = meta ? meta->fileType : "";

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

	// Route: register + info.
	auto infoHandler = [this](const HttpRequest&) {
		return HttpServerResponse::Json(200, fInfo->ToJson().Dump());
	};
	fServer.Route("POST", kApiRegister, infoHandler);
	fServer.Route("GET", kApiInfo, infoHandler);
	fServer.Route("GET", "/api/localsend/v1/info", infoHandler);

	if (!fServer.Start(fInfo->port)) {
		BAlert* alert = new BAlert("Errore",
			Tr(S_PORT_BUSY),
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
	std::lock_guard<std::mutex> lock(fDevicesMtx);
	for (const auto& d : fDevices) {
		if (d.fingerprint == dev.fingerprint)
			return;
	}
	fDevices.push_back(dev);

	BMessage msg(kMsgDeviceFound);
	msg.AddString("alias", dev.alias.c_str());
	msg.AddString("type", dev.deviceType.c_str());
	msg.AddString("ip", dev.host.c_str());
	PostMessage(&msg);
}


void
MainWindow::HandleIncoming(IncomingRequest* req)
{
	BString text(Tr(S_FROM));
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

	BAlert* alert = new BAlert(Tr(S_INCOMING_TRANSFER),
		text.String(), Tr(S_ACCEPT), Tr(S_REJECT), NULL,
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
				int32 sel = fDeviceList->CurrentSelection();
				if (sel < 0 || sel >= (int32)fDevices.size()) {
					BAlert* alert = new BAlert("LocalSend",
						Tr(S_SELECT_DEVICE), Tr(S_OK),
						NULL, NULL, B_WIDTH_AS_USUAL,
						B_INFO_ALERT);
					alert->Go();
				} else {
					auto& dev = fDevices[sel];
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
				BString status(Tr(S_RECEIVED_COLON));
				status << name;
				fHeader->SetStatus(status.String(), true, false);

				BNotification notif(B_INFORMATION_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(Tr(S_FILE_RECEIVED));
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
			const char* ip = nullptr;
			msg->FindString("alias", &alias);
			msg->FindString("type", &type);
			msg->FindString("ip", &ip);
			if (alias)
				fDeviceList->AddItem(new DeviceListItem(
					alias, type ? type : "", ip ? ip : ""));
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
			const char* status = nullptr;
			const char* checkSent = nullptr;
			msg->FindString("status", &status);
			bool ok = (msg->FindString("sent_file", &checkSent) == B_OK);
			if (status)
				fHeader->SetStatus(status, ok, !ok);
			if (!fProgressBar->IsHidden())
				fProgressBar->Hide();

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

		case kMsgSendText:
		{
			int32 sel = fDeviceList->CurrentSelection();
			if (sel < 0) {
				BAlert* alert = new BAlert("LocalSend",
					Tr(S_SELECT_DEVICE), Tr(S_OK),
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
				BString title(Tr(S_TEXT_RECEIVED));
				if (sender)
					title << " - " << sender;

				fHeader->SetStatus(title.String(), true, false);

				BAlert* alert = new BAlert(title.String(),
					text, Tr(S_OK), NULL, NULL,
					B_WIDTH_AS_USUAL, B_INFO_ALERT);
				alert->SetShortcut(0, B_ENTER);
				alert->Go();

				BNotification notif(B_INFORMATION_NOTIFICATION);
				notif.SetGroup("LocalSend");
				notif.SetTitle(Tr(S_TEXT_RECEIVED));
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
			SettingsWindow* sw = new SettingsWindow(fSettings, this);
			sw->Show();
			break;
		}

		case kMsgSettingsSave:
		{
			// Applica le impostazioni modificate.

			// Nome dispositivo.
			fInfo->alias = fSettings->alias;
			fHeader->SetDeviceName(fSettings->alias.c_str());
			SetTitle(BString("LocalSend - ")
				<< fSettings->alias.c_str());

			// Cartella di destinazione.
			fSink.SetDir(fSettings->destDir);
			fSink.EnsureDir();

			// Porta: richiede riavvio.
			if (fSettings->port != fInfo->port) {
				fInfo->port = fSettings->port;
				BAlert* alert = new BAlert("LocalSend",
					Tr(S_PORT_RESTART),
					Tr(S_OK), NULL, NULL,
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
			Tr(S_SELECT_DEVICE),
			"OK", NULL, NULL, B_WIDTH_AS_USUAL, B_INFO_ALERT);
		alert->Go();
		return;
	}

	if (!fFilePanel) {
		fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this),
			NULL, B_FILE_NODE, true, new BMessage(kMsgFileSelected));
		fFilePanel->SetButtonLabel(B_DEFAULT_BUTTON, Tr(S_SEND_FILE));
		fFilePanel->Window()->SetTitle(Tr(S_CHOOSE_FILES));
	}
	fFilePanel->Show();
}


void
MainWindow::SendText(const std::string& host, int port,
	const std::string& text)
{
	fHeader->SetStatus(Tr(S_SENDING));

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
		SendReport report = session.Send(host, port, files, "");

		remove(tmpPath.c_str());

		BMessage msg(kMsgSendDone);
		if (report.AllSent()) {
			msg.AddString("status", Tr(S_FILES_SENT));
			msg.AddString("sent_file", "message.txt");
		} else {
			msg.AddString("status", Tr(S_SEND_FAILED));
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
	fHeader->SetStatus(Tr(S_SENDING));

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
		SendReport report = session.Send(host, port, files, "",
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
			s << files.size() << Tr(S_FILES_SENT);
			msg.AddString("status", s.String());
		} else {
			msg.AddString("status", Tr(S_SEND_FAILED));
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


// --- LocalSendApp ----------------------------------------------------------

class LocalSendApp : public BApplication {
public:
	LocalSendApp();
	virtual ~LocalSendApp();
	virtual void ReadyToRun();

private:
	void StartDiscoveryListener();

	AppSettings fSettings;
	DeviceInfo fInfo;
	TlsIdentity fTls;
	MulticastAnnouncer* fAnnouncer;
	MainWindow* fWindow;
	std::thread fDiscoveryThread;
	bool fDiscoveryRunning;
};


LocalSendApp::LocalSendApp()
	:
	BApplication("application/x-vnd.LocalSend"),
	fAnnouncer(nullptr),
	fWindow(nullptr),
	fDiscoveryRunning(false)
{
	fSettings.Load(kSettingsFile);
	fTls = CreateSelfSignedTls("localsend");

	fInfo.alias = fSettings.alias;
	fInfo.port = fSettings.port;
	fInfo.fingerprint = fTls.fingerprint;
	fInfo.protocol = "https";
}


LocalSendApp::~LocalSendApp()
{
	fDiscoveryRunning = false;
	if (fDiscoveryThread.joinable())
		fDiscoveryThread.join();
	delete fAnnouncer;
	FreeTlsIdentity(fTls);
}


void
LocalSendApp::ReadyToRun()
{
	if (!fTls.ctx) {
		BAlert* alert = new BAlert("Errore",
			Tr(S_TLS_ERROR),
			Tr(S_EXIT), NULL, NULL, B_WIDTH_AS_USUAL, B_STOP_ALERT);
		alert->Go();
		PostMessage(B_QUIT_REQUESTED);
		return;
	}

	fWindow = new MainWindow(&fInfo, &fSettings);
	fWindow->StartServer(fTls.ctx);
	fWindow->Show();

	fAnnouncer = new MulticastAnnouncer(fInfo);
	fAnnouncer->Start();

	StartDiscoveryListener();
}


void
LocalSendApp::StartDiscoveryListener()
{
	fDiscoveryRunning = true;
	fDiscoveryThread = std::thread([this]() {
		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0)
			return;

		int yes = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(kDefaultPort + 1);

		if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
			close(fd);
			return;
		}

		ip_mreq mreq{};
		mreq.imr_multiaddr.s_addr = inet_addr(kMulticastGroup);
		mreq.imr_interface.s_addr = htonl(INADDR_ANY);
		setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

		timeval tv{};
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		while (fDiscoveryRunning) {
			char buf[2048];
			sockaddr_in from{};
			socklen_t fromLen = sizeof(from);
			ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
				(sockaddr*)&from, &fromLen);
			if (n <= 0)
				continue;
			buf[n] = 0;

			try {
				JsonValue msg = JsonValue::Parse(std::string(buf, n));
				if (!msg.Has("alias") || !msg.Has("fingerprint"))
					continue;
				if (msg.At("fingerprint").AsString() == fInfo.fingerprint)
					continue;

				DiscoveredDevice dev;
				dev.alias = msg.At("alias").AsString();
				dev.fingerprint = msg.At("fingerprint").AsString();
				dev.host = inet_ntoa(from.sin_addr);
				dev.port = msg.Has("port")
					? static_cast<int>(msg.At("port").AsInt(kDefaultPort))
					: kDefaultPort;
				dev.deviceType = msg.Has("deviceType")
					? msg.At("deviceType").AsString() : "unknown";

				if (fWindow)
					fWindow->AddDevice(dev);
			} catch (...) {
			}
		}
		close(fd);
	});
}


// --- main ------------------------------------------------------------------

int
main()
{
	LocalSendApp app;
	app.Run();
	return 0;
}
