// Orchestrazione del mittente (L0): prepare-upload -> upload (per file) -> cancel.
// Tiene sessionId + mappa fileId->token. La stessa nozione servira' al ricevente
// in L1, lato opposto (li genera e li valida).
#pragma once

#include <string>
#include <vector>

#include "net/IHttpClient.h"
#include "protocol/Models.h"

namespace ls {

struct FileOutcome {
    std::string fileId;
    std::string fileName;
    enum class Status { Sent, Rejected, Error } status = Status::Error;
    std::string detail;
};

struct SendReport {
    bool prepared = false;
    int  prepareStatus = 0;       // codice HTTP del prepare-upload
    std::string sessionId;
    std::vector<FileOutcome> files;
    bool allSent() const;
};

class UploadSession {
public:
    UploadSession(IHttpClient& http, DeviceInfo info);

    // Invia i file al ricevente host:port. 'pin' opzionale (vuoto = nessuno).
    SendReport send(const std::string& host, int port,
                    const std::vector<FileMetadata>& files,
                    const std::string& pin = "");

private:
    void cancel(const std::string& host, int port, const std::string& sessionId);

    IHttpClient& http_;
    DeviceInfo   info_;
};

} // namespace ls
