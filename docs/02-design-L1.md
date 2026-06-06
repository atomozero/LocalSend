# Design L1: Haiku come ricevente HTTP

Obiettivo L1: una macchina Haiku ALZA il server HTTP e riceve uno o piu' file da
una istanza LocalSend esistente (es. telefono) che fa da mittente. Ancora niente
multicast (la scoperta e' L2): il mittente raggiunge Haiku per IP. Traguardo:
vedere un file ARRIVARE su Haiku e finire su disco integro.

Vincolo guida (lo stesso di L0): UNA sola parte alza il server HTTP. In L1 quella
parte e' Haiku. Cambia il lato del trasporto, NON il protocollo: i modelli, il
JSON e la nozione di sessionId+token sono gli stessi di L0, usati a specchio.

## 1. Cosa cambia rispetto a L0 (e cosa NO)

| Aspetto | L0 (mittente) | L1 (ricevente) |
|---|---|---|
| Trasporto | client HTTP (`IHttpClient`) | server HTTP (`IHttpServer`) |
| sessionId + token | li RICEVE e usa | li GENERA e valida |
| prepare-upload | lo COSTRUISCE | lo LEGGE e risponde |
| upload | invia i byte | riceve i byte e scrive su disco |
| `protocol/` | invariato | invariato (condiviso) |

Il lato protocollo del ricevente e' gia' pronto e testato in L1-prep:
`parsePrepareUploadRequest`, `buildPrepareUploadResponse`, `ReceiveSession`,
`parseQuery`/`urlDecode`. Cio' che resta da fare in L1 e' SOLO il trasporto
(server su socket BSD) e la scrittura su disco.

## 2. Gli endpoint che il server deve esporre

Tutti su `http://0.0.0.0:53317` in L1 (TLS in L3). Path in `protocol/Constants.h`.

### 2.1 `POST /api/localsend/v2/prepare-upload`
- Body in ingresso: `parsePrepareUploadRequest(body)` -> `IncomingPrepareUpload`
  (info del mittente + lista file).
- `ReceiveSession::prepare(req, accept)` decide e assegna i token. Mappa diretta
  su HTTP:
  - `Accepted`        -> `200` + `buildPrepareUploadResponse(sessionId, tokens).dump()`
  - `NothingAccepted` -> `204` (nessun file accettato)
  - `SessionBusy`     -> `409` (un'altra sessione in corso)
- PIN (quando arrivera', vedi L3): `?pin=...` letto via `req.q("pin")`; se errato
  rispondere `401`.

### 2.2 `POST /api/localsend/v2/upload?sessionId=&fileId=&token=`
- Estrarre i tre parametri con `parseQuery` (o `req.q(...)`).
- `ReceiveSession::validateUpload(sessionId, fileId, token)`:
  - falso -> `403` (sessione/fileId/token non validi, o file gia' ricevuto).
  - vero  -> scrivere il body (byte grezzi) nel file di destinazione, poi
    `ReceiveSession::markReceived(fileId)` e rispondere `200`.
- A `isComplete()` true: sessione finita; chiamare `reset()` per accettarne una
  nuova. (In L1 una sessione alla volta, come il protocollo prevede.)

### 2.3 `POST /api/localsend/v2/cancel?sessionId=`
- `ReceiveSession::cancel(sessionId)` -> `reset()` se combacia. Rispondere `200`.
- Rimuovere/segnare i file parziali gia' scritti per quella sessione.

## 3. Il trasporto: SocketHttpServer (la sola parte nuova rischiosa)

Da implementare in `src/net/haiku/SocketHttpServer.{h,cpp}` realizzando
`IHttpServer`. Base: socket BSD (gia' de-rischiati in `tools/check/test_tcp_http`).

Responsabilita' minime:
- `start(port)`: `socket()` + `SO_REUSEADDR` + `bind()` + `listen()`. `false` se il
  bind fallisce (porta occupata).
- loop `accept()`; per connessione: leggere la request line + header (fino a
  `\r\n\r\n`), separare `path` da query string, riempire `HttpRequest`
  (`method`, `path`, `query=parseQuery(...)`, `headers` in minuscolo, `body`).
- leggere il body secondo `content-length` (l'upload puo' essere grande: leggere
  a blocchi e, per l'endpoint upload, streammare su file senza tenerlo in RAM).
- instradare su (metodo, path) -> `Handler` -> `HttpServerResponse` -> scrivere la
  risposta (status line, `Content-Type`, `Content-Length`, body).
- `stop()`: chiudere il socket di ascolto e uscire dal loop.

Concorrenza in L1: accettabile UN client alla volta (il protocollo usa una sola
sessione). Thread/connessioni multiple sono un raffinamento successivo.

Nota onesta: il body dell'upload NON va deserializzato come JSON — sono byte
grezzi del file. Solo prepare-upload e cancel hanno (eventuale) JSON. Lo
streaming diretto su disco dell'upload e' il punto da curare per file grandi.

## 4. Scrittura su disco

- Cartella di destinazione configurabile (default: una cartella "ricevuti").
- Nome file: `FileMetadata::fileName` ottenuto da `ReceiveSession::file(fileId)`.
  Sanitizzare il nome (niente `/`, niente `..`) per non scrivere fuori cartella.
- Scrivere su un file temporaneo e rinominare a fine upload, cosi' un upload
  interrotto non lascia un file dall'aspetto valido.
- Su annullo/errore: rimuovere i temporanei della sessione.

## 5. Struttura del codice (innesto su quanto esiste)

```
src/
  protocol/            # CONDIVISO, invariato (gia' usato da L0 e dai test L1)
  net/
    IHttpServer.h        # interfaccia server (gia' definita in L1-prep)
    HttpServerSupport.cpp# urlDecode + parseQuery (portabili, gia' testati)
    haiku/
      SocketHttpServer.* # DA FARE in L1: IHttpServer su socket BSD
  server/
    ReceiveSession.*     # logica ricevente (gia' fatta e testata in L1-prep)
    FileSink.*           # DA FARE in L1: scrittura sicura su disco
  app/
    main_receive.cpp     # DA FARE in L1: CLI "localsend-receive [--dir ...]"
```

Tutto IMPLEMENTATO. Logica di protocollo con test `tools/check/test_receive.cpp`;
catena completa provata end-to-end su host (mittente -> ricevente in loopback,
file con SHA-256 identico all'origine):
- `IHttpServer.h`, `HttpServerSupport.cpp` (urlDecode/parseQuery).
- `ReceiveSession` completo: prepare/validate/markReceived/isComplete/cancel.
- `parsePrepareUploadRequest` / `buildPrepareUploadResponse` con round-trip
  verificato contro il lato mittente.
- `SocketHttpServer` (server HTTP/1.1 su socket BSD, monothread).
- `FileSink` (scrittura su disco: sanifica il nome, .part + rename, no overwrite).
- `main_receive.cpp` (CLI `localsend-receive`) + target di build.

Resta da verificare su HARDWARE: comportamento di rete su Haiku reale e
interoperabilita' con un client LocalSend ufficiale (telefono). Possibili
raffinamenti: streaming dell'upload su disco senza tenerlo in RAM, e concorrenza
multi-connessione.

## 6. Flusso del ricevente (L1)

```
1. start(53317). Registra gli handler dei 3 endpoint.
2. prepare-upload: parse richiesta -> ReceiveSession::prepare -> 200/204/409.
3. upload (per file): valida token -> streamma il body su file temp ->
   markReceived. A isComplete: rinomina i temp definitivi, reset().
4. cancel: cancel(sessionId) -> rimuovi i temp -> reset().
5. Logga per file: ricevuto / rifiutato / errore.
```

## 7. Definition of done L1

- `localsend-receive` in ascolto su Haiku riceve un file inviato da un client
  LocalSend ufficiale (telefono), che lo vede comparire e completarsi; il file su
  disco e' integro (size e contenuto corretti).
- Gestiti almeno: accettazione (200), accettazione parziale, nessun file (204),
  sessione occupata (409), token errato (403), annullo via cancel.
- Nessun file scritto fuori dalla cartella di destinazione (nome sanificato).

## 8. Cosa NON fare in L1 (resta per i milestone successivi)

- Niente multicast ne' `/register` (L2): in L1 il mittente raggiunge Haiku per IP.
- Niente TLS ne' PIN da certificato (L3): si resta in HTTP.
- Niente attributi BFS, Tracker, Replicant (L4) ne' Download API (L5).
