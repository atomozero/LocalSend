// Gestione dell'item LocalSend nella Deskbar.
//
// L'item e' un replicant add-on (.so separato, file LocalSendDeskbar nella
// stessa cartella dell'eseguibile). Cosi' la Deskbar chiama
// instantiate_deskbar_item(maxW, maxH) con l'altezza reale del cassetto e
// l'icona scala dinamicamente (vedi src/replicant/DeskbarReplicant.cpp).
#ifndef _LOCALSEND_DESKBAR_ITEM_H
#define _LOCALSEND_DESKBAR_ITEM_H

#include <SupportDefs.h>

namespace LocalSend {

// Nome dell'item nella Deskbar. Univoco: il Deskbar non ammette duplicati.
extern const char* kDeskbarItemName;

// Controllo Deskbar: chiede se la nostra icona e' gia' presente.
bool IsDeskbarItemInstalled();

// Aggiunge l'icona al Deskbar caricando il .so del replicant.
// Ritorna B_OK in caso di successo.
status_t InstallDeskbarItem();

// Rimuove l'icona dal Deskbar.
status_t RemoveDeskbarItem();

// --- Autostart al login ----------------------------------------------------
//
// Su Haiku gli script in ~/config/settings/boot/launch/ vengono eseguiti
// dall'utente al login. Per il pattern "tray app" lanciamo l'eseguibile
// con --background cosi' la finestra parte nascosta e l'utente la richiama
// dal replicant.

bool IsAutostartEnabled();
status_t EnableAutostart();
status_t DisableAutostart();

} // namespace LocalSend

#endif // _LOCALSEND_DESKBAR_ITEM_H
