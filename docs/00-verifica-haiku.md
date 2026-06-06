# Verifica delle incognite tecniche su Haiku (gate del progetto)

Questo documento dice COME accertare ogni incognita sul sistema Haiku reale.
Niente qui e' dato per scontato. Dove non posso confermare la disponibilita' di
una libreria, lo dico apertamente: va verificato eseguendo i comandi indicati.

Tutto va fatto su una installazione Haiku reale (o VM), non su questo container.

## 0. Toolchain di base (preliminare a tutto)

```
gcc --version
ld --version
ls /system/develop/headers/os
ls /system/develop/headers/posix
```

Note:
- Haiku usa gcc moderno sulle build a 64 bit. Sulle build ibride a 32 bit puo'
  esistere anche un gcc2 legacy: in quel caso usare `setarch x86` per il gcc
  moderno. Verificare con `gcc --version` quale e' attivo.
- Le BeAPI stanno sotto `/system/develop/headers/os`.

## 1. Server HTTP lato BeAPI (il punto piu' critico)

Stato che mi aspetto, DA VERIFICARE:
- Esiste un CLIENT HTTP nella Network Kit (Network Services). NON risulta esistere
  un SERVER HTTP pronto nella BeAPI.

Come accertare il client HTTP:
```
ls /system/develop/headers/os/ | grep -i -E 'http|url|netservice'
find /system/develop/headers -iname '*Http*'
```
Le API recenti vivono nel namespace `BPrivate::Network` (header tipo
`HttpSession.h`, `HttpRequest.h`). Il nome esatto e' cambiato tra versioni di
Haiku: NON fidarsi della memoria, leggere gli header presenti sul sistema.

Come accertare l'assenza di un server HTTP pronto:
```
find /system/develop/headers -iname '*Server*' | grep -i http
```
Se non trova nulla di HTTP-server (atteso), la conclusione e': il server (L1) va
costruito sopra i socket BSD (BSocket / `<sys/socket.h>`) oppure portando una
libreria. Vedi il test `tools/check/test_tcp_http.cpp`, che dimostra entrambe le
direzioni con i soli socket POSIX, indipendentemente da classi BeAPI incerte.

Decisione pratica: per de-rischiare sia L0 sia L1 senza dipendere da nomi di
classi BeAPI che potrebbero non esistere, la base di trasporto puo' essere i
socket BSD (portabili e garantiti su Haiku). Il client HTTP BeAPI, se presente e
comodo, resta un'opzione per L0.

## 2. Socket multicast UDP (224.0.0.167:53317)

I socket BSD ci sono su Haiku (`<sys/socket.h>`, `<netinet/in.h>`,
`<arpa/inet.h>`). Il multicast usa `setsockopt` con `IP_ADD_MEMBERSHIP`,
`IP_MULTICAST_TTL`, `IP_MULTICAST_LOOP`. La presenza degli header non garantisce
il comportamento a runtime: va provato.

Come accertare:
```
gcc -o test_multicast tools/check/test_multicast.cpp -lnetwork
# Terminale A (ricevente):
./test_multicast recv
# Terminale B (mittente):
./test_multicast send
```
Successo = il ricevente stampa il pacchetto inviato. Provare anche tra due
macchine diverse sulla stessa LAN, non solo in loopback.

Nota: su Haiku la libreria di rete si linka con `-lnetwork`.

## 3. OpenSSL (TLS self-signed + SHA-256)

OpenSSL e' tipicamente disponibile come pacchetto su Haiku, DA VERIFICARE:
```
pkgman search openssl
pkgman install openssl_devel   # se non gia' presente
openssl version
find /system/develop/headers -ipath '*openssl*' -name 'sha.h'
```
Gli header attesi stanno sotto `/system/develop/headers/openssl`. Verificare che
ci siano sia i .h sia le librerie linkabili (`-lssl -lcrypto`).

Come accertare a livello di compilazione/esecuzione:
```
gcc -o test_openssl tools/check/test_openssl.cpp -lcrypto
./test_openssl
```
Successo = stampa la versione OpenSSL e l'hash SHA-256 di una stringa nota.

Generazione certificato self-signed al volo (per L3, verifica anticipata):
```
openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem \
  -days 3650 -subj "/CN=localsend"
# fingerprint = SHA-256 del certificato DER:
openssl x509 -in cert.pem -outform DER | openssl dgst -sha256
```
Questo riproduce a mano cio' che il client dovra' fare via API OpenSSL.

## 4. Libreria JSON C++ portabile

Le risposte del protocollo sono JSON; BMessage non basta per l'interoperabilita'.

Opzioni e come verificarle:
- Haiku ha un parser JSON PRIVATO interno (`BJson`), ma e' API privata e
  instabile: sconsigliato per il protocollo. Non assumerne forma o presenza.
- Pacchetto di sistema, DA VERIFICARE:
  ```
  pkgman search nlohmann
  pkgman search json
  ```
- Scelta consigliata e robusta: vendorizzare `nlohmann/json` (header singolo).
  Non dipende da pacchetti Haiku. Verifica che compili con il gcc di Haiku:
  ```
  # scaricare json.hpp in tools/check/ poi:
  gcc -std=c++17 -o test_json tools/check/test_json.cpp
  ./test_json
  ```
  Non posso confermare da qui che `nlohmann/json` sia pacchettizzato su Haiku:
  la via sicura e' includerne l'header nel repo.

## 5. MIME e tempi file (gia' nativi, utili per i metadati)

Per `fileType` (MIME) e per `metadata.modified/accessed`:
- MIME nativo: `BNodeInfo::GetType` / `BMimeType::GuessMimeType` (BeAPI). Da
  verificare leggendo `update_mime_database` e gli header `Mime.h`, `NodeInfo.h`.
- Tempi: `BStatable`/`BNode` o `stat()` POSIX (`st_mtim`, `st_atim`) da
  convertire in ISO 8601 UTC.

```
find /system/develop/headers/os -name 'Mime.h' -o -name 'NodeInfo.h'
```

## Riepilogo gate

| Incognita | Comando chiave | Esito che sblocca |
|-----------|----------------|-------------------|
| Client HTTP BeAPI | `find headers -iname '*Http*'` | header presenti |
| Server HTTP | socket BSD via `test_tcp_http` | listen+accept funziona |
| Multicast UDP | `test_multicast send/recv` | pacchetto ricevuto |
| OpenSSL | `test_openssl` + `openssl version` | hash e versione ok |
| JSON | `test_json` con header vendorizzato | parse/serialize ok |

Finche' questi non passano sull'Haiku reale, non impegnarsi oltre L0.
