// Verifica che una libreria JSON header-only compili e giri col gcc di Haiku.
// Scelta consigliata: nlohmann/json (header singolo). Scaricare json.hpp accanto
// a questo file (in tools/check/) prima di compilare.
//   curl -L -o json.hpp \
//     https://github.com/nlohmann/json/releases/latest/download/json.hpp
// Compila:  gcc -std=c++17 -o test_json test_json.cpp
// Successo = round-trip parse/serialize con i campi del prepare-upload.
//
// Nota onesta: non posso confermare da qui che nlohmann/json sia pacchettizzato
// su Haiku. La via sicura e' vendorizzare l'header nel repo.

#include "json.hpp"
#include <cstdio>
#include <string>

using nlohmann::json;

int main() {
    json req = {
        {"info", {
            {"alias", "Haiku Box"},
            {"version", "2.1"},
            {"deviceModel", "Haiku"},
            {"deviceType", "desktop"},
            {"fingerprint", "random-string"},
            {"port", 53317},
            {"protocol", "http"},
            {"download", false}
        }},
        {"files", {
            {"file-1", {
                {"id", "file-1"},
                {"fileName", "foto.png"},
                {"size", 324242},
                {"fileType", "image/png"},
                {"sha256", nullptr},
                {"preview", nullptr},
                {"metadata", {
                    {"modified", "2026-06-06T14:00:00Z"},
                    {"accessed", "2026-06-06T14:00:00Z"}
                }}
            }}
        }}
    };

    std::string text = req.dump();
    json back = json::parse(text);

    std::string name = back["files"]["file-1"]["fileName"].get<std::string>();
    long size = back["files"]["file-1"]["size"].get<long>();
    printf("round-trip ok: fileName=%s size=%ld\n", name.c_str(), size);

    // Simula la risposta del ricevente.
    json resp = json::parse(
        "{\"sessionId\":\"abc\",\"files\":{\"file-1\":\"tok-xyz\"}}");
    printf("sessionId=%s token(file-1)=%s\n",
           resp["sessionId"].get<std::string>().c_str(),
           resp["files"]["file-1"].get<std::string>().c_str());
    return 0;
}
