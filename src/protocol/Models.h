// Modelli del protocollo LocalSend, indipendenti dal trasporto.
// Condivisi tra mittente (L0) e ricevente (L1): il ricevente genera
// sessionId/token con le stesse strutture, lato opposto.
#ifndef _LOCALSEND_MODELS_H
#define _LOCALSEND_MODELS_H

#include <map>
#include <string>
#include <vector>

#include "protocol/Constants.h"
#include "protocol/Json.h"

namespace LocalSend {

// L'oggetto "info" del prepare-upload (identita' del device).
struct DeviceInfo {
	std::string alias = "Haiku Box";
	std::string version = "2.1";
	std::string deviceModel = "Haiku";
	std::string deviceType = "desktop"; // fallback per valori sconosciuti
	// Estensione Haiku: identifica la nostra app (kAppId). In uscita e'
	// sempre valorizzato; in ingresso (FromJson) resta vuoto se il peer non
	// lo manda, cosi' non ci sono falsi positivi dal default.
	std::string app = kAppId;
	std::string fingerprint;            // in HTTP: stringa casuale persistente
	int port = 53317;
	std::string protocol = "http";      // "https" da L3
	bool download = false;
	// Estensione Haiku: revisione della bacheca condivisa (0 = nessuna).
	// Emesso nel JSON solo se > 0; i client LocalSend ufficiali ignorano
	// i campi sconosciuti, quindi l'annuncio resta compatibile.
	int boardRev = 0;

	JsonValue ToJson() const;
	// Lato ricevente (L1): legge il blocco "info" inviato dal mittente.
	static DeviceInfo FromJson(const JsonValue& v);
};

// Metadati di un file nel prepare-upload.
struct FileMetadata {
	std::string id;
	std::string fileName;
	long long size = 0;
	std::string fileType = "application/octet-stream";
	std::string sha256;   // vuoto -> null
	std::string preview;  // vuoto -> null
	std::string modified; // ISO 8601 UTC, vuoto -> omesso
	std::string accessed; // ISO 8601 UTC, vuoto -> omesso

	std::string localPath; // non serializzato: sorgente dei byte (mittente)

	JsonValue ToJson() const;
	// Lato ricevente (L1): legge i metadati di un file dalla richiesta.
	static FileMetadata FromJson(const JsonValue& v);
};

// Costruisce il body completo del prepare-upload (mittente, L0).
JsonValue BuildPrepareUpload(const DeviceInfo& info,
	const std::vector<FileMetadata>& files);

// Esito del prepare-upload: sessionId + token per i file ACCETTATI.
// Struttura SIMMETRICA: il mittente la OTTIENE dalla risposta, il ricevente la
// GENERA prima di rispondere. Stessa forma, lati opposti.
struct PrepareUploadResult {
	std::string sessionId;
	std::map<std::string, std::string> fileTokens; // fileId -> token
};

// Mittente (L0): legge la risposta del ricevente.
PrepareUploadResult ParsePrepareUploadResponse(const std::string& body);

// --- Lato ricevente (L1) -------------------------------------------------

// Richiesta prepare-upload come la vede il ricevente: chi invia + cosa invia.
struct IncomingPrepareUpload {
	DeviceInfo sender;
	std::vector<FileMetadata> files; // file richiesti, nell'ordine di arrivo
};

// Ricevente (L1): legge il body del prepare-upload ricevuto dal mittente.
IncomingPrepareUpload ParsePrepareUploadRequest(const std::string& body);

// Ricevente (L1): costruisce il body della risposta (sessionId + token dei file
// accettati). Inverso di ParsePrepareUploadResponse.
JsonValue BuildPrepareUploadResponse(const std::string& sessionId,
	const std::map<std::string, std::string>& fileTokens);

} // namespace LocalSend

#endif // _LOCALSEND_MODELS_H
