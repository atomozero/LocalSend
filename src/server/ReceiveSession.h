// Orchestrazione del ricevente (L1): genera sessionId + token, valida gli upload
// in arrivo, traccia il completamento. Speculare a UploadSession (mittente, L0):
// stessa nozione di sessionId+token, lato opposto. NON dipende dai socket ne' da
// IHttpServer: e' logica di protocollo pura, quindi testabile senza rete.
#pragma once

#include <functional>
#include <map>
#include <string>

#include "protocol/Models.h"

namespace ls {

// Esito di un prepare-upload ricevuto, mappabile 1:1 su un codice HTTP:
//   Accepted        -> 200 + body (sessionId + token)
//   NothingAccepted -> 204 (nessun file accettato)
//   SessionBusy     -> 409 (un'altra sessione e' gia' in corso)
enum class PrepareStatus { Accepted, NothingAccepted, SessionBusy };

struct PrepareOutcome {
    PrepareStatus       status = PrepareStatus::NothingAccepted;
    PrepareUploadResult result; // valido solo se status == Accepted
};

class ReceiveSession {
public:
    // Genera i token (sessionId e token per file). Default: stringa casuale.
    // Iniettabile per test deterministici.
    using TokenGen = std::function<std::string()>;
    // Decide se accettare un file. Default (nullptr): accetta tutto.
    using AcceptPolicy = std::function<bool(const FileMetadata&)>;

    explicit ReceiveSession(TokenGen gen = nullptr);

    // Gestisce un prepare-upload in arrivo: sceglie i file accettati e assegna i
    // token. Crea una nuova sessione solo se almeno un file e' accettato.
    PrepareOutcome prepare(const IncomingPrepareUpload& req,
                           const AcceptPolicy& accept = nullptr);

    // Valida una richiesta di upload (sessionId + fileId + token coerenti con la
    // sessione attiva e file non gia' ricevuto).
    bool validateUpload(const std::string& sessionId,
                        const std::string& fileId,
                        const std::string& token) const;

    // Segna un file come ricevuto per intero. Ritorna false se sconosciuto.
    bool markReceived(const std::string& fileId);

    // Tutti i file accettati sono stati ricevuti.
    bool isComplete() const;

    // C'e' una sessione in corso (creata e non ancora annullata/azzerata).
    bool active() const { return active_; }

    // Annulla la sessione se l'id combacia (gestione di /cancel). Ritorna true se
    // qualcosa e' stato annullato.
    bool cancel(const std::string& sessionId);

    // Azzera lo stato per accettare una nuova sessione (es. dopo completamento).
    void reset();

    const std::string& sessionId() const { return sessionId_; }

    // Metadati del file accettato (nome, size, tipo), o nullptr se assente.
    const FileMetadata* file(const std::string& fileId) const;

private:
    struct Entry {
        FileMetadata meta;
        std::string  token;
        bool         received = false;
    };

    TokenGen                     gen_;
    bool                         active_ = false;
    std::string                  sessionId_;
    std::map<std::string, Entry> entries_; // fileId -> stato
};

} // namespace ls
