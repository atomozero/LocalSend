# tools/check: programmi di de-risking per Haiku

Piccoli programmi per accertare le incognite tecniche PRIMA di scrivere il
client vero. Vanno compilati ed eseguiti su Haiku reale (o VM), non in CI.
Dettagli e interpretazione in `../../docs/00-verifica-haiku.md`.

## Build (su Haiku)

```
gcc -o test_multicast test_multicast.cpp -lnetwork
gcc -o test_tcp_http  test_tcp_http.cpp  -lnetwork
gcc -o test_openssl   test_openssl.cpp   -lcrypto        # serve openssl_devel
gcc -std=c++17 -o test_json test_json.cpp                # serve json.hpp accanto
```

## Esecuzione

```
# multicast (due terminali / due macchine)
./test_multicast recv
./test_multicast send

# trasporto TCP nei due versi
./test_tcp_http server
./test_tcp_http client 127.0.0.1 53317 /

# openssl
./test_openssl

# json (dopo aver messo json.hpp in questa cartella)
./test_json
```

## Cosa ciascuno dimostra

- test_multicast: invio/ricezione su 224.0.0.167:53317 (gate per L2 discovery).
- test_tcp_http: listen+accept (gate per il server L1) e GET client (base L0),
  con i soli socket BSD, senza dipendere da classi BeAPI incerte.
- test_openssl: SHA-256 e versione (gate per sha256 file e fingerprint, L3).
- test_json: la libreria JSON compila e gira col gcc di Haiku (gate trasversale).

Onesta': compilare non basta. Il valore e' il comportamento a runtime su Haiku.
