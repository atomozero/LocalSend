# Stile del codice

Questo progetto e' un'applicazione nativa Haiku e ne segue le linee guida
ufficiali: https://www.haiku-os.org/development/coding-guidelines/

L'allineamento allo stile Haiku non e' estetica fine a se stessa: rende il codice
familiare a chi sviluppa su Haiku e coerente con le BeAPI che usiamo.

## Regole applicate

### Indentazione e righe
- Indentazione con **TAB**, con l'editor impostato a 4 colonne per tab.
- Spazi consentiti solo per allineare DOPO i tab di indentazione.
- Lunghezza massima di riga: **80 colonne**. Nelle righe spezzate, le
  continuazioni hanno almeno un tab in piu' rispetto alla riga base.

### Nomi
- **UpperCamelCase** (InterCaps, niente underscore) per: namespace, classi,
  struct, enum, type, funzioni e metodi. Esempi: `LocalSend`, `ReceiveSession`,
  `BuildPrepareUpload()`, `ParseQuery()`.
- **lowerCamelCase** per variabili locali e parametri: `sessionId`, `fileTokens`.
- **Prefisso `f`** per le variabili membro PRIVATE delle classi: `fListenFd`,
  `fSessionId`, `fGenerator`. I campi pubblici degli struct dati (aggregati come
  `DeviceInfo`, `FileMetadata`, `HttpRequest`) restano SENZA prefisso, in linea
  con la pratica Haiku per gli aggregati pubblici (`rgb_color`, `BRect`).
- **Prefisso `g`** per le globali, **`s`** per le statiche di file/funzione.
- **Prefisso `k`** per le costanti: `kDefaultPort`, `kApiUpload`.
- **MAIUSCOLO** per le macro.

### Puntatori e riferimenti
- `*` e `&` attaccati al tipo: `char* buffer`, `const std::string& path`.

### Graffe e blocchi
- Costrutti di controllo: graffa di apertura sulla **stessa riga**
  (`if (x) {`, `for (...) {`, `while (...) {`), `} else {` sulla stessa riga.
- Definizioni di funzione/metodo: graffa di apertura su **riga propria**.
- Statement a riga singola: niente graffe, lo statement va su una riga nuova.
- Due righe vuote tra le definizioni di funzione nei file .cpp.

### Header
- **Include guard** invece di `#pragma once`:
  ```
  #ifndef _RECEIVE_SESSION_H
  #define _RECEIVE_SESSION_H
  ...
  #endif // _RECEIVE_SESSION_H
  ```

### Commenti
- Stile `//` (anche su piu' righe consecutive).

## Verifica

Non esiste un formatter automatico configurato per questo repo; l'allineamento
e' mantenuto a mano. Prima di committare, controllare almeno indentazione (tab),
nomi (f/k/Upper/lower) e graffe. Su Haiku esiste `haiku-format` (basato su
clang-format) come riferimento, ma non e' richiesto dal flusso di build.
