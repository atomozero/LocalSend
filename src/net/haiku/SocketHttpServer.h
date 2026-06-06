// Server HTTP/1.1 su socket BSD, realizza IHttpServer. Portabile e garantito su
// Haiku (link -lnetwork). Monothread: serve una connessione alla volta, che basta
// al protocollo LocalSend (una sessione per volta) in L1. In L3 verra' affiancata
// una variante TLS; la concorrenza e' un raffinamento successivo.
#pragma once

#include <map>
#include <string>

#include "net/IHttpServer.h"

namespace ls {

class SocketHttpServer : public IHttpServer {
public:
    SocketHttpServer() = default;
    ~SocketHttpServer() override;

    void route(const std::string& method, const std::string& path,
               Handler handler) override;
    bool start(int port) override;
    void run() override;
    void stop() override;

private:
    void handleConnection(int clientFd);

    int  listenFd_ = -1;
    bool running_  = false;
    std::map<std::string, Handler> routes_; // chiave: "METODO PATH"
};

} // namespace ls
