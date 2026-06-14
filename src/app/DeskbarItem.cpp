// Gestione dell'item LocalSend nella Deskbar via replicant add-on (.so).
// La BView vera vive in src/replicant/DeskbarReplicant.cpp, compilato come
// shared library "LocalSendDeskbar" e installato nella Deskbar via entry_ref:
// in questo modo la Deskbar chiama instantiate_deskbar_item(maxW, maxH) con
// l'altezza reale del cassetto e l'icona si adatta da sola.

#include "app/DeskbarItem.h"

#include <Deskbar.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <image.h>

namespace LocalSend {

const char* kDeskbarItemName = "LocalSendDeskbar";

static const char* kReplicantBinaryName = "LocalSendDeskbar";


// Trova il file LocalSendDeskbar accanto all'eseguibile in esecuzione.
// Ritorna B_ENTRY_NOT_FOUND se non e' presente.
static status_t
FindReplicantRef(entry_ref* outRef)
{
	// Path dell'eseguibile corrente.
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type != B_APP_IMAGE)
			continue;
		BPath exePath(info.name);
		if (exePath.InitCheck() != B_OK)
			break;
		BPath dir;
		if (exePath.GetParent(&dir) != B_OK)
			break;
		BPath replicantPath(dir.Path(), kReplicantBinaryName);
		return get_ref_for_path(replicantPath.Path(), outRef);
	}
	return B_ENTRY_NOT_FOUND;
}


bool
IsDeskbarItemInstalled()
{
	BDeskbar deskbar;
	return deskbar.HasItem(kDeskbarItemName);
}


status_t
InstallDeskbarItem()
{
	BDeskbar deskbar;
	if (deskbar.HasItem(kDeskbarItemName))
		return B_OK;

	entry_ref ref;
	status_t err = FindReplicantRef(&ref);
	if (err != B_OK) {
		fprintf(stderr, "Replicant non trovato (%s): %s\n",
			kReplicantBinaryName, strerror(err));
		return err;
	}

	BPath path(&ref);
	fprintf(stderr, "[deskbar] installo replicant da: %s\n", path.Path());

	int32 id = -1;
	err = deskbar.AddItem(&ref, &id);
	if (err != B_OK) {
		fprintf(stderr, "[deskbar] AddItem fallito: %s (codice %ld)\n",
			strerror(err), (long)err);
	} else {
		fprintf(stderr, "[deskbar] AddItem ok (id=%ld)\n", (long)id);
	}
	return err;
}


status_t
RemoveDeskbarItem()
{
	BDeskbar deskbar;
	if (!deskbar.HasItem(kDeskbarItemName))
		return B_OK;
	return deskbar.RemoveItem(kDeskbarItemName);
}

} // namespace LocalSend
