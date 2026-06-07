// Annuncio e scoperta multicast UDP per LocalSend v2.1 (L2).
// Un thread in background invia periodicamente il DeviceInfo al gruppo
// multicast 224.0.0.167:53317 e risponde agli annunci di altri dispositivi.
#ifndef _LOCALSEND_MULTICAST_ANNOUNCER_H
#define _LOCALSEND_MULTICAST_ANNOUNCER_H

#include <atomic>
#include <thread>

#include "protocol/Models.h"

namespace LocalSend {

class MulticastAnnouncer {
public:
	MulticastAnnouncer(const DeviceInfo& info);
	~MulticastAnnouncer();

	// Apre il socket UDP, unisce il gruppo multicast e avvia il thread.
	// Ritorna false se il socket non puo' essere aperto.
	bool Start();

	// Ferma il thread e chiude il socket.
	void Stop();

private:
	void Run();

	DeviceInfo fInfo;
	int fFd;
	std::atomic<bool> fRunning;
	std::thread fThread;
};

} // namespace LocalSend

#endif // _LOCALSEND_MULTICAST_ANNOUNCER_H
