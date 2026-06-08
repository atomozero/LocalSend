#include "client/UploadSession.h"

#include <utility>

#include "protocol/Constants.h"

namespace LocalSend {

bool
SendReport::AllSent() const
{
	if (files.empty())
		return false;
	for (const auto& f : files) {
		if (f.status != FileOutcome::Status::Sent)
			return false;
	}
	return true;
}


UploadSession::UploadSession(IHttpClient& http, DeviceInfo info)
	:
	fHttp(http),
	fInfo(std::move(info))
{
}


void
UploadSession::Cancel(const std::string& host, int port,
	const std::string& sessionId)
{
	std::string path = std::string(kApiCancel) + "?sessionId="
		+ UrlEncode(sessionId);
	fHttp.Post(host, port, path, "application/json", "");
}


SendReport
UploadSession::Send(const std::string& host, int port,
	const std::vector<FileMetadata>& files, const std::string& pin,
	ProgressFn progress)
{
	SendReport report;

	// Passo 1: prepare-upload (solo metadati).
	std::string preparePath = kApiPrepareUpload;
	if (!pin.empty())
		preparePath += "?pin=" + UrlEncode(pin);

	JsonValue body = BuildPrepareUpload(fInfo, files);
	HttpResponse resp = fHttp.Post(host, port, preparePath, "application/json",
		body.Dump());
	report.prepareStatus = resp.status;

	if (!resp.IsOk()) {
		// 204 = nessun file accettato; 401/403 = PIN/rifiuto; 409 = sessione in
		// corso; 429 = troppe richieste; altri = errore.
		return report;
	}

	PrepareUploadResult prep = ParsePrepareUploadResponse(resp.body);
	report.prepared = true;
	report.sessionId = prep.sessionId;

	// Passo 2: upload per ogni file. I file assenti in risposta = rifiutati.
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

		std::string path = std::string(kApiUpload)
			+ "?sessionId=" + UrlEncode(prep.sessionId)
			+ "&fileId=" + UrlEncode(f.id)
			+ "&token=" + UrlEncode(it->second);

		HttpResponse up = fHttp.PostFile(host, port, path, f.fileType,
			f.localPath);
		if (up.IsOk()) {
			out.status = FileOutcome::Status::Sent;
		} else {
			out.status = FileOutcome::Status::Error;
			out.detail = up.status == 0
				? "connessione/IO fallita"
				: ("HTTP " + std::to_string(up.status));
			anyError = true;
		}
		report.files.push_back(out);

		if (progress)
			progress(static_cast<int>(report.files.size()));
	}

	// Passo 3: in caso di errore a meta' sessione, annulla.
	if (anyError && !prep.sessionId.empty())
		Cancel(host, port, prep.sessionId);

	return report;
}

} // namespace LocalSend
