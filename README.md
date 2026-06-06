# LocalSend per Haiku

Client nativo Haiku per il protocollo LocalSend v2.1: scambio file in LAN con
qualsiasi dispositivo (Android, iOS, Windows, macOS, Linux) che usi LocalSend,
senza internet e senza server di terze parti.

Stato attuale: fase di progettazione. Nessun client implementato ancora. Prima
si verificano le incognite tecniche su Haiku, poi si parte dal milestone L0.

## Documenti

- `docs/00-verifica-haiku.md`: come accertare le incognite (HTTP, multicast,
  OpenSSL, JSON) sul sistema Haiku reale. Nessuna libreria e' data per scontata.
- `docs/01-design-L0.md`: design di L0 (Haiku come solo mittente HTTP).

## De-risking

`tools/check/` contiene piccoli programmi da compilare ed eseguire su Haiku per
provare multicast UDP, trasporto TCP nei due versi, OpenSSL e JSON. Vedi
`tools/check/README.md`.

## Milestone (ognuno spedibile da solo)

- L0: Haiku mittente HTTP verso una istanza LocalSend esistente.
- L1: Haiku ricevente (alza il server HTTP).
- L2: scoperta automatica (multicast UDP + /register).
- L3: sicurezza (HTTPS self-signed, fingerprint, PIN).
- L4: anima Haiku (attributi BFS, Tracker query, Replicant, notifiche).
- L5: Download API (modalita' browser, mittente universale).

## Fatti del protocollo (v2.1)

- Multicast UDP 224.0.0.167:53317; HTTP TCP 53317.
- Serve che UNA sola parte alzi il server HTTP.
- Upload API: prepare-upload (metadati) -> upload (binario) -> cancel.
- Fonte: repo ufficiale `localsend/protocol`, README v2.1.
