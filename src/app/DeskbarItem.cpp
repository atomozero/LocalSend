// Gestione dell'item LocalSend nella Deskbar via replicant add-on (.so).
// La BView vera vive in src/replicant/DeskbarReplicant.cpp, compilato come
// shared library "LocalSendDeskbar" e installato nella Deskbar via entry_ref:
// in questo modo la Deskbar chiama instantiate_deskbar_item(maxW, maxH) con
// l'altezza reale del cassetto e l'icona si adatta da sola.

#include "app/DeskbarItem.h"

#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

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


// --- Autostart al login ----------------------------------------------------

static const char* kAutostartFileName = "localsend";


// Path completo dello script di autostart: ~/config/settings/boot/launch/localsend
static status_t
AutostartPath(BPath* outPath)
{
	BPath base;
	status_t err = find_directory(B_USER_SETTINGS_DIRECTORY, &base);
	if (err != B_OK)
		return err;
	err = outPath->SetTo(base.Path(), "boot/launch");
	if (err != B_OK)
		return err;
	// Garantisce che la cartella esista (al primo uso potrebbe mancare).
	create_directory(outPath->Path(), 0755);
	return outPath->Append(kAutostartFileName);
}


// Path assoluto dell'eseguibile corrente (LocalSend).
static status_t
CurrentExecutablePath(BPath* outPath)
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &info) == B_OK) {
		if (info.type != B_APP_IMAGE)
			continue;
		return outPath->SetTo(info.name);
	}
	return B_ENTRY_NOT_FOUND;
}


bool
IsAutostartEnabled()
{
	BPath script;
	if (AutostartPath(&script) != B_OK)
		return false;
	BEntry entry(script.Path());
	return entry.Exists();
}


status_t
EnableAutostart()
{
	BPath script;
	status_t err = AutostartPath(&script);
	if (err != B_OK)
		return err;

	BPath exe;
	err = CurrentExecutablePath(&exe);
	if (err != B_OK)
		return err;

	// Lo script lancia l'eseguibile in background con --background cosi'
	// la finestra resta nascosta: solo il replicant Deskbar e' visibile,
	// e l'utente apre la GUI cliccandolo. Il "&" evita di bloccare il
	// boot sequence dei launch scripts.
	BFile out(script.Path(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	err = out.InitCheck();
	if (err != B_OK)
		return err;

	char buf[1024];
	int n = snprintf(buf, sizeof(buf),
		"#!/bin/sh\n"
		"# Autostart generato da LocalSend (Impostazioni > Integrazione).\n"
		"\"%s\" --background &\n",
		exe.Path());
	if (n < 0)
		return B_ERROR;

	ssize_t w = out.Write(buf, n);
	if (w < 0)
		return (status_t)w;

	// Lo script deve essere eseguibile, altrimenti i boot scripts lo ignorano.
	chmod(script.Path(), 0755);
	return B_OK;
}


status_t
DisableAutostart()
{
	BPath script;
	status_t err = AutostartPath(&script);
	if (err != B_OK)
		return err;
	BEntry entry(script.Path());
	if (!entry.Exists())
		return B_OK;
	return entry.Remove();
}

} // namespace LocalSend
