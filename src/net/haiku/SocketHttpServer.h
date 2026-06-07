// Server HTTP/1.1 su socket BSD, realizza IHttpServer. Portabile e garantito
// su Haiku (-lnetwork). Monothread: serve una connessione alla volta, che basta
// al protocollo LocalSend (una sessione per volta) in L1. In L3 arriva
// una variante TLS; la concorrenza e' un raffinamento successivo.
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

	void Route(const std::string& method, const std::string& path,
		Handler handler) override;
	bool Start(int port) override;
	void Run() override;
	void Stop() override;

private:
	void HandleConnection(int clientFd);

	int fListenFd = -1;
	bool fRunning = false;
	std::map<std::string, Handler> fRoutes; // chiave: "METODO PATH"
};

} // namespace LocalSend

#endif // _LOCALSEND_SOCKET_HTTP_SERVER_H
