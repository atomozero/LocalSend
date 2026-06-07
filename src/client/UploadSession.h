// Orchestrazione del mittente (L0): prepare-upload -> upload -> cancel.
// Tiene sessionId + mappa fileId->token. La stessa nozione serve al ricevente
// in L1, lato opposto (li genera e li valida).
#ifndef _LOCALSEND_UPLOAD_SESSION_H
#define _LOCALSEND_UPLOAD_SESSION_H

#include <string>
#include <vector>

#include "net/IHttpClient.h"
#include "protocol/Models.h"

namespace LocalSend {

struct FileOutcome {
	std::string fileId;
	std::string fileName;
	enum class Status { Sent, Rejected, Error } status = Status::Error;
	std::string detail;
};

struct SendReport {
	bool prepared = false;
	int prepareStatus = 0; // codice HTTP del prepare-upload
	std::string sessionId;
	std::vector<FileOutcome> files;
	bool AllSent() const;
};

class UploadSession {
public:
	UploadSession(IHttpClient& http, DeviceInfo info);

	// Invia i file al ricevente host:port. 'pin' opzionale (vuoto = nessuno).
	SendReport Send(const std::string& host, int port,
		const std::vector<FileMetadata>& files, const std::string& pin = "");

private:
	void Cancel(const std::string& host, int port,
		const std::string& sessionId);

	IHttpClient& fHttp;
	DeviceInfo fInfo;
};

} // namespace LocalSend

#endif // _LOCALSEND_UPLOAD_SESSION_H
