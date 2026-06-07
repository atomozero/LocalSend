// Client HTTP/1.1 su socket BSD. Portabile e garantito su Haiku (-lnetwork).
// In L0 lavora su HTTP semplice; in L3 verra' affiancata una variante TLS.
#ifndef _LOCALSEND_SOCKET_HTTP_CLIENT_H
#define _LOCALSEND_SOCKET_HTTP_CLIENT_H

#include "net/IHttpClient.h"

namespace LocalSend {

class SocketHttpClient : public IHttpClient {
public:
	HttpResponse Post(const std::string& host, int port,
		const std::string& path, const std::string& contentType,
		const std::string& body) override;

	HttpResponse PostFile(const std::string& host, int port,
		const std::string& path, const std::string& contentType,
		const std::string& filePath) override;
};

} // namespace LocalSend

#endif // _LOCALSEND_SOCKET_HTTP_CLIENT_H
