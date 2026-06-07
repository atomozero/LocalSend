// Annuncio e scoperta multicast UDP per LocalSend v2.1 (L2).

#include "net/MulticastAnnouncer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "protocol/Constants.h"
#include "protocol/Json.h"

namespace LocalSend {

// Intervallo tra annunci (secondi).
static const int kAnnounceInterval = 5;


MulticastAnnouncer::MulticastAnnouncer(const DeviceInfo& info)
	:
	fInfo(info),
	fFd(-1),
	fRunning(false)
{
}


MulticastAnnouncer::~MulticastAnnouncer()
{
	Stop();
}


bool
MulticastAnnouncer::Start()
{
	fFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fFd < 0) {
		perror("multicast: socket");
		return false;
	}

	int yes = 1;
	setsockopt(fFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	// Bind su porta del protocollo per ricevere annunci degli altri.
	sockaddr_in bindAddr{};
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	bindAddr.sin_port = htons(kDefaultPort);
	if (bind(fFd, (sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
		perror("multicast: bind");
		close(fFd);
		fFd = -1;
		return false;
	}

	// Entra nel gruppo multicast.
	ip_mreq mreq{};
	mreq.imr_multiaddr.s_addr = inet_addr(kMulticastGroup);
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);
	if (setsockopt(fFd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			&mreq, sizeof(mreq)) < 0) {
		perror("multicast: IP_ADD_MEMBERSHIP");
		close(fFd);
		fFd = -1;
		return false;
	}

	// TTL = 1: solo LAN locale.
	unsigned char ttl = 1;
	setsockopt(fFd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

	fRunning = true;
	fThread = std::thread(&MulticastAnnouncer::Run, this);
	return true;
}


void
MulticastAnnouncer::Stop()
{
	if (!fRunning)
		return;
	fRunning = false;
	if (fFd >= 0) {
		// Chiude il socket per sbloccare select().
		shutdown(fFd, SHUT_RDWR);
	}
	if (fThread.joinable())
		fThread.join();
	if (fFd >= 0) {
		close(fFd);
		fFd = -1;
	}
}


void
MulticastAnnouncer::Run()
{
	sockaddr_in groupAddr{};
	groupAddr.sin_family = AF_INET;
	groupAddr.sin_addr.s_addr = inet_addr(kMulticastGroup);
	groupAddr.sin_port = htons(kDefaultPort);

	// Prepara il JSON dell'annuncio (announce: true).
	JsonValue ann = fInfo.ToJson();
	ann["announce"] = true;
	std::string annMsg = ann.Dump();

	// Annuncio iniziale immediato.
	sendto(fFd, annMsg.data(), annMsg.size(), 0,
		(sockaddr*)&groupAddr, sizeof(groupAddr));

	while (fRunning) {
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(fFd, &readSet);

		timeval tv{};
		tv.tv_sec = kAnnounceInterval;
		tv.tv_usec = 0;

		int ready = select(fFd + 1, &readSet, nullptr, nullptr, &tv);

		if (!fRunning)
			break;

		if (ready > 0 && FD_ISSET(fFd, &readSet)) {
			// Messaggio in arrivo: un altro dispositivo si annuncia.
			char buf[2048];
			sockaddr_in from{};
			socklen_t fromLen = sizeof(from);
			ssize_t n = recvfrom(fFd, buf, sizeof(buf) - 1, 0,
				(sockaddr*)&from, &fromLen);
			if (n <= 0)
				continue;
			buf[n] = 0;

			// Ignora i propri annunci.
			try {
				JsonValue msg = JsonValue::Parse(std::string(buf, n));
				if (msg.Has("fingerprint")
					&& msg.At("fingerprint").AsString()
						== fInfo.fingerprint) {
					continue;
				}

				// Se e' un annuncio (announce=true), rispondi con
				// announce=false in unicast.
				if (msg.Has("announce")
					&& msg.At("announce").AsBool(false)) {
					JsonValue reply = fInfo.ToJson();
					reply["announce"] = false;
					std::string replyMsg = reply.Dump();
					sendto(fFd, replyMsg.data(), replyMsg.size(), 0,
						(sockaddr*)&from, sizeof(from));
				}
			} catch (...) {
				// JSON non valido: ignora.
			}
		}

		if (ready == 0) {
			// Timeout: invia annuncio periodico.
			sendto(fFd, annMsg.data(), annMsg.size(), 0,
				(sockaddr*)&groupAddr, sizeof(groupAddr));
		}
	}
}

} // namespace LocalSend
