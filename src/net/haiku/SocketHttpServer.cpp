#include "net/haiku/SocketHttpServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace LocalSend {

namespace {

std::string
ToLower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower((unsigned char)c));
	return s;
}


const char*
ReasonPhrase(int status)
{
	switch (status) {
		case 200: return "OK";
		case 204: return "No Content";
		case 400: return "Bad Request";
		case 401: return "Unauthorized";
		case 403: return "Forbidden";
		case 404: return "Not Found";
		case 409: return "Conflict";
		case 500: return "Internal Server Error";
		default:  return "OK";
	}
}

// Funzioni di I/O: astraggono la differenza tra socket nudo e SSL.
typedef std::function<ssize_t(char*, size_t)> ReadFn;
typedef std::function<bool(const char*, size_t)> WriteFn;


ReadFn
MakePlainReader(int fd)
{
	return [fd](char* buf, size_t len) -> ssize_t {
		return recv(fd, buf, len, 0);
	};
}


WriteFn
MakePlainWriter(int fd)
{
	return [fd](const char* data, size_t len) -> bool {
		size_t sent = 0;
		while (sent < len) {
			ssize_t n = send(fd, data + sent, len - sent, 0);
			if (n <= 0)
				return false;
			sent += static_cast<size_t>(n);
		}
		return true;
	};
}


ReadFn
MakeSslReader(SSL* ssl)
{
	return [ssl](char* buf, size_t len) -> ssize_t {
		int n = SSL_read(ssl, buf, static_cast<int>(len));
		return static_cast<ssize_t>(n);
	};
}


WriteFn
MakeSslWriter(SSL* ssl)
{
	return [ssl](const char* data, size_t len) -> bool {
		size_t sent = 0;
		while (sent < len) {
			int chunk = static_cast<int>(
				len - sent > 16384 ? 16384 : len - sent);
			int n = SSL_write(ssl, data + sent, chunk);
			if (n <= 0)
				return false;
			sent += static_cast<size_t>(n);
		}
		return true;
	};
}


void
WriteResponse(const WriteFn& writeFn, const HttpServerResponse& r)
{
	char head[512];
	snprintf(head, sizeof(head),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		r.status, ReasonPhrase(r.status), r.contentType.c_str(),
		r.body.size());
	if (!writeFn(head, std::strlen(head)))
		return;
	if (!r.body.empty())
		writeFn(r.body.data(), r.body.size());
}

} // namespace


SocketHttpServer::~SocketHttpServer()
{
	Stop();
}


void
SocketHttpServer::EnableTls(void* sslCtx)
{
	fSslCtx = sslCtx;
}


void
SocketHttpServer::Route(const std::string& method, const std::string& path,
	Handler handler)
{
	fRoutes[method + " " + path] = std::move(handler);
}


bool
SocketHttpServer::Start(int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	int yes = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(static_cast<uint16_t>(port));

	if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		close(fd);
		return false;
	}
	if (listen(fd, 8) != 0) {
		close(fd);
		return false;
	}
	fListenFd = fd;
	return true;
}


void
SocketHttpServer::Run()
{
	if (fListenFd < 0)
		return;
	fRunning = true;
	while (fRunning) {
		int client = accept(fListenFd, nullptr, nullptr);
		if (client < 0) {
			if (!fRunning)
				break;
			continue;
		}
		HandleConnection(client);
		close(client);
	}
}


void
SocketHttpServer::Stop()
{
	fRunning = false;
	if (fListenFd >= 0) {
		close(fListenFd);
		fListenFd = -1;
	}
}


void
SocketHttpServer::HandleConnection(int clientFd)
{
	// Timeout di 10 secondi sulla lettura.
	timeval tv{};
	tv.tv_sec = 10;
	tv.tv_usec = 0;
	setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	SSL* ssl = nullptr;
	ReadFn readFn;
	WriteFn writeFn;

	if (fSslCtx) {
		ssl = SSL_new(static_cast<SSL_CTX*>(fSslCtx));
		SSL_set_fd(ssl, clientFd);
		if (SSL_accept(ssl) <= 0) {
			SSL_free(ssl);
			return;
		}
		readFn = MakeSslReader(ssl);
		writeFn = MakeSslWriter(ssl);
	} else {
		readFn = MakePlainReader(clientFd);
		writeFn = MakePlainWriter(clientFd);
	}

	std::string buf;
	char tmp[65536];

	// Leggi finche' non hai l'intestazione completa.
	size_t headerEnd = std::string::npos;
	while (true) {
		headerEnd = buf.find("\r\n\r\n");
		if (headerEnd != std::string::npos)
			break;
		if (buf.size() > (1u << 20)) {
			WriteResponse(writeFn, HttpServerResponse::Empty(400));
			goto cleanup;
		}
		ssize_t n = readFn(tmp, sizeof(tmp));
		if (n <= 0)
			goto cleanup;
		buf.append(tmp, static_cast<size_t>(n));
	}

	{
		std::string head = buf.substr(0, headerEnd);
		std::string body = buf.substr(headerEnd + 4);

		// Request line: "METODO PATH HTTP/1.1".
		size_t lineEnd = head.find("\r\n");
		std::string requestLine = head.substr(0, lineEnd);
		size_t sp1 = requestLine.find(' ');
		size_t sp2 = (sp1 == std::string::npos)
			? std::string::npos : requestLine.find(' ', sp1 + 1);
		if (sp1 == std::string::npos || sp2 == std::string::npos) {
			WriteResponse(writeFn, HttpServerResponse::Empty(400));
			goto cleanup;
		}

		HttpRequest req;
		req.method = requestLine.substr(0, sp1);
		std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);

		// Separa path da query string.
		size_t qmark = target.find('?');
		if (qmark == std::string::npos) {
			req.path = target;
		} else {
			req.path = target.substr(0, qmark);
			req.query = ParseQuery(target.substr(qmark + 1));
		}

		// Headers.
		size_t pos = lineEnd + 2;
		long long contentLength = 0;
		while (pos < head.size()) {
			size_t e = head.find("\r\n", pos);
			if (e == std::string::npos)
				e = head.size();
			std::string line = head.substr(pos, e - pos);
			pos = e + 2;
			size_t colon = line.find(':');
			if (colon != std::string::npos) {
				std::string key = ToLower(line.substr(0, colon));
				size_t v = colon + 1;
				while (v < line.size() && line[v] == ' ')
					v++;
				std::string val = line.substr(v);
				req.headers[key] = val;
				if (key == "content-length")
					contentLength = atoll(val.c_str());
			}
		}

		// Body: completa la lettura fino a content-length.
		while (static_cast<long long>(body.size()) < contentLength) {
			ssize_t n = readFn(tmp, sizeof(tmp));
			if (n <= 0)
				break;
			body.append(tmp, static_cast<size_t>(n));
		}
		if (static_cast<long long>(body.size()) > contentLength)
			body.resize(contentLength);
		req.body = std::move(body);

		// Instrada.
		auto it = fRoutes.find(req.method + " " + req.path);
		if (it == fRoutes.end()) {
			WriteResponse(writeFn, HttpServerResponse::Empty(404));
			goto cleanup;
		}
		HttpServerResponse resp = it->second(req);
		WriteResponse(writeFn, resp);
	}

cleanup:
	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
}

} // namespace LocalSend
