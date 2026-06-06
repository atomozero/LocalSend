#include "client/UploadSession.h"

#include <utility>

#include "protocol/Constants.h"

namespace ls {

bool SendReport::allSent() const {
    if (files.empty()) return false;
    for (const auto& f : files)
        if (f.status != FileOutcome::Status::Sent) return false;
    return true;
}

UploadSession::UploadSession(IHttpClient& http, DeviceInfo info)
    : http_(http), info_(std::move(info)) {}

void UploadSession::cancel(const std::string& host, int port,
                           const std::string& sessionId) {
    std::string path = std::string(kApiCancel) + "?sessionId=" + urlEncode(sessionId);
    http_.post(host, port, path, "application/json", "");
}

SendReport UploadSession::send(const std::string& host, int port,
                               const std::vector<FileMetadata>& files,
                               const std::string& pin) {
    SendReport report;

    // Passo 1: prepare-upload (solo metadati).
    std::string preparePath = kApiPrepareUpload;
    if (!pin.empty()) preparePath += "?pin=" + urlEncode(pin);

    JsonValue body = buildPrepareUpload(info_, files);
    HttpResponse resp = http_.post(host, port, preparePath, "application/json",
                                   body.dump());
    report.prepareStatus = resp.status;

    if (!resp.ok()) {
        // 204 = nessun file accettato; 401/403 = PIN/rifiuto; 409 = sessione in
        // corso; 429 = troppe richieste; altri = errore.
        return report;
    }

    PrepareUploadResult prep = parsePrepareUploadResponse(resp.body);
    report.prepared = true;
    report.sessionId = prep.sessionId;

    // Passo 2: upload per ogni file. I file assenti dalla risposta sono rifiutati.
    bool anyError = false;
    for (const auto& f : files) {
        FileOutcome out;
        out.fileId = f.id;
        out.fileName = f.fileName;

        auto it = prep.fileTokens.find(f.id);
        if (it == prep.fileTokens.end()) {
            out.status = FileOutcome::Status::Rejected;
            out.detail = "non accettato dal ricevente";
            report.files.push_back(out);
            continue;
        }

        std::string path = std::string(kApiUpload) +
                           "?sessionId=" + urlEncode(prep.sessionId) +
                           "&fileId=" + urlEncode(f.id) +
                           "&token=" + urlEncode(it->second);

        HttpResponse up = http_.postFile(host, port, path, f.fileType, f.localPath);
        if (up.ok()) {
            out.status = FileOutcome::Status::Sent;
        } else {
            out.status = FileOutcome::Status::Error;
            out.detail = up.status == 0 ? "connessione/IO fallita"
                                        : ("HTTP " + std::to_string(up.status));
            anyError = true;
        }
        report.files.push_back(out);
    }

    // Passo 3: in caso di errore a meta' sessione, annulla.
    if (anyError && !prep.sessionId.empty())
        cancel(host, port, prep.sessionId);

    return report;
}

} // namespace ls
