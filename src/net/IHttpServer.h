// Interfaccia del server HTTP, DEFINITA ora (L0) ma IMPLEMENTATA in L1.
// Separata da IHttpClient di proposito: L0 usa solo il client; L1 aggiunge un
// SocketHttpServer (socket BSD) che realizza questa interfaccia senza toccare il
// mittente. La logica di protocollo del ricevente (ReceiveSession) dipende da
// questa astrazione, non dai socket: cosi' e' testabile senza rete.
#pragma once

#include <functional>
#include <map>
#include <string>

namespace ls {

// Richiesta HTTP in ingresso, come la vede un handler del server.
struct HttpRequest {
    std::string method;                          // "POST", "GET", ...
    std::string path;                            // solo il path, senza query
    std::map<std::string, std::string> query;    // chiavi/valori gia' decodificati
    std::map<std::string, std::string> headers;  // chiavi in minuscolo
    std::string body;

    // Valore di un parametro di query, o "" se assente.
    std::string q(const std::string& key) const {
        auto it = query.find(key);
        return it == query.end() ? std::string() : it->second;
    }
};

// Risposta che un handler restituisce al server.
struct HttpServerResponse {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;

    static HttpServerResponse json(int status, const std::string& body) {
        return HttpServerResponse{status, "application/json", body};
    }
    static HttpServerResponse empty(int status) {
        return HttpServerResponse{status, "text/plain", ""};
    }
};

class IHttpServer {
public:
    virtual ~IHttpServer() = default;

    using Handler = std::function<HttpServerResponse(const HttpRequest&)>;

    // Registra un handler per (metodo, path). Il path NON include la query.
    virtual void route(const std::string& method, const std::string& path,
                       Handler handler) = 0;

    // Prepara l'ascolto sulla porta (socket/bind/listen). Ritorna false se il
    // bind fallisce (es. porta occupata). Non blocca.
    virtual bool start(int port) = 0;

    // Entra nel loop di accept e serve le richieste finche' stop() non e'
    // chiamato (da un altro thread o da un gestore di segnale). Bloccante.
    virtual void run() = 0;

    // Ferma il loop e libera la porta.
    virtual void stop() = 0;
};

// Decodifica percentuale (inverso di urlEncode lato client). Portabile: serve
// sia al parsing della query sia, in L1, alla lettura dei path.
std::string urlDecode(const std::string& s);

// Spezza una query string ("a=1&b=2", senza il '?') in coppie decodificate.
std::map<std::string, std::string> parseQuery(const std::string& queryString);

} // namespace ls
