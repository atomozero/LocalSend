// localsend-receive: ricevente LocalSend con HTTPS, scoperta automatica e
// conferma interattiva (L3). Alza un server HTTPS, si annuncia in LAN via
// multicast UDP, e chiede conferma all'utente prima di accettare i file.
//
//   localsend-receive [--dir CARTELLA] [--port PORTA] [--alias NOME]
//                     [--pin PIN] [--auto]
//
// Opzioni:
//   --pin PIN     Richiede questo PIN al mittente (401 se errato/assente)
//   --auto        Accetta automaticamente senza chiedere conferma
//
// Esempio:
//   localsend-receive --dir /boot/home/Downloads --pin 1234

#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "net/MulticastAnnouncer.h"
#include "net/TlsContext.h"
#include "net/haiku/SocketHttpServer.h"
#include "protocol/Constants.h"
#include "protocol/Fingerprint.h"
#include "protocol/Models.h"
#include "server/FileSink.h"
#include "server/ReceiveSession.h"

using namespace LocalSend;

namespace {

SocketHttpServer* gServer = nullptr;

void
OnSignal(int)
{
	if (gServer)
		gServer->Stop();
}


void
Usage(const char* argv0)
{
	fprintf(stderr,
		"uso: %s [--dir CARTELLA] [--port PORTA] [--alias NOME] "
		"[--pin PIN] [--auto]\n",
		argv0);
}


// Chiede conferma all'utente su stdin. Ritorna true se accetta.
bool
AskConfirmation(const IncomingPrepareUpload& in)
{
	printf("\nRichiesta da \"%s\" (%s): %zu file\n",
		in.sender.alias.c_str(), in.sender.deviceType.c_str(),
		in.files.size());
	for (const auto& f : in.files) {
		printf("  - %s (%lld byte, %s)\n", f.fileName.c_str(), f.size,
			f.fileType.c_str());
	}
	printf("Accettare? [s/n] ");
	fflush(stdout);

	char buf[16] = {};
	if (!fgets(buf, sizeof(buf), stdin))
		return false;
	return buf[0] == 's' || buf[0] == 'S'
		|| buf[0] == 'y' || buf[0] == 'Y';
}

} // namespace


int
main(int argc, char** argv)
{
	// Log a riga: cosi' l'output compare subito anche se rediretto su file
	// (un server gira a lungo; senza, il buffer a blocchi nasconde i log).
	setvbuf(stdout, nullptr, _IOLBF, 0);

	signal(SIGPIPE, SIG_IGN); // niente crash se il peer chiude a meta' upload
	signal(SIGINT, OnSignal);
	signal(SIGTERM, OnSignal);

	std::string destDir = "/boot/home/Downloads";
	int port = kDefaultPort;
	std::string alias = "Haiku Box";
	std::string requiredPin;
	bool autoAccept = false;

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--dir" && i + 1 < argc) {
			destDir = argv[++i];
		} else if (a == "--port" && i + 1 < argc) {
			port = atoi(argv[++i]);
		} else if (a == "--alias" && i + 1 < argc) {
			alias = argv[++i];
		} else if (a == "--pin" && i + 1 < argc) {
			requiredPin = argv[++i];
		} else if (a == "--auto") {
			autoAccept = true;
		} else {
			Usage(argv[0]);
			return 2;
		}
	}

	// Certificato TLS self-signed (HTTPS richiesto dai client moderni).
	TlsIdentity tls = CreateSelfSignedTls("localsend");
	if (!tls.ctx) {
		fprintf(stderr, "impossibile creare il certificato TLS\n");
		return 1;
	}

	// Identita' del device: fingerprint = SHA-256 del certificato DER.
	DeviceInfo info;
	info.alias = alias;
	info.port = port;
	info.fingerprint = tls.fingerprint;
	info.protocol = "https";

	FileSink sink(destDir);
	std::string err;
	if (!sink.EnsureDir(&err)) {
		fprintf(stderr, "cartella di destinazione non utilizzabile: %s\n",
			err.c_str());
		return 1;
	}

	ReceiveSession session;
	SocketHttpServer server;
	gServer = &server;

	// 1) prepare-upload: verifica PIN, chiedi conferma, genera sessionId+token.
	server.Route("POST", kApiPrepareUpload,
		[&](const HttpRequest& req) {
		// Anti-flood: max 15 prepare-upload ogni 10s -> 429 (server a thread
		// singolo, lo stato static e' sicuro).
		static time_t rateWindowStart = 0;
		static int rateCount = 0;
		time_t now = time(nullptr);
		if (now - rateWindowStart >= 10) {
			rateWindowStart = now;
			rateCount = 0;
		}
		if (++rateCount > 15)
			return HttpServerResponse::Empty(429);
		// Verifica PIN se richiesto.
		if (!requiredPin.empty()) {
			std::string pin = req.Query("pin");
			if (pin != requiredPin) {
				fprintf(stderr, "PIN errato da %s (ricevuto: \"%s\")\n",
					req.headers.count("host")
						? req.headers.at("host").c_str() : "?",
					pin.c_str());
				return HttpServerResponse::Empty(401);
			}
		}

		IncomingPrepareUpload in;
		try {
			in = ParsePrepareUploadRequest(req.body);
		} catch (const std::exception& e) {
			fprintf(stderr, "prepare-upload non valido: %s\n", e.what());
			return HttpServerResponse::Empty(400);
		}

		// Conferma interattiva (a meno di --auto).
		if (!autoAccept) {
			if (!AskConfirmation(in)) {
				printf("Rifiutato.\n");
				return HttpServerResponse::Empty(403);
			}
		} else {
			printf("\nRichiesta da \"%s\" (%s): %zu file\n",
				in.sender.alias.c_str(), in.sender.deviceType.c_str(),
				in.files.size());
			for (const auto& f : in.files) {
				printf("  - %s (%lld byte, %s)\n", f.fileName.c_str(),
					f.size, f.fileType.c_str());
			}
		}

		PrepareOutcome out = session.Prepare(in,
			ReceiveSession::AcceptPolicy(), req.clientHost);
		switch (out.status) {
			case PrepareStatus::SessionBusy:
				return HttpServerResponse::Empty(409);
			case PrepareStatus::NothingAccepted:
				return HttpServerResponse::Empty(204);
			case PrepareStatus::Accepted:
				break;
		}

		printf("Sessione %s avviata; in attesa degli upload...\n",
			out.result.sessionId.c_str());

		return HttpServerResponse::Json(200,
			BuildPrepareUploadResponse(out.result.sessionId,
				out.result.fileTokens).Dump());
	});

	// 2) upload: valida token, scrivi i byte su disco, segna come ricevuto.
	server.Route("POST", kApiUpload, [&](const HttpRequest& req) {
		std::string sessionId = req.Query("sessionId");
		std::string fileId = req.Query("fileId");
		std::string token = req.Query("token");

		// Parametri obbligatori mancanti -> 400.
		if (sessionId.empty() || fileId.empty() || token.empty())
			return HttpServerResponse::Empty(400);

		if (!session.ValidateUpload(sessionId, fileId, token,
				req.clientHost))
			return HttpServerResponse::Empty(403);

		const FileMetadata* meta = session.File(fileId);
		std::string name = meta ? meta->fileName : fileId;
		std::string mimeType = meta ? meta->fileType : "";

		// Verifica integrita' contro lo sha256 dichiarato (se presente).
		if (meta && !meta->sha256.empty()) {
			std::string want = meta->sha256;
			for (char& c : want) {
				if (c >= 'A' && c <= 'Z')
					c += 32;
			}
			if (Sha256Hex(req.body) != want) {
				fprintf(stderr, "sha256 non corrisponde (%s): scartato\n",
					name.c_str());
				return HttpServerResponse::Empty(500);
			}
		}

		std::string outPath, werr;
		if (!sink.Save(name, req.body, mimeType, &outPath, &werr)) {
			fprintf(stderr, "scrittura fallita (%s): %s\n", name.c_str(),
				werr.c_str());
			return HttpServerResponse::Empty(500);
		}

		session.MarkReceived(fileId);
		printf("  [RICEVUTO] %s -> %s (%zu byte)\n", name.c_str(),
			outPath.c_str(), req.body.size());

		if (session.IsComplete()) {
			printf("Sessione completata: tutti i file ricevuti.\n");
			session.Reset();
		}
		return HttpServerResponse::Empty(200);
	});

	// 3) cancel: annulla la sessione in corso.
	server.Route("POST", kApiCancel, [&](const HttpRequest& req) {
		if (session.Cancel(req.Query("sessionId")))
			printf("Sessione annullata dal mittente.\n");
		return HttpServerResponse::Empty(200);
	});

	// 4) register (L2): un dispositivo ci contatta via HTTP per registrarsi.
	server.Route("POST", kApiRegister, [&](const HttpRequest&) {
		return HttpServerResponse::Json(200, info.ToJson().Dump());
	});

	// 5) info: GET per ottenere il DeviceInfo (v1 e v2).
	auto infoHandler = [&](const HttpRequest&) {
		return HttpServerResponse::Json(200, info.ToJson().Dump());
	};
	server.Route("GET", kApiInfo, infoHandler);
	server.Route("GET", "/api/localsend/v1/info", infoHandler);

	server.EnableTls(tls.ctx);

	if (!server.Start(port)) {
		fprintf(stderr, "impossibile aprire la porta %d (occupata?)\n", port);
		return 1;
	}

	// Avvia l'annuncio multicast (L2).
	MulticastAnnouncer announcer(info);
	bool multicastOk = announcer.Start();

	printf("Ricevente: %s (fp %.8s...)\n", info.alias.c_str(),
		info.fingerprint.c_str());
	printf("In ascolto su 0.0.0.0:%d  ->  cartella %s\n", port,
		sink.Dir().c_str());
	if (!requiredPin.empty())
		printf("PIN richiesto: %s\n", requiredPin.c_str());
	if (autoAccept)
		printf("Accettazione automatica attiva\n");
	if (multicastOk)
		printf("Scoperta LAN attiva (multicast %s:%d)\n",
			kMulticastGroup, kDefaultPort);
	else
		fprintf(stderr, "avviso: multicast non disponibile; "
			"raggiungibile solo per IP\n");
	printf("Premi Ctrl-C per uscire.\n");

	server.Run();

	announcer.Stop();
	FreeTlsIdentity(tls);
	printf("\nServer fermato.\n");
	return 0;
}
