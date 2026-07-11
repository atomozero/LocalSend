// Replicant desktop di LocalSend: zona di drop trascinabile sul desktop.
// La classe vive in DesktopReplicant.cpp, compilato sia nell'app (per
// l'anteprima con BDragger nelle Impostazioni) sia nel .so LocalSendDesktop
// che Tracker carica quando ripristina il replicant dal desktop.
#ifndef _LOCALSEND_DESKTOP_REPLICANT_H
#define _LOCALSEND_DESKTOP_REPLICANT_H

#include <View.h>

// Crea la drop-zone (lato x lato) con la maniglia BDragger gia' attaccata
// nell'angolo in basso a destra: e' il pezzo che l'utente trascina sul
// desktop. Dimensioni esplicite gia' impostate per l'uso nei layout.
BView* NewLocalSendDropView(float side);

#endif // _LOCALSEND_DESKTOP_REPLICANT_H
