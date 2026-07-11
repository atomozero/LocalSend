// Replicant desktop di LocalSend: una drop-zone sul desktop. Trascini file
// sopra l'icona e vengono pubblicati in bacheca (o, con Shift, inviati
// subito alla cerchia dei preferiti online). Il desktop e' uno shelf di
// Tracker: la view viene archiviata li' e ricaricata a ogni riavvio dal
// .so LocalSendDesktop (campo "add_on" della archive).
//
// REGOLA FERREA: questa view gira dentro il team di Tracker. Niente rete,
// niente thread, niente lavoro pesante: solo disegno e BMessage verso
// l'app LocalSend, che fa tutto il resto. Se l'app non risponde, il
// replicant degrada a icona "spenta" e ritenta da solo (watchdog a 5 s).

#include <Bitmap.h>
#include <Dragger.h>
#include <Entry.h>
#include <Font.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <MimeType.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <View.h>
#include <Window.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "replicant/DesktopReplicant.h"

// Signature dell'app principale: icona dal MIME DB + destinatario dei messaggi.
static const char* kAppSignature = "application/x-vnd.LocalSend";

// Signature di QUESTO .so: finisce nel campo "add_on" dell'archive, cosi'
// Tracker sa quale add-on caricare quando ripristina il desktop.
static const char* kReplicantSignature = "application/x-vnd.LocalSend-Desktop";

static const char* kClassName = "LocalSendDropView";
static const char* kItemName = "LocalSendDropZone";

// Protocollo app <-> replicant: i codici DEVONO coincidere con l'enum di
// main_gui.cpp (Fase 0). Il replicant si iscrive con BSUB, riceve BUPD a
// ogni cambiamento, inoltra i drop con BDRP.
static const uint32 kMsgReplicantSubscribe		= 'BSUB';
static const uint32 kMsgReplicantUnsubscribe	= 'BUNS';
static const uint32 kMsgBoardUpdate				= 'BUPD';
static const uint32 kMsgReplicantDrop			= 'BDRP';
static const uint32 kMsgOpenBoard				= 'BOPN';
static const uint32 kMsgStopShare				= 'SSHR';
static const uint32 kMsgQuitApp					= 'QFTR';

// Interno: tick del watchdog di connessione (BMessageRunner).
static const uint32 kMsgCheckApp = 'CHKA';
static const bigtime_t kCheckIntervalUs = 5000000; // 5 s


// Inoltra un messaggio all'app. Se non gira e launchIfNotRunning e' true,
// la lancia consegnandole il messaggio: arrivera' al suo MessageReceived
// dopo ReadyToRun, quando la finestra esiste gia'.
static bool
SendToApp(BMessage* msg, bool launchIfNotRunning)
{
	BMessenger app(kAppSignature);
	if (app.IsValid())
		return app.SendMessage(msg) == B_OK;
	if (!launchIfNotRunning)
		return false;
	return be_roster->Launch(kAppSignature, msg) == B_OK;
}


class LocalSendDropView : public BView {
public:
	LocalSendDropView(BRect frame);
	LocalSendDropView(BMessage* archive);
	virtual ~LocalSendDropView();

	static LocalSendDropView* Instantiate(BMessage* archive);
	virtual status_t Archive(BMessage* into, bool deep = true) const;

	virtual void AttachedToWindow();
	virtual void DetachedFromWindow();
	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void MouseMoved(BPoint where, uint32 transit,
		const BMessage* dragMessage);
	virtual void MessageReceived(BMessage* message);
	virtual void GetPreferredSize(float* width, float* height);

private:
	void _LoadIcon();
	void _Subscribe();
	void _Unsubscribe();

	BBitmap* fIcon;
	BMessageRunner* fCheckRunner;

	// Ultimo snapshot ricevuto dall'app (kMsgBoardUpdate).
	int32 fBoardCount;
	bool fBoardActive;
	int32 fPeersOnline;
	int32 fFavsOnline;

	// Stato locale del replicant.
	bool fConnected;	// true dal primo BUPD; false se l'app sparisce
	bool fDragOver;		// un drag di Tracker sta sorvolando la view
};


LocalSendDropView::LocalSendDropView(BRect frame)
	:
	BView(frame, kItemName, B_FOLLOW_NONE,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND),
	fIcon(nullptr),
	fCheckRunner(nullptr),
	fBoardCount(0),
	fBoardActive(false),
	fPeersOnline(0),
	fFavsOnline(0),
	fConnected(false),
	fDragOver(false)
{
}


LocalSendDropView::LocalSendDropView(BMessage* archive)
	:
	BView(archive),
	fIcon(nullptr),
	fCheckRunner(nullptr),
	fBoardCount(0),
	fBoardActive(false),
	fPeersOnline(0),
	fFavsOnline(0),
	fConnected(false),
	fDragOver(false)
{
}


LocalSendDropView::~LocalSendDropView()
{
	delete fIcon;
}


LocalSendDropView*
LocalSendDropView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kClassName))
		return nullptr;
	return new LocalSendDropView(archive);
}


status_t
LocalSendDropView::Archive(BMessage* into, bool deep) const
{
	// deep=true archivia anche il BDragger figlio: sul desktop la maniglia
	// resta e il suo menu offre il "Remove replicant" per toglierlo.
	status_t err = BView::Archive(into, deep);
	if (err != B_OK)
		return err;
	into->AddString("add_on", kReplicantSignature);
	into->AddString("class", kClassName);
	return B_OK;
}


void
LocalSendDropView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(B_TRANSPARENT_COLOR);
	_LoadIcon();

	// Watchdog: ogni 5 s verifica che l'app risponda ancora e, se e'
	// (ri)apparsa, si re-iscrive. Nessun polling di stato: lo stato
	// arriva in push con kMsgBoardUpdate.
	BMessage check(kMsgCheckApp);
	fCheckRunner = new BMessageRunner(BMessenger(this), &check,
		kCheckIntervalUs);

	_Subscribe();
}


void
LocalSendDropView::DetachedFromWindow()
{
	delete fCheckRunner;
	fCheckRunner = nullptr;
	_Unsubscribe();
	BView::DetachedFromWindow();
}


void
LocalSendDropView::_LoadIcon()
{
	delete fIcon;
	fIcon = nullptr;

	// Icona HVIF dell'app dal MIME database, renderizzata alla dimensione
	// corrente della view: vettoriale, scala pulita a qualunque misura.
	BMimeType type(kAppSignature);
	uint8* data = nullptr;
	size_t size = 0;
	if (type.GetIcon(&data, &size) != B_OK || data == nullptr)
		return;

	BBitmap* bm = new BBitmap(Bounds(), B_RGBA32);
	if (BIconUtils::GetVectorIcon(data, size, bm) != B_OK) {
		delete bm;
		free(data);
		return;
	}
	free(data);
	fIcon = bm;
}


void
LocalSendDropView::_Subscribe()
{
	BMessenger app(kAppSignature);
	if (!app.IsValid()) {
		if (fConnected) {
			fConnected = false;
			Invalidate();
		}
		return;
	}
	BMessage sub(kMsgReplicantSubscribe);
	sub.AddMessenger("target", BMessenger(this));
	app.SendMessage(&sub);
	// fConnected diventa true al primo kMsgBoardUpdate ricevuto.
}


void
LocalSendDropView::_Unsubscribe()
{
	BMessenger app(kAppSignature);
	if (!app.IsValid())
		return;
	BMessage uns(kMsgReplicantUnsubscribe);
	uns.AddMessenger("target", BMessenger(this));
	app.SendMessage(&uns);
}


void
LocalSendDropView::Draw(BRect)
{
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	if (fIcon)
		DrawBitmap(fIcon, Bounds().LeftTop());

	BRect b = Bounds();

	// Bordo evidenziato mentre un drag sorvola la view.
	if (fDragOver) {
		SetHighColor(60, 140, 220, 230);
		SetPenSize(2);
		StrokeRoundRect(b.InsetByCopy(1, 1), 6, 6);
		SetPenSize(1);
	}

	// Scritta "Drop here" centrata in basso: ombra scura + testo chiaro,
	// leggibile su qualunque sfondo del desktop.
	{
		BFont font(be_plain_font);
		float fs = b.Height() / 9;
		if (fs < 8)
			fs = 8;
		if (fs > 11)
			fs = 11;
		font.SetSize(fs);
		SetFont(&font);
		const char* hint = "Drop here";
		float x = b.left + (b.Width() - StringWidth(hint)) / 2;
		float y = b.bottom - 3;
		SetHighColor(0, 0, 0, 180);
		DrawString(hint, BPoint(x + 1, y + 1));
		SetHighColor(255, 255, 255, 235);
		DrawString(hint, BPoint(x, y));
	}

	if (!fConnected) {
		// App non in esecuzione o non ancora iscritti: pallino grigio.
		SetHighColor(130, 130, 130, 220);
		FillEllipse(BRect(b.right - 9, b.top + 1, b.right - 1, b.top + 9));
		return;
	}

	// Badge con il numero di file in bacheca.
	if (fBoardCount > 0) {
		BRect badge(b.right - 17, b.top + 1, b.right - 1, b.top + 17);
		SetHighColor(60, 140, 220, 240);
		FillEllipse(badge);
		SetHighColor(255, 255, 255, 255);
		char txt[8];
		if (fBoardCount > 9)
			strcpy(txt, "9+");
		else
			snprintf(txt, sizeof(txt), "%d",
				(int)(fBoardCount < 0 ? 0 : fBoardCount));
		BFont font(be_bold_font);
		font.SetSize(10);
		SetFont(&font);
		font_height fh;
		font.GetHeight(&fh);
		float cx = badge.left + badge.Width() / 2 - StringWidth(txt) / 2;
		float cy = badge.top + badge.Height() / 2
			+ (fh.ascent - fh.descent) / 2;
		DrawString(txt, BPoint(cx, cy));
	}

	// Pallini verdi: preferiti online (la cerchia), max 5. Appena sopra
	// la scritta "Drop here" per non sovrapporsi.
	int32 dots = fFavsOnline > 5 ? 5 : fFavsOnline;
	SetHighColor(70, 180, 90, 240);
	for (int32 i = 0; i < dots; i++) {
		float x = b.left + 2 + i * 7;
		FillEllipse(BRect(x, b.bottom - 19, x + 5, b.bottom - 14));
	}
}


void
LocalSendDropView::MouseDown(BPoint where)
{
	BMessage* msg = Window() ? Window()->CurrentMessage() : nullptr;
	int32 buttons = 0;
	if (msg != nullptr)
		msg->FindInt32("buttons", &buttons);

	if (buttons & (B_SECONDARY_MOUSE_BUTTON | B_TERTIARY_MOUSE_BUTTON)) {
		BPopUpMenu* menu = new BPopUpMenu("localsend-drop", false, false);
		menu->SetAsyncAutoDestruct(true);
		menu->AddItem(new BMenuItem("Open LocalSend",
			new BMessage(kMsgOpenBoard)));
		BMenuItem* stopItem = new BMenuItem("Stop board sharing",
			new BMessage(kMsgStopShare));
		stopItem->SetEnabled(fBoardActive);
		menu->AddItem(stopItem);
		menu->AddSeparatorItem();
		menu->AddItem(new BMenuItem("Quit LocalSend",
			new BMessage(kMsgQuitApp)));
		menu->SetTargetForItems(BMessenger(this));

		BPoint screenPoint = ConvertToScreen(where);
		menu->Go(screenPoint, true, true, true);
		return;
	}

	// Click sinistro: apri/porta in primo piano la GUI (bacheca in Fase 3).
	BMessage open(kMsgOpenBoard);
	SendToApp(&open, true);
}


void
LocalSendDropView::MouseMoved(BPoint, uint32 transit,
	const BMessage* dragMessage)
{
	// Evidenzia solo i drag veri (dragMessage != nullptr), non il mouse.
	bool over = dragMessage != nullptr
		&& (transit == B_ENTERED_VIEW || transit == B_INSIDE_VIEW);
	if (over != fDragOver) {
		fDragOver = over;
		Invalidate();
	}
}


void
LocalSendDropView::MessageReceived(BMessage* message)
{
	// Drop dal Tracker: refs -> app. Default bacheca; con Shift premuto
	// al momento del rilascio, invio diretto alla cerchia (Fase 1).
	if (message->WasDropped() && message->HasRef("refs")) {
		fDragOver = false;
		Invalidate();

		BMessage drop(kMsgReplicantDrop);
		drop.AddBool("circle", (modifiers() & B_SHIFT_KEY) != 0);
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++)
			drop.AddRef("refs", &ref);
		SendToApp(&drop, true);
		return;
	}

	switch (message->what) {
		case kMsgBoardUpdate:
		{
			message->FindInt32("count", &fBoardCount);
			bool active = false;
			message->FindBool("active", &active);
			fBoardActive = active;
			message->FindInt32("peers", &fPeersOnline);
			message->FindInt32("favonline", &fFavsOnline);
			fConnected = true;
			Invalidate();
			return;
		}

		case kMsgCheckApp:
		{
			// Watchdog: l'app c'e' ancora? Se e' sparita, icona "spenta";
			// se e' ricomparsa dopo un'assenza, re-iscriviti.
			BMessenger app(kAppSignature);
			if (!app.IsValid()) {
				if (fConnected) {
					fConnected = false;
					Invalidate();
				}
			} else if (!fConnected) {
				_Subscribe();
			}
			return;
		}

		case kMsgOpenBoard:
		{
			BMessage open(kMsgOpenBoard);
			SendToApp(&open, true);
			return;
		}

		case kMsgStopShare:
		{
			BMessage stop(kMsgStopShare);
			SendToApp(&stop, false);
			return;
		}

		case kMsgQuitApp:
		{
			BMessage quit(kMsgQuitApp);
			SendToApp(&quit, false);
			return;
		}
	}
	BView::MessageReceived(message);
}


void
LocalSendDropView::GetPreferredSize(float* width, float* height)
{
	BRect b = Bounds();
	*width = b.Width();
	*height = b.Height();
}


// --- Factory per l'app (anteprima trascinabile nelle Impostazioni) ----------

BView*
NewLocalSendDropView(float side)
{
	BRect frame(0, 0, side - 1, side - 1);
	LocalSendDropView* view = new LocalSendDropView(frame);

	// Maniglia BDragger nell'angolo in basso a destra: e' quella che
	// l'utente trascina sul desktop (con "Show replicants" attivo nella
	// Deskbar). Figlia della view: viene archiviata insieme a lei.
	BRect dragRect(frame.right - 7, frame.bottom - 7,
		frame.right, frame.bottom);
	view->AddChild(new BDragger(dragRect, view,
		B_FOLLOW_RIGHT | B_FOLLOW_BOTTOM));

	// Dimensioni esplicite: la view a frame fisso convive con il layout kit.
	view->SetExplicitMinSize(BSize(side, side));
	view->SetExplicitMaxSize(BSize(side, side));
	return view;
}
