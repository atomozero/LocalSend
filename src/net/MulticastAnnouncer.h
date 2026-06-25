// Annuncio e scoperta multicast UDP per LocalSend v2.1 (L2).
// Un thread in background invia periodicamente il DeviceInfo al gruppo
// multicast 224.0.0.167:53317 e risponde agli annunci di altri dispositivi.
#ifndef _LOCALSEND_MULTICAST_ANNOUNCER_H
#define _LOCALSEND_MULTICAST_ANNOUNCER_H

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "protocol/Models.h"

namespace LocalSend {

class MulticastAnnouncer {
public:
	// Callback invocato dal thread del announcer ogni volta che si sente
	// un peer (annuncio o reply). Chi imposta la callback deve smistare
	// thread-safe verso la UI (es. BLooper::PostMessage).
	struct Peer {
		std::string alias;
		std::string fingerprint;
		std::string host;
		int port = 0;
		std::string deviceType;
	};
	using PeerHeardCallback = std::function<void(const Peer&)>;

	MulticastAnnouncer(const DeviceInfo& info);
	~MulticastAnnouncer();

	// Da chiamare prima di Start(). La callback puo' venire invocata da un
	// thread non-GUI: chi la registra deve usare PostMessage o lock interni.
	void SetPeerHeardCallback(PeerHeardCallback cb) { fPeerCb = cb; }

	// Apre il socket UDP, unisce il gruppo multicast e avvia il thread.
	// Ritorna false se il socket non puo' essere aperto.
	bool Start();

	// Ferma il thread e chiude il socket.
	void Stop();

	// Spara subito un annuncio multicast extra (oltre al tick periodico).
	// Usato all'apertura della finestra e dal pulsante Refresh per
	// forzare le risposte unicast immediate dai peer gia' attivi.
	void TriggerBurst();

private:
	void Run();

	DeviceInfo fInfo;
	int fFd;
	std::atomic<bool> fRunning;
	std::thread fThread;
	PeerHeardCallback fPeerCb;
};

} // namespace LocalSend

#endif // _LOCALSEND_MULTICAST_ANNOUNCER_H
