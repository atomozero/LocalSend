// localsend-send: mittente L0. Invia uno o piu' file a una istanza LocalSend
// esistente (es. telefono) che fa da ricevente/server.
//
//   localsend-send <host> <file> [file...] [--pin 123456] [--port 53317]
//                  [--alias "Nome"]
//
// Esempio:
//   localsend-send 192.168.1.42 ./foto.png ./note.txt --pin 123456

#include <signal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "client/FileSource.h"
#include "client/UploadSession.h"
#include "net/haiku/SocketHttpClient.h"
#include "protocol/Constants.h"
#include "protocol/Fingerprint.h"

using namespace ls;

namespace {

void usage(const char* argv0) {
    fprintf(stderr,
            "uso: %s <host> <file> [file...] [--pin PIN] [--port PORTA] "
            "[--alias NOME]\n",
            argv0);
}

const char* statusText(FileOutcome::Status s) {
    switch (s) {
        case FileOutcome::Status::Sent:     return "INVIATO";
        case FileOutcome::Status::Rejected: return "RIFIUTATO";
        case FileOutcome::Status::Error:    return "ERRORE";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN); // evita crash se il peer chiude a meta' upload

    if (argc < 3) { usage(argv[0]); return 2; }

    std::string host = argv[1];
    int port = kDefaultPort;
    std::string pin;
    std::string alias = "Haiku Box";
    std::vector<std::string> paths;

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--pin" && i + 1 < argc) { pin = argv[++i]; }
        else if (a == "--port" && i + 1 < argc) { port = atoi(argv[++i]); }
        else if (a == "--alias" && i + 1 < argc) { alias = argv[++i]; }
        else if (a.rfind("--", 0) == 0) {
            fprintf(stderr, "opzione sconosciuta: %s\n", a.c_str());
            usage(argv[0]);
            return 2;
        } else {
            paths.push_back(a);
        }
    }

    if (paths.empty()) { fprintf(stderr, "nessun file da inviare\n"); return 2; }

    // Costruisci i metadati dei file.
    std::vector<FileMetadata> files;
    for (size_t i = 0; i < paths.size(); ++i) {
        FileMetadata m;
        std::string id = "file-" + std::to_string(i + 1);
        if (!buildFileMetadata(paths[i], id, m)) {
            fprintf(stderr, "impossibile leggere il file: %s\n", paths[i].c_str());
            return 1;
        }
        files.push_back(m);
    }

    // Identita' del device: fingerprint persistente accanto all'eseguibile.
    DeviceInfo info;
    info.alias = alias;
    info.port = kDefaultPort;
    info.protocol = "http";
    info.fingerprint = loadOrCreateFingerprint("./localsend_fingerprint");

    printf("Mittente: %s (fp %.8s...)\n", info.alias.c_str(),
           info.fingerprint.c_str());
    printf("Destinazione: %s:%d  file: %zu%s\n", host.c_str(), port, files.size(),
           pin.empty() ? "" : "  (con PIN)");

    SocketHttpClient http;
    UploadSession session(http, info);
    SendReport report = session.send(host, port, files, pin);

    if (!report.prepared) {
        const char* hint = "";
        switch (report.prepareStatus) {
            case 0:   hint = " (connessione fallita: host/porta?)"; break;
            case 204: hint = " (nessun file accettato)"; break;
            case 401: hint = " (PIN richiesto o errato)"; break;
            case 403: hint = " (richiesta rifiutata)"; break;
            case 409: hint = " (un'altra sessione e' in corso)"; break;
            case 429: hint = " (troppe richieste)"; break;
        }
        fprintf(stderr, "prepare-upload fallito: HTTP %d%s\n",
                report.prepareStatus, hint);
        return 1;
    }

    printf("Sessione: %s\n", report.sessionId.c_str());
    for (const auto& f : report.files) {
        printf("  [%-9s] %s%s%s\n", statusText(f.status), f.fileName.c_str(),
               f.detail.empty() ? "" : "  -> ", f.detail.c_str());
    }

    return report.allSent() ? 0 : 1;
}
