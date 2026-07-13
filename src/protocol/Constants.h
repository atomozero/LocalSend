// Costanti del protocollo LocalSend v2.1. Unico posto dove vivono porte e path.
// Condiviso tra mittente (L0) e ricevente (L1).
#ifndef _LOCALSEND_CONSTANTS_H
#define _LOCALSEND_CONSTANTS_H

namespace LocalSend {

constexpr const char* kMulticastGroup	= "224.0.0.167";
constexpr int		kDefaultPort		= 53317;
constexpr const char* kProtocolVersion	= "2.1";

// Marcatore (estensione Haiku) messo nel campo "app" dell'annuncio: permette
// di riconoscere con certezza un peer che gira la nostra app. I client
// ufficiali non lo inviano e ignorano il campo sconosciuto.
constexpr const char* kAppId			= "localsend-haiku";

constexpr const char* kApiPrepareUpload	= "/api/localsend/v2/prepare-upload";
constexpr const char* kApiUpload		= "/api/localsend/v2/upload";
constexpr const char* kApiCancel		= "/api/localsend/v2/cancel";
constexpr const char* kApiRegister		= "/api/localsend/v2/register";
constexpr const char* kApiInfo			= "/api/localsend/v2/info";
constexpr const char* kApiPrepareDownload = "/api/localsend/v2/prepare-download";
constexpr const char* kApiDownload		= "/api/localsend/v2/download";
constexpr int		kDownloadPort		= 53318;

} // namespace LocalSend

#endif // _LOCALSEND_CONSTANTS_H
