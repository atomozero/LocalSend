# LocalSend per Haiku

Client nativo Haiku per il protocollo LocalSend v2.1: scambio file in LAN con
qualsiasi dispositivo (Android, iOS, Windows, macOS, Linux) che usi LocalSend,
senza internet e senza server di terze parti.

Stato attuale: L0 (mittente) e L1 (ricevente) implementati. `localsend-send`
invia file a una istanza LocalSend esistente; `localsend-receive` alza un server
HTTP e riceve file. Zero dipendenze esterne (socket BSD + modulo JSON interno),
compila su Haiku con un solo comando.

L1 (ricevente) e' stato provato end-to-end su host (mittente -> ricevente in
loopback): i file arrivano con SHA-256 identico all'origine. Resta da verificare
il comportamento di rete su Haiku reale e contro un client LocalSend ufficiale.
La logica del protocollo ha anche test host-side: `make test-receive`.

## Build ed uso

```
make                 # su Haiku (usa -lnetwork): compila send + receive
```

Mittente (L0):
```
./localsend-send <host> <file> [file...] [--pin PIN] [--port PORTA] [--alias NOME]

# esempio: invio a un telefono con LocalSend
./localsend-send 192.168.1.42 ./foto.png ./note.txt --pin 123456
```

Ricevente (L1):
```
./localsend-receive [--dir CARTELLA] [--port PORTA] [--alias NOME]

# esempio: ricevi nella cartella ./ricevuti (default)
./localsend-receive --dir ./ricevuti
```

In L1 l'accettazione e' automatica e i nomi file vengono sanificati (si scrive
solo dentro CARTELLA). Conferma interattiva e PIN arriveranno con L3. Va ora
provato contro un client LocalSend ufficiale su Haiku reale.

## Documenti

- `docs/00-verifica-haiku.md`: come accertare le incognite (HTTP, multicast,
  OpenSSL, JSON) sul sistema Haiku reale. Nessuna libreria e' data per scontata.
- `docs/01-design-L0.md`: design di L0 (Haiku come solo mittente HTTP).
- `docs/02-design-L1.md`: design di L1 (Haiku ricevente) e cosa e' gia' pronto.
- `docs/coding-style.md`: stile del codice (linee guida Haiku) seguito dal repo.

## De-risking e test

`tools/check/` contiene piccoli programmi da compilare ed eseguire su Haiku per
provare multicast UDP, trasporto TCP nei due versi, OpenSSL e JSON. Vedi
`tools/check/README.md`.

`tools/check/test_receive.cpp` e' invece un test host-side (logica di protocollo
pura, niente rete): gira ovunque con `make test-receive` e verifica il lato
ricevente e la simmetria mittente/ricevente.

## Milestone (ognuno spedibile da solo)

- L0: Haiku mittente HTTP verso una istanza LocalSend esistente. [fatto]
- L1: Haiku ricevente (alza il server HTTP). [fatto, da provare su Haiku reale]
- L2: scoperta automatica (multicast UDP + /register).
- L3: sicurezza (HTTPS self-signed, fingerprint, PIN).
- L4: anima Haiku (attributi BFS, Tracker query, Replicant, notifiche).
- L5: Download API (modalita' browser, mittente universale).

## Fatti del protocollo (v2.1)

- Multicast UDP 224.0.0.167:53317; HTTP TCP 53317.
- Serve che UNA sola parte alzi il server HTTP.
- Upload API: prepare-upload (metadati) -> upload (binario) -> cancel.
- Fonte: repo ufficiale `localsend/protocol`, README v2.1.

## Licenza

MIT. Vedi il file `LICENSE`.
