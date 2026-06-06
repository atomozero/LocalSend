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
    // Lato ricevente (L1): legge il blocco "info" inviato dal mittente.
    static DeviceInfo fromJson(const JsonValue& v);
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

    std::string localPath; // non serializzato: da dove leggere i byte (mittente)

    JsonValue toJson() const;
    // Lato ricevente (L1): legge i metadati di un file dalla richiesta.
    static FileMetadata fromJson(const JsonValue& v);
};

// Costruisce il body completo del prepare-upload (mittente, L0).
JsonValue buildPrepareUpload(const DeviceInfo& info,
                             const std::vector<FileMetadata>& files);

// Esito del prepare-upload: sessionId + token per i file ACCETTATI.
// Struttura SIMMETRICA: il mittente la OTTIENE dalla risposta, il ricevente la
// GENERA prima di rispondere. Stessa forma, lati opposti.
struct PrepareUploadResult {
    std::string sessionId;
    std::map<std::string, std::string> fileTokens; // fileId -> token
};

// Mittente (L0): legge la risposta del ricevente.
PrepareUploadResult parsePrepareUploadResponse(const std::string& body);

// --- Lato ricevente (L1) -------------------------------------------------

// Richiesta prepare-upload come la vede il ricevente: chi invia + cosa invia.
struct IncomingPrepareUpload {
    DeviceInfo                sender;
    std::vector<FileMetadata> files; // file richiesti, nell'ordine di arrivo
};

// Ricevente (L1): legge il body del prepare-upload ricevuto dal mittente.
IncomingPrepareUpload parsePrepareUploadRequest(const std::string& body);

// Ricevente (L1): costruisce il body della risposta (sessionId + token dei file
// accettati). Inverso di parsePrepareUploadResponse.
JsonValue buildPrepareUploadResponse(const std::string& sessionId,
                                     const std::map<std::string, std::string>& fileTokens);

} // namespace ls
