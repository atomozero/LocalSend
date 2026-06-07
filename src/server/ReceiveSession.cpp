#include "server/ReceiveSession.h"

#include <utility>

#include "protocol/Fingerprint.h"

namespace LocalSend {

ReceiveSession::ReceiveSession(TokenGen gen)
	:
	fGenerator(gen ? std::move(gen) : TokenGen(&GenerateFingerprint))
{
}


PrepareOutcome
ReceiveSession::Prepare(const IncomingPrepareUpload& req,
	const AcceptPolicy& accept)
{
	if (fActive)
		return PrepareOutcome{PrepareStatus::SessionBusy, {}};

	std::map<std::string, Entry> entries;
	PrepareUploadResult result;
	for (const auto& f : req.files) {
		if (accept && !accept(f))
			continue; // file rifiutato: niente token
		std::string token = fGenerator();
		result.fileTokens[f.id] = token;
		entries[f.id] = Entry{f, token, false};
	}

	if (entries.empty())
		return PrepareOutcome{PrepareStatus::NothingAccepted, {}};

	// Almeno un file accettato: apri la sessione.
	fSessionId = fGenerator();
	result.sessionId = fSessionId;
	fEntries = std::move(entries);
	fActive = true;
	return PrepareOutcome{PrepareStatus::Accepted, result};
}


bool
ReceiveSession::ValidateUpload(const std::string& sessionId,
	const std::string& fileId, const std::string& token) const
{
	if (!fActive || sessionId != fSessionId)
		return false;
	auto it = fEntries.find(fileId);
	if (it == fEntries.end())
		return false;
	if (it->second.received)
		return false; // gia' ricevuto: niente doppioni
	return token == it->second.token;
}


bool
ReceiveSession::MarkReceived(const std::string& fileId)
{
	auto it = fEntries.find(fileId);
	if (it == fEntries.end())
		return false;
	it->second.received = true;
	return true;
}


bool
ReceiveSession::IsComplete() const
{
	if (!fActive || fEntries.empty())
		return false;
	for (const auto& kv : fEntries) {
		if (!kv.second.received)
			return false;
	}
	return true;
}


bool
ReceiveSession::Cancel(const std::string& sessionId)
{
	if (!fActive || sessionId != fSessionId)
		return false;
	Reset();
	return true;
}


void
ReceiveSession::Reset()
{
	fActive = false;
	fSessionId.clear();
	fEntries.clear();
}


const FileMetadata*
ReceiveSession::File(const std::string& fileId) const
{
	auto it = fEntries.find(fileId);
	return it == fEntries.end() ? nullptr : &it->second.meta;
}

} // namespace LocalSend
