// De-risking del trasporto HTTP con i soli socket BSD (portabili, garantiti su
// Haiku). Dimostra ENTRAMBE le direzioni senza dipendere da classi BeAPI incerte:
//   - "server": apre listen()+accept() su :53317 e risponde 200 (de-rischia L1)
//   - "client": fa una GET verso un host:porta e stampa la risposta (base L0)
//
// Compila su Haiku:  gcc -o test_tcp_http test_tcp_http.cpp -lnetwork
// Uso:
//   ./test_tcp_http server
//   ./test_tcp_http client 127.0.0.1 53317 /
//
// Questo NON e' il client/server LocalSend definitivo: e' la prova che il
// trasporto TCP minimo funziona su Haiku in entrambi i versi.

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>

static int do_server() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(53317);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(fd, 4) < 0) { perror("listen"); return 1; }
    printf("Server TCP in ascolto su :53317 (Ctrl-C per uscire)\n");

    for (;;) {
        sockaddr_in cli{};
        socklen_t len = sizeof(cli);
        int c = accept(fd, (sockaddr*)&cli, &len);
        if (c < 0) { perror("accept"); continue; }
        char buf[4096];
        ssize_t n = recv(c, buf, sizeof(buf) - 1, 0);
        if (n > 0) { buf[n] = 0; printf("---- richiesta ----\n%s\n", buf); }
        const char* body = "{\"status\":\"ok\"}";
        char resp[256];
        int rl = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(body), body);
        send(c, resp, rl, 0);
        close(c);
    }
}

static int do_client(const char* host, const char* port, const char* path) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, port, &hints, &res) != 0) { perror("getaddrinfo"); return 1; }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { perror("socket"); freeaddrinfo(res); return 1; }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        perror("connect"); freeaddrinfo(res); return 1;
    }
    freeaddrinfo(res);

    char req[1024];
    int rl = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\nHost: %s:%s\r\nConnection: close\r\n\r\n",
        path, host, port);
    send(fd, req, rl, 0);

    char buf[4096];
    ssize_t n;
    printf("---- risposta ----\n");
    while ((n = recv(fd, buf, sizeof(buf) - 1, 0)) > 0) { buf[n] = 0; fputs(buf, stdout); }
    printf("\n");
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 2 && strcmp(argv[1], "server") == 0) return do_server();
    if (argc >= 5 && strcmp(argv[1], "client") == 0)
        return do_client(argv[2], argv[3], argv[4]);
    fprintf(stderr, "uso:\n  %s server\n  %s client <host> <porta> <path>\n",
            argv[0], argv[0]);
    return 2;
}
