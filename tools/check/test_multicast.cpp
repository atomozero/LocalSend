// Verifica multicast UDP su Haiku per LocalSend (224.0.0.167:53317).
// Compila su Haiku:  gcc -o test_multicast test_multicast.cpp -lnetwork
// Uso:
//   ./test_multicast recv   (un terminale)
//   ./test_multicast send   (un altro terminale, anche su altra macchina LAN)
// Successo = "recv" stampa il messaggio inviato da "send".
//
// Nota onesta: la sola compilazione non prova nulla. Il valore e' il RUNTIME su
// Haiku reale, meglio tra due macchine fisiche sulla stessa LAN.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>

static const char* GROUP = "224.0.0.167";
static const int   PORT  = 53317;

static int do_recv() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        perror("IP_ADD_MEMBERSHIP"); return 1;
    }

    printf("In ascolto su %s:%d ...\n", GROUP, PORT);
    char buf[2048];
    sockaddr_in from{};
    socklen_t fromlen = sizeof(from);
    ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fromlen);
    if (n < 0) { perror("recvfrom"); return 1; }
    buf[n] = 0;
    printf("Ricevuto da %s: %s\n", inet_ntoa(from.sin_addr), buf);
    close(fd);
    return 0;
}

static int do_send() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    unsigned char ttl = 1;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(GROUP);
    addr.sin_port = htons(PORT);

    const char* msg = "{\"alias\":\"Haiku Box\",\"announce\":true}";
    ssize_t n = sendto(fd, msg, strlen(msg), 0, (sockaddr*)&addr, sizeof(addr));
    if (n < 0) { perror("sendto"); return 1; }
    printf("Inviato a %s:%d -> %s\n", GROUP, PORT, msg);
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: %s recv|send\n", argv[0]); return 2; }
    if (strcmp(argv[1], "recv") == 0) return do_recv();
    if (strcmp(argv[1], "send") == 0) return do_send();
    fprintf(stderr, "argomento sconosciuto: %s\n", argv[1]);
    return 2;
}
