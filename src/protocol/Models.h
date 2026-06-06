// Modelli del protocollo LocalSend, indipendenti dal trasporto.
// Condivisi tra mittente (L0) e futuro ricevente (L1): il ricevente generera'
// sessionId/token con le stesse strutture, lato opposto.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "protocol/Json.h"

namespace ls {

// L'oggetto "info" del prepare-upload (identita' del device).
struct DeviceInfo {
    std::string alias       = "Haiku Box";
    std::string version     = "2.1";
    std::string deviceModel = "Haiku";
    std::string deviceType  = "desktop"; // fallback per valori sconosciuti
    std::string fingerprint;             // in HTTP: stringa casuale persistente
    int         port        = 53317;
    std::string protocol    = "http";    // "https" da L3
    bool        download    = false;

    JsonValue toJson() const;
};

// Metadati di un file nel prepare-upload.
struct FileMetadata {
    std::string id;
    std::string fileName;
    long long   size = 0;
    std::string fileType = "application/octet-stream";
    std::string sha256;   // vuoto -> null
    std::string preview;  // vuoto -> null
    std::string modified; // ISO 8601 UTC, vuoto -> omesso
    std::string accessed; // ISO 8601 UTC, vuoto -> omesso

    std::string localPath; // non serializzato: da dove leggere i byte

    JsonValue toJson() const;
};

// Costruisce il body completo del prepare-upload.
JsonValue buildPrepareUpload(const DeviceInfo& info,
                             const std::vector<FileMetadata>& files);

// Esito del prepare-upload: sessionId + token per i file ACCETTATI.
struct PrepareUploadResult {
    std::string sessionId;
    std::map<std::string, std::string> fileTokens; // fileId -> token
};

PrepareUploadResult parsePrepareUploadResponse(const std::string& body);

} // namespace ls
