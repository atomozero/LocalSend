#include "net/haiku/SocketHttpClient.h"

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace LocalSend {

std::string
UrlEncode(const std::string& s)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	out.reserve(s.size() * 3);
	for (unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0x0F];
		}
	}
	return out;
}


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


bool
SendAll(int fd, const char* data, size_t len)
{
	size_t sent = 0;
	while (sent < len) {
		ssize_t n = send(fd, data + sent, len - sent, 0);
		if (n <= 0)
			return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}


std::string
ToLower(std::string s)
{
	for (char& c : s)
		c = static_cast<char>(std::tolower((unsigned char)c));
	return s;
}


// Legge l'intera risposta e la decompone in status/headers/body.
HttpResponse
ReadResponse(int fd)
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
		ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
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

	// Body. Per L0 i ricevitori rispondono con Content-Length o chiudono.
	if (contentLength >= 0) {
		while (static_cast<long long>(body.size()) < contentLength) {
			ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
			if (n <= 0)
				break;
			body.append(tmp, static_cast<size_t>(n));
		}
		if (static_cast<long long>(body.size()) > contentLength)
			body.resize(contentLength);
	} else {
		// Niente Content-Length: leggi fino a chiusura.
		ssize_t n;
		while ((n = recv(fd, tmp, sizeof(tmp), 0)) > 0)
			body.append(tmp, static_cast<size_t>(n));
		(void)chunked; // chunked non necessario per i body brevi di L0
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
		path.c_str(), host.c_str(), port, contentType.c_str(), contentLength);
	return std::string(head);
}

} // namespace


HttpResponse
SocketHttpClient::Post(const std::string& host, int port,
	const std::string& path, const std::string& contentType,
	const std::string& body)
{
	HttpResponse fail;
	int fd = ConnectTo(host, port);
	if (fd < 0)
		return fail;

	std::string head = BuildHead(host, port, path, contentType,
		static_cast<long long>(body.size()));
	bool okHead = SendAll(fd, head.data(), head.size());
	bool okBody = okHead && SendAll(fd, body.data(), body.size());
	HttpResponse resp = okBody ? ReadResponse(fd) : fail;
	close(fd);
	return resp;
}


HttpResponse
SocketHttpClient::PostFile(const std::string& host, int port,
	const std::string& path, const std::string& contentType,
	const std::string& filePath)
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

	std::string head = BuildHead(host, port, path, contentType, size);
	if (!SendAll(fd, head.data(), head.size())) {
		close(fd);
		fclose(f);
		return fail;
	}

	char buf[65536];
	bool ok = true;
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
		if (!SendAll(fd, buf, n)) {
			ok = false;
			break;
		}
	}
	fclose(f);

	HttpResponse resp = ok ? ReadResponse(fd) : fail;
	close(fd);
	return resp;
}

} // namespace LocalSend
