# Design L0: Haiku come solo mittente HTTP

Obiettivo L0: una macchina Haiku invia uno o piu' file a una istanza LocalSend
esistente (es. telefono) che fa da ricevente/server. Nessun server, nessun
multicast lato Haiku: IP del destinatario inserito a mano. Traguardo: vedere un
file muoversi.

Vincolo guida: il protocollo richiede che UNA sola parte alzi il server HTTP.
Facendo fare a Haiku il mittente, L0 dipende solo dal client HTTP, il terreno
piu' solido. Cosi' si dimostra il protocollo end to end prima di affrontare il
server (L1).

## 1. Le sole chiamate HTTP necessarie a L0

Tutte verso il ricevente, su `http://<ip>:53317` in L0 (TLS arriva in L3).

### 1.1 prepare-upload (solo metadati)
`POST /api/localsend/v2/prepare-upload`  (PIN opzionale: `?pin=123456`)

Body:
```json
{
  "info": {
    "alias": "Haiku Box",
    "version": "2.1",
    "deviceModel": "Haiku",
    "deviceType": "desktop",
    "fingerprint": "<stringa casuale persistente>",
    "port": 53317,
    "protocol": "http",
    "download": false
  },
  "files": {
    "<fileId>": {
      "id": "<fileId>",
      "fileName": "foto.png",
      "size": 324242,
      "fileType": "image/png",
      "sha256": null,
      "preview": null,
      "metadata": {
        "modified": "2026-06-06T14:00:00Z",
        "accessed": "2026-06-06T14:00:00Z"
      }
    }
  }
}
```

Risposta attesa (il ricevente accetta o accetta in parte):
```json
{
  "sessionId": "<sessionId>",
  "files": { "<fileId>": "<token>" }
}
```
Se un fileId non compare nella risposta, quel file e' stato rifiutato: non
inviarlo. Gestire i codici 401/403 (PIN errato/rifiuto), 409 (sessione in corso),
429 (troppe richieste), 500.

### 1.2 upload (dati binari)
`POST /api/localsend/v2/upload?sessionId=<s>&fileId=<f>&token=<t>`

Body = byte grezzi del file (un upload per file). In L0: sequenziale. In seguito
parallelizzabile. Il `token` e' quello restituito per quel preciso fileId.

### 1.3 cancel (annullo)
`POST /api/localsend/v2/cancel?sessionId=<s>`

Da chiamare se l'utente annulla o in caso di errore a meta' sessione.

## 2. Costruzione dei metadati file

Per ogni file da inviare:
- `id`: identificatore stabile nella sessione (es. UUID o indice + nome).
- `fileName`: nome base.
- `size`: byte, da `stat()` / `BNode`.
- `fileType`: MIME. Su Haiku usare il MIME nativo (`BNodeInfo::GetType`,
  fallback `BMimeType::GuessMimeType`); fallback finale `application/octet-stream`.
- `sha256`: in L0 lasciare `null` (calcolo opzionale; attivarlo dopo con OpenSSL).
- `preview`: `null` in L0 (Translation Kit arriva piu' tardi).
- `metadata.modified` / `metadata.accessed`: da `st_mtim`/`st_atim` convertiti in
  ISO 8601 UTC (`YYYY-MM-DDThh:mm:ssZ`).

## 3. Gestione sessionId e token

- `prepare-upload` ritorna un `sessionId` per la sessione e un `token` PER FILE.
- Tenere una mappa `fileId -> token` piu' il `sessionId` in un oggetto
  `UploadSession`.
- Ogni `upload` usa sessionId + fileId + il suo token.
- Alla fine o su errore: opzionale `cancel(sessionId)`.
- Questo modello (sessionId + mappa token) e' SIMMETRICO: in L1 il ricevente
  GENERA sessionId e token e li valida. Stessa struttura dati, lato opposto.

## 4. Struttura del codice (pensata perche' L1 si innesti senza riscrivere)

```
src/
  protocol/            # condiviso L0 + L1, indipendente dal trasporto
    Constants.h        # gruppo multicast, porta 53317, path API, versione
    Models.h/.cpp      # DeviceInfo, FileMetadata, PrepareUploadRequest/Response
    Json.h/.cpp        # (de)serializzazione modelli <-> JSON (nlohmann)
    Fingerprint.h/.cpp # genera/persiste la stringa casuale (HTTP) ; SHA in L3

  net/                 # interfacce di trasporto
    IHttpClient.h      # interfaccia usata da L0
    IHttpServer.h      # interfaccia DEFINITA ora, implementata in L1
    haiku/
      SocketHttpClient.* # client HTTP su socket BSD (o client BeAPI se comodo)
      # SocketHttpServer.*  -> L1

  client/              # logica mittente (L0)
    UploadSession.*    # orchestrazione prepare-upload -> upload -> cancel
    FileSource.*       # apre i file, ricava size/mime/tempi, costruisce metadati

  app/
    main_send.cpp      # CLI L0: localsend-send <ip> <file...> [--pin 123456]
```

Principi che rendono L1 un innesto, non una riscrittura:
- `protocol/` non sa nulla di client o server: modelli e JSON sono condivisi.
- `IHttpClient` e `IHttpServer` sono interfacce separate. L0 implementa/usa solo
  il client; L1 aggiunge l'implementazione del server senza toccare L0.
- `UploadSession` (mittente) e il futuro `ReceiveSession` (ricevente) usano la
  STESSA `protocol/`: stessi modelli, stesso JSON, stessa nozione di
  sessionId+token, lati opposti.
- Le costanti (porte, path, versione protocollo) vivono in un solo posto.

## 5. Flusso di UploadSession (L0)

```
1. Costruisci DeviceInfo locale (alias, fingerprint persistente, deviceType=desktop).
2. Per ogni file: FileSource -> FileMetadata.
3. POST prepare-upload (info + files). Parse risposta -> sessionId, map token.
4. Per ogni fileId presente nella risposta:
       POST upload?sessionId&fileId&token  con i byte del file.
   (sequenziale in L0; parallelo dopo)
5. Su errore o annullo: POST cancel?sessionId.
6. Riporta esito per file (inviato / rifiutato / errore).
```

## 6. Cosa NON fare in L0 (resta per i milestone successivi)

- Niente server, niente `/register`, niente multicast (L1/L2).
- Niente TLS ne' fingerprint da certificato: in HTTP il fingerprint e' una
  stringa casuale (L3 introduce HTTPS e SHA-256 del cert).
- Niente attributi BFS, Replicant, Tracker (L4).
- Niente Download API (L5).

## 7. Definition of done L0

- `localsend-send <ip> <file>` invia con successo un file a un telefono con
  LocalSend ufficiale, che mostra la richiesta e riceve il file integro.
- Gestiti almeno: accettazione parziale, rifiuto (file assente in risposta),
  PIN, e annullo via cancel.
