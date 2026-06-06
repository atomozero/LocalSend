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

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL: %s (riga %d)\n", #cond, __LINE__);       \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

using namespace ls;

// Costruisce un body prepare-upload realistico usando il lato MITTENTE, cosi' il
// test verifica anche la simmetria send <-> receive.
static std::string makeRequestBody() {
    DeviceInfo info;
    info.alias = "iPhone di Andrea";
    info.fingerprint = "abc123";
    info.deviceType = "mobile";

    FileMetadata a;
    a.id = "file-1"; a.fileName = "foto.png"; a.size = 324242;
    a.fileType = "image/png"; a.modified = "2026-06-06T14:00:00Z";

    FileMetadata b;
    b.id = "file-2"; b.fileName = "note.txt"; b.size = 12;
    b.fileType = "text/plain";

    return buildPrepareUpload(info, {a, b}).dump();
}

static void testParseRequest() {
    std::printf("parsePrepareUploadRequest...\n");
    IncomingPrepareUpload req = parsePrepareUploadRequest(makeRequestBody());

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

static void testBuildResponseRoundTrip() {
    std::printf("buildPrepareUploadResponse round-trip...\n");
    std::map<std::string, std::string> tokens = {
        {"file-1", "tok-A"}, {"file-2", "tok-B"}};
    std::string body = buildPrepareUploadResponse("sess-1", tokens).dump();

    // Il MITTENTE deve poterla rileggere: simmetria response <-> request.
    PrepareUploadResult r = parsePrepareUploadResponse(body);
    CHECK(r.sessionId == "sess-1");
    CHECK(r.fileTokens.size() == 2);
    CHECK(r.fileTokens["file-1"] == "tok-A");
    CHECK(r.fileTokens["file-2"] == "tok-B");
}

// Generatore di token deterministico per asserzioni stabili.
static ReceiveSession::TokenGen counter() {
    auto n = std::make_shared<int>(0);
    return [n]() { return "T" + std::to_string((*n)++); };
}

static void testSessionAcceptAll() {
    std::printf("ReceiveSession: accetta tutto, flusso completo...\n");
    IncomingPrepareUpload req = parsePrepareUploadRequest(makeRequestBody());
    ReceiveSession s(counter());

    PrepareOutcome out = s.prepare(req);
    CHECK(out.status == PrepareStatus::Accepted);
    CHECK(s.active());
    // Token assegnati ai file (T0, T1), poi sessionId (T2).
    CHECK(out.result.fileTokens.size() == 2);
    CHECK(out.result.sessionId == "T2");
    CHECK(s.sessionId() == "T2");
    CHECK(s.file("file-1") != nullptr);
    CHECK(s.file("file-1")->fileName == "foto.png");

    const std::string& tok1 = out.result.fileTokens["file-1"];
    // Validazioni.
    CHECK(s.validateUpload("T2", "file-1", tok1));
    CHECK(!s.validateUpload("WRONG", "file-1", tok1));        // sessione errata
    CHECK(!s.validateUpload("T2", "file-1", "bad"));          // token errato
    CHECK(!s.validateUpload("T2", "ignoto", tok1));           // file ignoto

    // Completamento.
    CHECK(!s.isComplete());
    CHECK(s.markReceived("file-1"));
    CHECK(!s.isComplete());
    CHECK(s.markReceived("file-2"));
    CHECK(s.isComplete());
    // Dopo aver ricevuto, niente doppioni.
    CHECK(!s.validateUpload("T2", "file-1", tok1));
}

static void testSessionPartialAndBusy() {
    std::printf("ReceiveSession: accettazione parziale e sessione occupata...\n");
    IncomingPrepareUpload req = parsePrepareUploadRequest(makeRequestBody());
    ReceiveSession s(counter());

    // Accetta solo i .png.
    auto onlyPng = [](const FileMetadata& f) { return f.fileType == "image/png"; };
    PrepareOutcome out = s.prepare(req, onlyPng);
    CHECK(out.status == PrepareStatus::Accepted);
    CHECK(out.result.fileTokens.size() == 1);
    CHECK(out.result.fileTokens.count("file-1") == 1);
    CHECK(s.file("file-2") == nullptr); // rifiutato: assente dalla sessione

    // Una seconda prepare mentre la prima e' attiva -> occupato.
    PrepareOutcome busy = s.prepare(req);
    CHECK(busy.status == PrepareStatus::SessionBusy);

    // Cancel libera la sessione.
    CHECK(!s.cancel("altro-id"));
    CHECK(s.cancel(s.sessionId()));
    CHECK(!s.active());

    // Ora una nuova prepare riparte.
    PrepareOutcome again = s.prepare(req, onlyPng);
    CHECK(again.status == PrepareStatus::Accepted);
}

static void testSessionNothingAccepted() {
    std::printf("ReceiveSession: nessun file accettato -> niente sessione...\n");
    IncomingPrepareUpload req = parsePrepareUploadRequest(makeRequestBody());
    ReceiveSession s(counter());

    auto rejectAll = [](const FileMetadata&) { return false; };
    PrepareOutcome out = s.prepare(req, rejectAll);
    CHECK(out.status == PrepareStatus::NothingAccepted);
    CHECK(!s.active());
    CHECK(s.sessionId().empty());
}

static void testQueryParsing() {
    std::printf("parseQuery / urlDecode...\n");
    auto q = parseQuery("sessionId=abc&fileId=file-1&token=t%20x");
    CHECK(q["sessionId"] == "abc");
    CHECK(q["fileId"] == "file-1");
    CHECK(q["token"] == "t x");           // %20 -> spazio

    CHECK(urlDecode("a%2Fb") == "a/b");
    CHECK(urlDecode("hello+world") == "hello world");
    CHECK(urlDecode("%ZZbad") == "%ZZbad"); // sequenza malformata invariata

    // Uso realistico via HttpRequest.q().
    HttpRequest r;
    r.query = parseQuery("sessionId=s1&pin=1234");
    CHECK(r.q("sessionId") == "s1");
    CHECK(r.q("pin") == "1234");
    CHECK(r.q("assente").empty());
}

int main() {
    testParseRequest();
    testBuildResponseRoundTrip();
    testSessionAcceptAll();
    testSessionPartialAndBusy();
    testSessionNothingAccepted();
    testQueryParsing();

    if (g_failures == 0) {
        std::printf("\nTUTTI I TEST OK\n");
        return 0;
    }
    std::printf("\n%d TEST FALLITI\n", g_failures);
    return 1;
}
