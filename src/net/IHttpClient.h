// Interfaccia del client HTTP usata da L0. Separata da IHttpServer (L1) di
// proposito: L0 usa solo questa, L1 aggiunge il server senza toccare il mittente.
#pragma once

#include <map>
#include <string>

namespace ls {

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers; // chiavi in minuscolo
    std::string body;

    bool ok() const { return status >= 200 && status < 300; }
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // POST con body in memoria. 'path' include gia' l'eventuale query string.
    virtual HttpResponse post(const std::string& host, int port,
                              const std::string& path,
                              const std::string& contentType,
                              const std::string& body) = 0;

    // POST che invia in streaming il contenuto di un file come body.
    virtual HttpResponse postFile(const std::string& host, int port,
                                  const std::string& path,
                                  const std::string& contentType,
                                  const std::string& filePath) = 0;
};

// Codifica percentuale per i valori di query (sessionId, fileId, token, pin).
std::string urlEncode(const std::string& s);

} // namespace ls
