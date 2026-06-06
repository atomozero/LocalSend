// Client HTTP/1.1 su socket BSD. Portabile e garantito su Haiku (link -lnetwork).
// In L0 lavora su HTTP semplice; in L3 verra' affiancata una variante TLS.
#pragma once

#include "net/IHttpClient.h"

namespace ls {

class SocketHttpClient : public IHttpClient {
public:
    HttpResponse post(const std::string& host, int port,
                      const std::string& path,
                      const std::string& contentType,
                      const std::string& body) override;

    HttpResponse postFile(const std::string& host, int port,
                          const std::string& path,
                          const std::string& contentType,
                          const std::string& filePath) override;
};

} // namespace ls
