// Client HTTP/1.1 su socket BSD con supporto TLS opzionale. Portabile e
// garantito su Haiku (-lnetwork). Usato dal mittente (L0) per inviare file a
// un ricevente LocalSend. Con EnableTls() si connette via HTTPS (necessario
// per i client moderni che richiedono TLS).
#ifndef _LOCALSEND_SOCKET_HTTP_CLIENT_H
#define _LOCALSEND_SOCKET_HTTP_CLIENT_H

#include "net/IHttpClient.h"

namespace LocalSend {

class SocketHttpClient : public IHttpClient {
public:
	SocketHttpClient();
	~SocketHttpClient();

	// Abilita TLS per tutte le connessioni successive. Crea internamente
	// un SSL_CTX client che accetta certificati self-signed (come da
	// protocollo LocalSend).
	void EnableTls();

	HttpResponse Get(const std::string& host, int port,
		const std::string& path) override;

	HttpResponse Post(const std::string& host, int port,
		const std::string& path, const std::string& contentType,
		const std::string& body) override;

	HttpResponse PostFile(const std::string& host, int port,
		const std::string& path, const std::string& contentType,
		const std::string& filePath,
		TransferProgressFn progress = nullptr) override;

private:
	void* fSslCtx; // SSL_CTX*, nullptr se TLS disabilitato
};

} // namespace LocalSend

#endif // _LOCALSEND_SOCKET_HTTP_CLIENT_H
