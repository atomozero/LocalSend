#include "net/haiku/SocketHttpServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ls {

namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

bool sendAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

const char* reasonPhrase(int status) {
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

void writeResponse(int fd, const HttpServerResponse& r) {
    char head[512];
    snprintf(head, sizeof(head),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             r.status, reasonPhrase(r.status), r.contentType.c_str(),
             r.body.size());
    if (!sendAll(fd, head, std::strlen(head))) return;
    if (!r.body.empty()) sendAll(fd, r.body.data(), r.body.size());
}

} // namespace

SocketHttpServer::~SocketHttpServer() { stop(); }

void SocketHttpServer::route(const std::string& method, const std::string& path,
                             Handler handler) {
    routes_[method + " " + path] = std::move(handler);
}

bool SocketHttpServer::start(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

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
    listenFd_ = fd;
    return true;
}

void SocketHttpServer::run() {
    if (listenFd_ < 0) return;
    running_ = true;
    while (running_) {
        int client = accept(listenFd_, nullptr, nullptr);
        if (client < 0) {
            if (!running_) break; // stop() ha chiuso il socket: uscita pulita
            continue;
        }
        handleConnection(client);
        close(client);
    }
}

void SocketHttpServer::stop() {
    running_ = false;
    if (listenFd_ >= 0) {
        // Chiudere il socket di ascolto sblocca accept() in run().
        close(listenFd_);
        listenFd_ = -1;
    }
}

void SocketHttpServer::handleConnection(int clientFd) {
    std::string buf;
    char tmp[65536];

    // Leggi finche' non hai l'intestazione completa.
    size_t headerEnd = std::string::npos;
    while (true) {
        headerEnd = buf.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
        if (buf.size() > (1u << 20)) { // header assurdamente grande: rifiuta
            writeResponse(clientFd, HttpServerResponse::empty(400));
            return;
        }
        ssize_t n = recv(clientFd, tmp, sizeof(tmp), 0);
        if (n <= 0) return;
        buf.append(tmp, static_cast<size_t>(n));
    }

    std::string head = buf.substr(0, headerEnd);
    std::string body = buf.substr(headerEnd + 4);

    // Request line: "METODO PATH HTTP/1.1".
    size_t lineEnd = head.find("\r\n");
    std::string requestLine = head.substr(0, lineEnd);
    size_t sp1 = requestLine.find(' ');
    size_t sp2 = (sp1 == std::string::npos) ? std::string::npos
                                            : requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        writeResponse(clientFd, HttpServerResponse::empty(400));
        return;
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
        req.query = parseQuery(target.substr(qmark + 1));
    }

    // Headers.
    size_t pos = lineEnd + 2;
    long long contentLength = 0;
    while (pos < head.size()) {
        size_t e = head.find("\r\n", pos);
        if (e == std::string::npos) e = head.size();
        std::string line = head.substr(pos, e - pos);
        pos = e + 2;
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = toLower(line.substr(0, colon));
            size_t v = colon + 1;
            while (v < line.size() && line[v] == ' ') v++;
            std::string val = line.substr(v);
            req.headers[key] = val;
            if (key == "content-length") contentLength = atoll(val.c_str());
        }
    }

    // Body: completa la lettura fino a content-length. (L1: in memoria; lo
    // streaming diretto su disco e' un raffinamento, vedi docs/02-design-L1.md.)
    while (static_cast<long long>(body.size()) < contentLength) {
        ssize_t n = recv(clientFd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        body.append(tmp, static_cast<size_t>(n));
    }
    if (static_cast<long long>(body.size()) > contentLength)
        body.resize(contentLength);
    req.body = std::move(body);

    // Instrada.
    auto it = routes_.find(req.method + " " + req.path);
    if (it == routes_.end()) {
        writeResponse(clientFd, HttpServerResponse::empty(404));
        return;
    }
    HttpServerResponse resp = it->second(req);
    writeResponse(clientFd, resp);
}

} // namespace ls
