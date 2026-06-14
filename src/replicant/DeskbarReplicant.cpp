// Add-on Deskbar di LocalSend: piccola icona vicino all'orologio. Costruito
// come .so separato e installato via BDeskbar::AddItem(entry_ref) cosi' che
// la Deskbar chiami instantiate_deskbar_item(maxW, maxH) con l'altezza reale
// del cassetto. La view si dimensiona di conseguenza e l'icona HVIF, essendo
// vettoriale, scala pulita a qualunque misura.

#include <Bitmap.h>
#include <IconUtils.h>
#include <MimeType.h>
#include <Roster.h>
#include <View.h>

#include <cstdio>
#include <cstdlib>

// Signature dell'app principale: usata per caricare l'icona dal MIME DB
// e per lanciare la GUI con un click.
static const char* kAppSignature = "application/x-vnd.LocalSend";

// Signature di QUESTO .so: usata dal BShelf nel campo "add_on" cosi' che
// la Deskbar sappia quale add-on caricare al ripristino.
static const char* kReplicantSignature = "application/x-vnd.LocalSend-Deskbar";

static const char* kItemName = "LocalSendDeskbar";
static const char* kClassName = "LocalSendDeskbarView";


class LocalSendDeskbarView : public BView {
public:
	LocalSendDeskbarView(BRect frame);
	LocalSendDeskbarView(BMessage* archive);
	virtual ~LocalSendDeskbarView();

	static LocalSendDeskbarView* Instantiate(BMessage* archive);
	virtual status_t Archive(BMessage* into, bool deep = true) const;

	virtual void AttachedToWindow();
	virtual void Draw(BRect updateRect);
	virtual void MouseDown(BPoint where);
	virtual void GetPreferredSize(float* width, float* height);

private:
	void _LoadIcon();

	BBitmap* fIcon;
};


LocalSendDeskbarView::LocalSendDeskbarView(BRect frame)
	:
	BView(frame, kItemName, B_FOLLOW_NONE,
		B_WILL_DRAW | B_TRANSPARENT_BACKGROUND),
	fIcon(nullptr)
{
}


LocalSendDeskbarView::LocalSendDeskbarView(BMessage* archive)
	:
	BView(archive),
	fIcon(nullptr)
{
}


LocalSendDeskbarView::~LocalSendDeskbarView()
{
	delete fIcon;
}


LocalSendDeskbarView*
LocalSendDeskbarView::Instantiate(BMessage* archive)
{
	if (!validate_instantiation(archive, kClassName))
		return nullptr;
	return new LocalSendDeskbarView(archive);
}


status_t
LocalSendDeskbarView::Archive(BMessage* into, bool deep) const
{
	status_t err = BView::Archive(into, deep);
	if (err != B_OK)
		return err;
	// "add_on" indica alla Deskbar quale .so caricare al ripristino.
	// Deve essere la signature del replicant stesso, non quella dell'app.
	into->AddString("add_on", kReplicantSignature);
	into->AddString("class", kClassName);
	return B_OK;
}


void
LocalSendDeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (Parent())
		SetViewColor(Parent()->ViewColor());
	else
		SetViewColor(B_TRANSPARENT_COLOR);
	_LoadIcon();
}


void
LocalSendDeskbarView::_LoadIcon()
{
	delete fIcon;
	fIcon = nullptr;

	// Carica l'icona HVIF dell'app dal MIME database e renderizzala alla
	// dimensione corrente della view. Vettoriale: scala pulito a qualunque
	// misura. (B_MINI_ICON/B_LARGE_ICON sono bitmap a dimensione fissa.)
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
LocalSendDeskbarView::Draw(BRect)
{
	SetDrawingMode(B_OP_ALPHA);
	SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	if (fIcon)
		DrawBitmap(fIcon, Bounds().LeftTop());
}


void
LocalSendDeskbarView::MouseDown(BPoint)
{
	// Click: porta in primo piano la GUI (la lancia se assente).
	if (be_roster->Launch(kAppSignature) == B_OK)
		return;
	BMessenger msgr(kAppSignature);
	if (msgr.IsValid()) {
		BMessage bring(B_SILENT_RELAUNCH);
		msgr.SendMessage(&bring);
	}
}


void
LocalSendDeskbarView::GetPreferredSize(float* width, float* height)
{
	BRect b = Bounds();
	*width = b.Width();
	*height = b.Height();
}


static BView*
MakeView(float maxWidth, float maxHeight)
{
	float side = maxHeight;
	if (side < 1)
		side = 16;
	if (maxWidth > 0 && maxWidth < side)
		side = maxWidth;
	return new LocalSendDeskbarView(BRect(0, 0, side - 1, side - 1));
}


// Entry point preferito dalla Deskbar moderna.
extern "C" _EXPORT BView*
instantiate_deskbar_entry(image_id, const entry_ref*,
	float maxWidth, float maxHeight)
{
	return MakeView(maxWidth, maxHeight);
}


// Entry point legacy: compatibilita' con Deskbar piu' vecchi.
extern "C" _EXPORT BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return MakeView(maxWidth, maxHeight);
}
