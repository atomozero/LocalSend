#include "client/UploadSession.h"

#include <atomic>
#include <thread>
#include <utility>

#include "net/haiku/SocketHttpClient.h"
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

	// Calcola il totale dei byte accettati per il progresso.
	long long totalBytes = 0;
	for (const auto& f : files) {
		if (prep.fileTokens.count(f.id))
			totalBytes += f.size;
	}

	// Separa file accettati e rifiutati.
	struct UploadTask {
		const FileMetadata* file;
		std::string token;
		FileOutcome outcome;
	};
	std::vector<UploadTask> tasks;
	for (const auto& f : files) {
		auto it = prep.fileTokens.find(f.id);
		if (it == prep.fileTokens.end()) {
			FileOutcome out;
			out.fileId = f.id;
			out.fileName = f.fileName;
			out.status = FileOutcome::Status::Rejected;
			out.detail = "non accettato dal ricevente";
			report.files.push_back(out);
		} else {
			UploadTask t;
			t.file = &f;
			t.token = it->second;
			t.outcome.fileId = f.id;
			t.outcome.fileName = f.fileName;
			tasks.push_back(t);
		}
	}

	// Passo 2: upload parallelo. Ogni thread crea la propria connessione.
	std::atomic<long long> sentBytes(0);
	bool anyError = false;

	auto uploadOne = [&](UploadTask& t) {
		std::string path = std::string(kApiUpload)
			+ "?sessionId=" + UrlEncode(prep.sessionId)
			+ "&fileId=" + UrlEncode(t.file->id)
			+ "&token=" + UrlEncode(t.token);

		TransferProgressFn fileProgress;
		if (progress) {
			fileProgress = [&](long long fileSent, long long) {
				progress(sentBytes + fileSent, totalBytes);
			};
		}

		SocketHttpClient http;
		http.EnableTls();
		HttpResponse up = http.PostFile(host, port, path,
			t.file->fileType, t.file->localPath, fileProgress);
		if (up.IsOk()) {
			t.outcome.status = FileOutcome::Status::Sent;
			sentBytes += t.file->size;
		} else {
			t.outcome.status = FileOutcome::Status::Error;
			t.outcome.detail = up.status == 0
				? "connessione/IO fallita"
				: ("HTTP " + std::to_string(up.status));
			anyError = true;
		}
	};

	if (tasks.size() == 1) {
		// Un solo file: niente thread extra.
		uploadOne(tasks[0]);
	} else {
		// Piu' file: upload parallelo.
		std::vector<std::thread> threads;
		for (auto& t : tasks)
			threads.emplace_back(uploadOne, std::ref(t));
		for (auto& th : threads)
			th.join();
	}

	for (auto& t : tasks)
		report.files.push_back(t.outcome);

	// Passo 3: in caso di errore a meta' sessione, annulla.
	if (anyError && !prep.sessionId.empty())
		Cancel(host, port, prep.sessionId);

	return report;
}

} // namespace LocalSend
