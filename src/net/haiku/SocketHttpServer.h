// Server HTTP/1.1 su socket BSD con supporto TLS opzionale, realizza
// IHttpServer. Portabile e garantito su Haiku (-lnetwork). Monothread: serve
// una connessione alla volta, che basta al protocollo LocalSend (una sessione
// per volta). In L3 viene usato con TLS self-signed per soddisfare i client
// che richiedono HTTPS.
#ifndef _LOCALSEND_SOCKET_HTTP_SERVER_H
#define _LOCALSEND_SOCKET_HTTP_SERVER_H

#include <map>
#include <string>

#include "net/IHttpServer.h"

namespace LocalSend {

class SocketHttpServer : public IHttpServer {
public:
	SocketHttpServer() {}
	virtual ~SocketHttpServer();

	// Abilita TLS. Va chiamato prima di Start(). ctx e' un SSL_CTX* (passato
	// come void* per evitare l'include di OpenSSL nell'header).
	void EnableTls(void* sslCtx);

	// Callback di progresso body (opzionale): chiamato durante la lettura
	// del body per le richieste con content-length. (byteLetti, byteTotali).
	typedef std::function<void(long long, long long)> BodyProgressFn;
	void SetBodyProgressFn(BodyProgressFn fn);

	void Route(const std::string& method, const std::string& path,
		Handler handler) override;
	bool Start(int port) override;
	void Run() override;
	void Stop() override;

private:
	void HandleConnection(int clientFd);

	int fListenFd = -1;
	bool fRunning = false;
	void* fSslCtx = nullptr;
	BodyProgressFn fBodyProgress;
	std::map<std::string, Handler> fRoutes; // chiave: "METODO PATH"
};

} // namespace LocalSend

#endif // _LOCALSEND_SOCKET_HTTP_SERVER_H
