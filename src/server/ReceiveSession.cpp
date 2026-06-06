#include "server/ReceiveSession.h"

#include <utility>

#include "protocol/Fingerprint.h"

namespace ls {

ReceiveSession::ReceiveSession(TokenGen gen)
    : gen_(gen ? std::move(gen) : TokenGen(&generateFingerprint)) {}

PrepareOutcome ReceiveSession::prepare(const IncomingPrepareUpload& req,
                                       const AcceptPolicy& accept) {
    if (active_) return PrepareOutcome{PrepareStatus::SessionBusy, {}};

    std::map<std::string, Entry> entries;
    PrepareUploadResult result;
    for (const auto& f : req.files) {
        if (accept && !accept(f)) continue; // file rifiutato: niente token
        std::string token = gen_();
        result.fileTokens[f.id] = token;
        entries[f.id] = Entry{f, token, false};
    }

    if (entries.empty())
        return PrepareOutcome{PrepareStatus::NothingAccepted, {}};

    // Almeno un file accettato: apri la sessione.
    sessionId_ = gen_();
    result.sessionId = sessionId_;
    entries_ = std::move(entries);
    active_ = true;
    return PrepareOutcome{PrepareStatus::Accepted, result};
}

bool ReceiveSession::validateUpload(const std::string& sessionId,
                                    const std::string& fileId,
                                    const std::string& token) const {
    if (!active_ || sessionId != sessionId_) return false;
    auto it = entries_.find(fileId);
    if (it == entries_.end()) return false;
    if (it->second.received) return false; // gia' ricevuto: niente doppioni
    return token == it->second.token;
}

bool ReceiveSession::markReceived(const std::string& fileId) {
    auto it = entries_.find(fileId);
    if (it == entries_.end()) return false;
    it->second.received = true;
    return true;
}

bool ReceiveSession::isComplete() const {
    if (!active_ || entries_.empty()) return false;
    for (const auto& kv : entries_)
        if (!kv.second.received) return false;
    return true;
}

bool ReceiveSession::cancel(const std::string& sessionId) {
    if (!active_ || sessionId != sessionId_) return false;
    reset();
    return true;
}

void ReceiveSession::reset() {
    active_ = false;
    sessionId_.clear();
    entries_.clear();
}

const FileMetadata* ReceiveSession::file(const std::string& fileId) const {
    auto it = entries_.find(fileId);
    return it == entries_.end() ? nullptr : &it->second.meta;
}

} // namespace ls
