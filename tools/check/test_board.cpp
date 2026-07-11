// Test host-side della BACHECA (L6): logica di protocollo pura, senza rete.
// Copre l'estensione boardRev dell'annuncio (compatibilita' con i client
// ufficiali inclusa) e la simmetria prepare-download tra il lato che pubblica
// (route del download server) e il lato che osserva (thread di fetch).
//
// Compila dalla radice del repo con "make test-board" (vedi Makefile).

#include <cstdio>
#include <string>

#include "protocol/Json.h"
#include "protocol/Models.h"

static int gFailures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                    \
			std::printf("  FAIL: %s (riga %d)\n", #cond, __LINE__);       \
			++gFailures;                                                  \
		}                                                                 \
	} while (0)

using namespace LocalSend;


static void
TestBoardRevSerialization()
{
	std::printf("DeviceInfo.boardRev: serializzazione condizionale...\n");

	// boardRev = 0 (default): il campo NON deve comparire, cosi' i payload
	// standard restano identici finche' la bacheca non e' mai stata usata.
	DeviceInfo plain;
	plain.alias = "Haiku Box";
	plain.fingerprint = "fp-1";
	CHECK(!plain.ToJson().Has("boardRev"));

	// boardRev > 0: campo presente e rileggibile (round-trip).
	DeviceInfo board = plain;
	board.boardRev = 7;
	board.download = true;
	JsonValue j = board.ToJson();
	CHECK(j.Has("boardRev"));
	DeviceInfo back = DeviceInfo::FromJson(j);
	CHECK(back.boardRev == 7);
	CHECK(back.download == true);
	CHECK(back.alias == "Haiku Box");
}


static void
TestBoardRevCompat()
{
	std::printf("DeviceInfo.boardRev: compatibilita' client ufficiali...\n");

	// Annuncio di un client LocalSend ufficiale: niente boardRev.
	// FromJson deve tornare 0 senza errori.
	JsonValue official = JsonValue::Parse(
		"{\"alias\":\"Pixel\",\"version\":\"2.1\","
		"\"fingerprint\":\"fp-android\",\"port\":53317,"
		"\"protocol\":\"https\",\"download\":false,\"announce\":true}");
	DeviceInfo d = DeviceInfo::FromJson(official);
	CHECK(d.boardRev == 0);
	CHECK(d.alias == "Pixel");

	// Campo sconosciuto in piu' (futuri dialetti): ignorato senza errori.
	JsonValue withExtra = JsonValue::Parse(
		"{\"alias\":\"X\",\"fingerprint\":\"f\",\"boardRev\":3,"
		"\"unknownField\":[1,2,3]}");
	CHECK(DeviceInfo::FromJson(withExtra).boardRev == 3);
}


static void
TestAnnounceRoundTrip()
{
	std::printf("Annuncio multicast con boardRev: round-trip...\n");

	// Lato annunciante: come _AnnounceJson del MulticastAnnouncer.
	DeviceInfo info;
	info.alias = "Scrivania";
	info.fingerprint = "fp-haiku";
	info.deviceType = "desktop";
	info.boardRev = 2;
	info.download = true;
	JsonValue ann = info.ToJson();
	ann["announce"] = true;
	std::string wire = ann.Dump();

	// Lato ascoltatore: come il loop di ricezione dell'announcer.
	JsonValue msg = JsonValue::Parse(wire);
	CHECK(msg.Has("alias") && msg.Has("fingerprint"));
	CHECK(msg.At("announce").AsBool(false));
	CHECK(msg.Has("boardRev"));
	CHECK(static_cast<int>(msg.At("boardRev").AsInt(0)) == 2);
	CHECK(msg.At("deviceType").AsString() == "desktop");
}


// Costruisce la risposta prepare-download come la route del download server
// (info + sessionId fissa + mappa fileId -> metadati).
static std::string
MakePrepareDownloadResponse()
{
	DeviceInfo info;
	info.alias = "Scrivania";
	info.fingerprint = "fp-haiku";
	info.boardRev = 5;
	info.download = true;

	FileMetadata a;
	a.id = "file-1";
	a.fileName = "relazione.pdf";
	a.size = 123456;
	a.fileType = "application/pdf";

	FileMetadata b;
	b.id = "file-2";
	b.fileName = "foto.png";
	b.size = 999;
	b.fileType = "image/png";

	JsonValue root = JsonValue::Object();
	root["info"] = info.ToJson();
	root["sessionId"] = "board";
	JsonValue files = JsonValue::Object();
	files["file-1"] = a.ToJson();
	files["file-2"] = b.ToJson();
	root["files"] = files;
	return root.Dump();
}


static void
TestPrepareDownloadSymmetry()
{
	std::printf("prepare-download: pubblicante <-> osservatore...\n");

	// Lato osservatore: come il thread di fetch di kMsgPeerBoardChanged.
	JsonValue root = JsonValue::Parse(MakePrepareDownloadResponse());
	CHECK(root.At("sessionId").AsString() == "board");
	CHECK(root.Has("info"));
	CHECK(DeviceInfo::FromJson(root.At("info")).boardRev == 5);

	CHECK(root.Has("files"));
	int count = 0;
	bool foundPdf = false;
	for (const auto& kv : root.At("files").Items()) {
		FileMetadata fm = FileMetadata::FromJson(kv.second);
		std::string id = fm.id.empty() ? kv.first : fm.id;
		CHECK(!id.empty());
		if (fm.fileName == "relazione.pdf") {
			foundPdf = true;
			CHECK(id == "file-1");
			CHECK(fm.size == 123456);
			CHECK(fm.fileType == "application/pdf");
		}
		count++;
	}
	CHECK(count == 2);
	CHECK(foundPdf);
}


static void
TestFileIdFallbackFromKey()
{
	std::printf("prepare-download: fileId dalla chiave della mappa...\n");

	// Un client che omette "id" nei metadati: la chiave della mappa e' la
	// fonte autorevole (stessa regola di ParsePrepareUploadRequest).
	JsonValue root = JsonValue::Parse(
		"{\"sessionId\":\"board\",\"files\":{"
		"\"file-9\":{\"fileName\":\"senza-id.txt\",\"size\":10}}}");
	for (const auto& kv : root.At("files").Items()) {
		FileMetadata fm = FileMetadata::FromJson(kv.second);
		std::string id = fm.id.empty() ? kv.first : fm.id;
		CHECK(id == "file-9");
		CHECK(fm.fileName == "senza-id.txt");
	}
}


int
main()
{
	TestBoardRevSerialization();
	TestBoardRevCompat();
	TestAnnounceRoundTrip();
	TestPrepareDownloadSymmetry();
	TestFileIdFallbackFromKey();

	if (gFailures == 0) {
		std::printf("\nTUTTI I TEST OK\n");
		return 0;
	}
	std::printf("\n%d TEST FALLITI\n", gFailures);
	return 1;
}
