// localsend-receive: ricevente L1. Alza un server HTTP e riceve file da una
// istanza LocalSend (es. telefono) che fa da mittente. Niente scoperta
// automatica (e' L2): il mittente raggiunge questa macchina per IP.
//
//   localsend-receive [--dir CARTELLA] [--port PORTA] [--alias NOME]
//
// Esempio:
//   localsend-receive --dir ./ricevuti
//
// I file accettati vengono scritti in CARTELLA (default ./ricevuti). In L1
// l'accettazione e' automatica; conferma interattiva e PIN arriveranno con L3.

#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <string>

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
	fprintf(stderr, "uso: %s [--dir CARTELLA] [--port PORTA] [--alias NOME]\n",
		argv0);
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

	std::string destDir = "./ricevuti";
	int port = kDefaultPort;
	std::string alias = "Haiku Box";

	for (int i = 1; i < argc; ++i) {
		std::string a = argv[i];
		if (a == "--dir" && i + 1 < argc) {
			destDir = argv[++i];
		} else if (a == "--port" && i + 1 < argc) {
			port = atoi(argv[++i]);
		} else if (a == "--alias" && i + 1 < argc) {
			alias = argv[++i];
		} else {
			Usage(argv[0]);
			return 2;
		}
	}

	// Identita' del device (per ora solo a scopo di log; usata davvero da L2).
	DeviceInfo info;
	info.alias = alias;
	info.port = port;
	info.fingerprint = LoadOrCreateFingerprint("./localsend_fingerprint");

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

	// 1) prepare-upload: leggi metadati, accetta, genera sessionId + token.
	server.Route("POST", kApiPrepareUpload, [&](const HttpRequest& req) {
		IncomingPrepareUpload in;
		try {
			in = ParsePrepareUploadRequest(req.body);
		} catch (const std::exception& e) {
			fprintf(stderr, "prepare-upload non valido: %s\n", e.what());
			return HttpServerResponse::Empty(400);
		}

		PrepareOutcome out = session.Prepare(in); // accetta tutto in L1
		switch (out.status) {
			case PrepareStatus::SessionBusy:
				return HttpServerResponse::Empty(409);
			case PrepareStatus::NothingAccepted:
				return HttpServerResponse::Empty(204);
			case PrepareStatus::Accepted:
				break;
		}

		printf("\nRichiesta da \"%s\" (%s): %zu file\n",
			in.sender.alias.c_str(), in.sender.deviceType.c_str(),
			in.files.size());
		for (const auto& f : in.files) {
			printf("  - %s (%lld byte, %s)\n", f.fileName.c_str(), f.size,
				f.fileType.c_str());
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

		if (!session.ValidateUpload(sessionId, fileId, token))
			return HttpServerResponse::Empty(403);

		const FileMetadata* meta = session.File(fileId);
		std::string name = meta ? meta->fileName : fileId;

		std::string outPath, werr;
		if (!sink.Save(name, req.body, &outPath, &werr)) {
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

	if (!server.Start(port)) {
		fprintf(stderr, "impossibile aprire la porta %d (occupata?)\n", port);
		return 1;
	}

	printf("Ricevente: %s (fp %.8s...)\n", info.alias.c_str(),
		info.fingerprint.c_str());
	printf("In ascolto su 0.0.0.0:%d  ->  cartella %s\n", port,
		sink.Dir().c_str());
	printf("Premi Ctrl-C per uscire.\n");

	server.Run();

	printf("\nServer fermato.\n");
	return 0;
}
