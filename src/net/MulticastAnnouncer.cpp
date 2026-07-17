// Annuncio e scoperta multicast UDP per LocalSend v2.1 (L2).

#include "net/MulticastAnnouncer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>

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
MulticastAnnouncer::TriggerBurst()
{
	// Annuncio extra fuori dal tick periodico: i peer gia' attivi
	// rispondono in unicast in pochi ms, quindi la lista si popola
	// senza attendere kAnnounceInterval. Safe da thread esterni:
	// sendto e' atomico a livello kernel; fFd e' stabile tra Start/Stop.
	if (!fRunning || fFd < 0)
		return;
	sockaddr_in groupAddr{};
	groupAddr.sin_family = AF_INET;
	groupAddr.sin_addr.s_addr = inet_addr(kMulticastGroup);
	groupAddr.sin_port = htons(kDefaultPort);
	std::string annMsg = _AnnounceJson(true);
	sendto(fFd, annMsg.data(), annMsg.size(), 0,
		(sockaddr*)&groupAddr, sizeof(groupAddr));
}


void
MulticastAnnouncer::SetBoard(int boardRev, bool downloadActive)
{
	std::lock_guard<std::mutex> lock(fInfoMtx);
	fInfo.boardRev = boardRev;
	fInfo.download = downloadActive;
}


void
MulticastAnnouncer::SetAlias(const std::string& alias)
{
	std::lock_guard<std::mutex> lock(fInfoMtx);
	fInfo.alias = alias;
}


std::string
MulticastAnnouncer::_AnnounceJson(bool announce)
{
	std::lock_guard<std::mutex> lock(fInfoMtx);
	JsonValue v = fInfo.ToJson();
	v["announce"] = announce;
	return v.Dump();
}


void
MulticastAnnouncer::Run()
{
	sockaddr_in groupAddr{};
	groupAddr.sin_family = AF_INET;
	groupAddr.sin_addr.s_addr = inet_addr(kMulticastGroup);
	groupAddr.sin_port = htons(kDefaultPort);

	// Il fingerprint non cambia mai dopo Start: copia locale, letta senza
	// lock nel loop. Il resto dell'annuncio invece va ricomposto a ogni
	// invio: SetBoard puo' cambiarlo a runtime.
	std::string selfFingerprint;
	{
		std::lock_guard<std::mutex> lock(fInfoMtx);
		selfFingerprint = fInfo.fingerprint;
	}

	// Annuncio iniziale immediato + timer per i successivi.
	{
		std::string annMsg = _AnnounceJson(true);
		sendto(fFd, annMsg.data(), annMsg.size(), 0,
			(sockaddr*)&groupAddr, sizeof(groupAddr));
	}
	time_t lastAnnounce = time(nullptr);

	while (fRunning) {
		// Timeout breve: l'annuncio periodico NON dipende dall'assenza di
		// traffico ma da un timer indipendente (sotto). Cosi' su una LAN
		// affollata continuiamo comunque ad annunciarci, e i peer che ci
		// registrano IN RISPOSTA (es. lo smartphone, che annuncia di rado)
		// restano vivi invece di essere potati per TTL.
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(fFd, &readSet);

		timeval tv{};
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		int ready = select(fFd + 1, &readSet, nullptr, nullptr, &tv);

		if (!fRunning)
			break;

		// Drena TUTTI i pacchetti in coda, non solo il primo: piu' peer
		// possono annunciarsi/rispondere quasi insieme (es. le repliche al
		// nostro annuncio). Con MSG_DONTWAIT il ciclo esce a coda vuota.
		while (ready > 0 && FD_ISSET(fFd, &readSet)) {
			char buf[2048];
			sockaddr_in from{};
			socklen_t fromLen = sizeof(from);
			ssize_t n = recvfrom(fFd, buf, sizeof(buf) - 1, MSG_DONTWAIT,
				(sockaddr*)&from, &fromLen);
			if (n <= 0)
				break;
			buf[n] = 0;

			try {
				JsonValue msg = JsonValue::Parse(std::string(buf, n));
				// Ignora i propri annunci.
				if (msg.Has("fingerprint")
					&& msg.At("fingerprint").AsString()
						== selfFingerprint) {
					continue;
				}

				// Notifica al consumatore (UI): peer sentito (annuncio o
				// reply). Tutti i campi sono opzionali tranne fingerprint.
				if (fPeerCb && msg.Has("alias")
						&& msg.Has("fingerprint")) {
					Peer p;
					p.alias = msg.At("alias").AsString();
					p.fingerprint = msg.At("fingerprint").AsString();
					p.host = inet_ntoa(from.sin_addr);
					p.port = msg.Has("port")
						? static_cast<int>(msg.At("port").AsInt(kDefaultPort))
						: kDefaultPort;
					p.deviceType = msg.Has("deviceType")
						? msg.At("deviceType").AsString()
						: std::string("unknown");
					if (msg.Has("boardRev")) {
						p.boardRev = static_cast<int>(
							msg.At("boardRev").AsInt(0));
					}
					// Riconoscimento Haiku: marcatore "app" (certezza) o,
					// come ripiego, deviceModel "Haiku".
					std::string app = msg.Has("app")
						? msg.At("app").AsString() : std::string();
					std::string model = msg.Has("deviceModel")
						? msg.At("deviceModel").AsString() : std::string();
					p.deviceModel = model;
					p.isHaiku = (app == kAppId) || (model == "Haiku");
					fPeerCb(p);
				}

				// Se e' un annuncio (announce=true), rispondi con
				// announce=false in unicast.
				if (msg.Has("announce")
					&& msg.At("announce").AsBool(false)) {
					std::string replyMsg = _AnnounceJson(false);
					sendto(fFd, replyMsg.data(), replyMsg.size(), 0,
						(sockaddr*)&from, sizeof(from));
				}
			} catch (...) {
				// JSON non valido: ignora.
			}
		}

		// Annuncio periodico su timer indipendente dal traffico: garantisce
		// che ci annunciamo ogni kAnnounceInterval anche a LAN affollata.
		time_t now = time(nullptr);
		if (now - lastAnnounce >= kAnnounceInterval) {
			std::string annMsg = _AnnounceJson(true);
			sendto(fFd, annMsg.data(), annMsg.size(), 0,
				(sockaddr*)&groupAddr, sizeof(groupAddr));
			lastAnnounce = now;
		}
	}
}

} // namespace LocalSend
