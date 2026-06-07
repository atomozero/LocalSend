// Test host-side del lato RICEVENTE (L1-prep). A differenza degli altri file in
// tools/check (che vanno provati su Haiku reale per il comportamento di rete),
// questo e' logica di protocollo pura: compila e gira su qualunque host.
//
// Compila dalla radice del repo con "make test-receive" (vedi Makefile), che
// raccoglie i sorgenti di protocollo + server e lancia l'eseguibile.

#include <cstdio>
#include <memory>
#include <string>

#include "net/IHttpServer.h"
#include "protocol/Models.h"
#include "server/ReceiveSession.h"

static int gFailures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("  FAIL: %s (riga %d)\n", #cond, __LINE__);       \
			++gFailures;                                                  \
		}                                                                 \
	} while (0)

using namespace LocalSend;

// Costruisce un body prepare-upload realistico usando il lato MITTENTE, cosi'
// il test verifica anche la simmetria send <-> receive.
static std::string
MakeRequestBody()
{
	DeviceInfo info;
	info.alias = "iPhone di Andrea";
	info.fingerprint = "abc123";
	info.deviceType = "mobile";

	FileMetadata a;
	a.id = "file-1";
	a.fileName = "foto.png";
	a.size = 324242;
	a.fileType = "image/png";
	a.modified = "2026-06-06T14:00:00Z";

	FileMetadata b;
	b.id = "file-2";
	b.fileName = "note.txt";
	b.size = 12;
	b.fileType = "text/plain";

	return BuildPrepareUpload(info, {a, b}).Dump();
}


static void
TestParseRequest()
{
	std::printf("ParsePrepareUploadRequest...\n");
	IncomingPrepareUpload req = ParsePrepareUploadRequest(MakeRequestBody());

	CHECK(req.sender.alias == "iPhone di Andrea");
	CHECK(req.sender.fingerprint == "abc123");
	CHECK(req.sender.deviceType == "mobile");
	CHECK(req.files.size() == 2);
	CHECK(req.files[0].id == "file-1");
	CHECK(req.files[0].fileName == "foto.png");
	CHECK(req.files[0].size == 324242);
	CHECK(req.files[0].fileType == "image/png");
	CHECK(req.files[0].modified == "2026-06-06T14:00:00Z");
	CHECK(req.files[0].sha256.empty());   // era null
	CHECK(req.files[1].fileName == "note.txt");
}


static void
TestBuildResponseRoundTrip()
{
	std::printf("BuildPrepareUploadResponse round-trip...\n");
	std::map<std::string, std::string> tokens = {
		{"file-1", "tok-A"}, {"file-2", "tok-B"}};
	std::string body = BuildPrepareUploadResponse("sess-1", tokens).Dump();

	// Il MITTENTE deve poterla rileggere: simmetria response <-> request.
	PrepareUploadResult r = ParsePrepareUploadResponse(body);
	CHECK(r.sessionId == "sess-1");
	CHECK(r.fileTokens.size() == 2);
	CHECK(r.fileTokens["file-1"] == "tok-A");
	CHECK(r.fileTokens["file-2"] == "tok-B");
}


// Generatore di token deterministico per asserzioni stabili.
static ReceiveSession::TokenGen
Counter()
{
	auto n = std::make_shared<int>(0);
	return [n]() { return "T" + std::to_string((*n)++); };
}


static void
TestSessionAcceptAll()
{
	std::printf("ReceiveSession: accetta tutto, flusso completo...\n");
	IncomingPrepareUpload req = ParsePrepareUploadRequest(MakeRequestBody());
	ReceiveSession s(Counter());

	PrepareOutcome out = s.Prepare(req);
	CHECK(out.status == PrepareStatus::Accepted);
	CHECK(s.IsActive());
	// Token assegnati ai file (T0, T1), poi sessionId (T2).
	CHECK(out.result.fileTokens.size() == 2);
	CHECK(out.result.sessionId == "T2");
	CHECK(s.SessionId() == "T2");
	CHECK(s.File("file-1") != nullptr);
	CHECK(s.File("file-1")->fileName == "foto.png");

	const std::string& tok1 = out.result.fileTokens["file-1"];
	// Validazioni.
	CHECK(s.ValidateUpload("T2", "file-1", tok1));
	CHECK(!s.ValidateUpload("WRONG", "file-1", tok1));        // sessione errata
	CHECK(!s.ValidateUpload("T2", "file-1", "bad"));          // token errato
	CHECK(!s.ValidateUpload("T2", "ignoto", tok1));           // file ignoto

	// Completamento.
	CHECK(!s.IsComplete());
	CHECK(s.MarkReceived("file-1"));
	CHECK(!s.IsComplete());
	CHECK(s.MarkReceived("file-2"));
	CHECK(s.IsComplete());
	// Dopo aver ricevuto, niente doppioni.
	CHECK(!s.ValidateUpload("T2", "file-1", tok1));
}


static void
TestSessionPartialAndBusy()
{
	std::printf("ReceiveSession: accettazione parziale/occupata...\n");
	IncomingPrepareUpload req = ParsePrepareUploadRequest(MakeRequestBody());
	ReceiveSession s(Counter());

	// Accetta solo i .png.
	auto onlyPng = [](const FileMetadata& f) {
		return f.fileType == "image/png";
	};
	PrepareOutcome out = s.Prepare(req, onlyPng);
	CHECK(out.status == PrepareStatus::Accepted);
	CHECK(out.result.fileTokens.size() == 1);
	CHECK(out.result.fileTokens.count("file-1") == 1);
	CHECK(s.File("file-2") == nullptr); // rifiutato: assente dalla sessione

	// Una seconda prepare mentre la prima e' attiva -> occupato.
	PrepareOutcome busy = s.Prepare(req);
	CHECK(busy.status == PrepareStatus::SessionBusy);

	// Cancel libera la sessione.
	CHECK(!s.Cancel("altro-id"));
	CHECK(s.Cancel(s.SessionId()));
	CHECK(!s.IsActive());

	// Ora una nuova prepare riparte.
	PrepareOutcome again = s.Prepare(req, onlyPng);
	CHECK(again.status == PrepareStatus::Accepted);
}


static void
TestSessionNothingAccepted()
{
	std::printf("ReceiveSession: nessun file accettato...\n");
	IncomingPrepareUpload req = ParsePrepareUploadRequest(MakeRequestBody());
	ReceiveSession s(Counter());

	auto rejectAll = [](const FileMetadata&) { return false; };
	PrepareOutcome out = s.Prepare(req, rejectAll);
	CHECK(out.status == PrepareStatus::NothingAccepted);
	CHECK(!s.IsActive());
	CHECK(s.SessionId().empty());
}


static void
TestQueryParsing()
{
	std::printf("ParseQuery / UrlDecode...\n");
	auto q = ParseQuery("sessionId=abc&fileId=file-1&token=t%20x");
	CHECK(q["sessionId"] == "abc");
	CHECK(q["fileId"] == "file-1");
	CHECK(q["token"] == "t x");           // %20 -> spazio

	CHECK(UrlDecode("a%2Fb") == "a/b");
	CHECK(UrlDecode("hello+world") == "hello world");
	CHECK(UrlDecode("%ZZbad") == "%ZZbad"); // sequenza malformata invariata

	// Uso realistico via HttpRequest::Query().
	HttpRequest r;
	r.query = ParseQuery("sessionId=s1&pin=1234");
	CHECK(r.Query("sessionId") == "s1");
	CHECK(r.Query("pin") == "1234");
	CHECK(r.Query("assente").empty());
}


int
main()
{
	TestParseRequest();
	TestBuildResponseRoundTrip();
	TestSessionAcceptAll();
	TestSessionPartialAndBusy();
	TestSessionNothingAccepted();
	TestQueryParsing();

	if (gFailures == 0) {
		std::printf("\nTUTTI I TEST OK\n");
		return 0;
	}
	std::printf("\n%d TEST FALLITI\n", gFailures);
	return 1;
}
