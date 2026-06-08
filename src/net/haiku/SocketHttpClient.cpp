#include "net/haiku/SocketHttpClient.h"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace LocalSend {

std::string
UrlEncode(const std::string& s)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size() * 3);
	for (unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_'
			|| c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0x0F];
		}
	}
	return out;
}


// Funzioni di I/O astratte: socket nudo o SSL.
typedef std::function<ssize_t(char*, size_t)> ReadFn;
typedef std::function<bool(const char*, size_t)> WriteFn;


namespace {

int
ConnectTo(const std::string& host, int port)
{
	addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char portStr[16];
	snprintf(portStr, sizeof(portStr), "%d", port);

	addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res)
		return -1;

	int fd = -1;
	for (addrinfo* p = res; p; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}


std::string
ToLower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower((unsigned char)c));
	return s;
}


HttpResponse
ReadResponse(const ReadFn& readFn)
{
	HttpResponse resp;
	std::string buf;
	char tmp[65536];

	// Leggi finche' non hai l'intestazione completa.
	size_t headerEnd = std::string::npos;
	while (true) {
		headerEnd = buf.find("\r\n\r\n");
		if (headerEnd != std::string::npos)
			break;
		ssize_t n = readFn(tmp, sizeof(tmp));
		if (n <= 0)
			break;
		buf.append(tmp, static_cast<size_t>(n));
	}
	if (headerEnd == std::string::npos) {
		resp.status = 0;
		return resp;
	}

	std::string head = buf.substr(0, headerEnd);
	std::string body = buf.substr(headerEnd + 4);

	// Status line.
	size_t lineEnd = head.find("\r\n");
	std::string statusLine = head.substr(0, lineEnd);
	{
		size_t sp1 = statusLine.find(' ');
		if (sp1 != std::string::npos)
			resp.status = atoi(statusLine.c_str() + sp1 + 1);
	}

	// Headers.
	size_t pos = lineEnd + 2;
	long long contentLength = -1;
	bool chunked = false;
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
			resp.headers[key] = val;
			if (key == "content-length")
				contentLength = atoll(val.c_str());
			if (key == "transfer-encoding"
				&& ToLower(val).find("chunked") != std::string::npos)
				chunked = true;
		}
	}

	// Body: leggi tutto il raw rimanente, poi decodifica se chunked.
	auto readRemaining = [&](long long limit) {
		if (limit >= 0) {
			while (static_cast<long long>(body.size()) < limit) {
				ssize_t n = readFn(tmp, sizeof(tmp));
				if (n <= 0)
					break;
				body.append(tmp, static_cast<size_t>(n));
			}
			if (static_cast<long long>(body.size()) > limit)
				body.resize(limit);
		} else {
			ssize_t n;
			while ((n = readFn(tmp, sizeof(tmp))) > 0)
				body.append(tmp, static_cast<size_t>(n));
		}
	};

	if (contentLength >= 0) {
		readRemaining(contentLength);
	} else if (chunked) {
		// Leggi tutto fino a chiusura, poi decodifica chunked.
		readRemaining(-1);
		// Decodifica chunked: "SIZE\r\nDATA\r\n...0\r\n\r\n"
		std::string decoded;
		size_t p = 0;
		while (p < body.size()) {
			size_t nl = body.find("\r\n", p);
			if (nl == std::string::npos)
				break;
			long long chunkSize = strtoll(body.c_str() + p, nullptr, 16);
			if (chunkSize <= 0)
				break;
			p = nl + 2;
			if (p + static_cast<size_t>(chunkSize) > body.size())
				chunkSize = static_cast<long long>(body.size() - p);
			decoded.append(body, p, static_cast<size_t>(chunkSize));
			p += static_cast<size_t>(chunkSize) + 2; // skip \r\n
		}
		body = std::move(decoded);
	} else {
		readRemaining(-1);
	}

	resp.body = std::move(body);
	return resp;
}


std::string
BuildHead(const std::string& host, int port, const std::string& path,
	const std::string& contentType, long long contentLength)
{
	char head[1024];
	snprintf(head, sizeof(head),
		"POST %s HTTP/1.1\r\n"
		"Host: %s:%d\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %lld\r\n"
		"Connection: close\r\n"
		"\r\n",
		path.c_str(), host.c_str(), port, contentType.c_str(),
		contentLength);
	return std::string(head);
}

} // namespace


SocketHttpClient::SocketHttpClient()
	:
	fSslCtx(nullptr)
{
}


SocketHttpClient::~SocketHttpClient()
{
	if (fSslCtx)
		SSL_CTX_free(static_cast<SSL_CTX*>(fSslCtx));
}


void
SocketHttpClient::EnableTls()
{
	if (fSslCtx)
		return;
	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) {
		fprintf(stderr, "TLS client: creazione SSL_CTX fallita\n");
		return;
	}
	// Accetta certificati self-signed (come da protocollo LocalSend).
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
	fSslCtx = ctx;
}


HttpResponse
SocketHttpClient::Post(const std::string& host, int port,
	const std::string& path, const std::string& contentType,
	const std::string& body)
{
	HttpResponse fail;
	int fd = ConnectTo(host, port);
	if (fd < 0)
		return fail;

	SSL* ssl = nullptr;
	ReadFn readFn;
	WriteFn writeFn;

	if (fSslCtx) {
		ssl = SSL_new(static_cast<SSL_CTX*>(fSslCtx));
		SSL_set_fd(ssl, fd);
		if (SSL_connect(ssl) <= 0) {
			SSL_free(ssl);
			close(fd);
			return fail;
		}
		readFn = [ssl](char* buf, size_t len) -> ssize_t {
			return static_cast<ssize_t>(
				SSL_read(ssl, buf, static_cast<int>(len)));
		};
		writeFn = [ssl](const char* data, size_t len) -> bool {
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
	} else {
		readFn = [fd](char* buf, size_t len) -> ssize_t {
			return recv(fd, buf, len, 0);
		};
		writeFn = [fd](const char* data, size_t len) -> bool {
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

	std::string head = BuildHead(host, port, path, contentType,
		static_cast<long long>(body.size()));
	bool okHead = writeFn(head.data(), head.size());
	bool okBody = okHead && writeFn(body.data(), body.size());
	HttpResponse resp = okBody ? ReadResponse(readFn) : fail;

	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	close(fd);
	return resp;
}


HttpResponse
SocketHttpClient::PostFile(const std::string& host, int port,
	const std::string& path, const std::string& contentType,
	const std::string& filePath, TransferProgressFn progress)
{
	HttpResponse fail;
	FILE* f = fopen(filePath.c_str(), "rb");
	if (!f)
		return fail;

	fseek(f, 0, SEEK_END);
	long long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 0) {
		fclose(f);
		return fail;
	}

	int fd = ConnectTo(host, port);
	if (fd < 0) {
		fclose(f);
		return fail;
	}

	SSL* ssl = nullptr;
	ReadFn readFn;
	WriteFn writeFn;

	if (fSslCtx) {
		ssl = SSL_new(static_cast<SSL_CTX*>(fSslCtx));
		SSL_set_fd(ssl, fd);
		if (SSL_connect(ssl) <= 0) {
			SSL_free(ssl);
			close(fd);
			fclose(f);
			return fail;
		}
		readFn = [ssl](char* buf, size_t len) -> ssize_t {
			return static_cast<ssize_t>(
				SSL_read(ssl, buf, static_cast<int>(len)));
		};
		writeFn = [ssl](const char* data, size_t len) -> bool {
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
	} else {
		readFn = [fd](char* buf, size_t len) -> ssize_t {
			return recv(fd, buf, len, 0);
		};
		writeFn = [fd](const char* data, size_t len) -> bool {
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

	std::string head = BuildHead(host, port, path, contentType, size);
	if (!writeFn(head.data(), head.size())) {
		if (ssl) {
			SSL_shutdown(ssl);
			SSL_free(ssl);
		}
		close(fd);
		fclose(f);
		return fail;
	}

	char buf[65536];
	bool ok = true;
	long long totalSent = 0;
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (!writeFn(buf, n)) {
			ok = false;
			break;
		}
		totalSent += static_cast<long long>(n);
		if (progress)
			progress(totalSent, size);
	}
	fclose(f);

	HttpResponse resp = ok ? ReadResponse(readFn) : fail;
	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	close(fd);
	return resp;
}

} // namespace LocalSend
