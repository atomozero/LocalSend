# LocalSend for Haiku

Native Haiku client for the LocalSend v2.1 protocol: share files over LAN with
any device (Android, iOS, Windows, macOS, Linux) running LocalSend, without
internet and without third-party servers.

## Features

- **GUI application** (`LocalSend`) with device discovery, file sending,
  accept/reject dialog, system notifications, and settings
- **CLI tools** (`localsend-send`, `localsend-receive`) for terminal use
- **HTTPS** with self-signed certificates (TLS via OpenSSL 3)
- **Automatic discovery** via UDP multicast (224.0.0.167:53317)
- **BFS integration**: received files get the correct MIME type attribute
- **Localized** in Italian, English, Japanese, Chinese, and Spanish
  (auto-detects system language)
- **PIN protection** and interactive accept/reject for incoming transfers
- Zero external dependencies beyond Haiku system libraries and OpenSSL

## Quick start

### GUI (recommended)

```
make
./LocalSend
```

The GUI window shows discovered devices on the LAN. Select a device and click
"Send file..." to send, or wait for incoming transfers (an accept/reject dialog
will appear). Settings are accessible from the main window.

### CLI

**Send files:**
```
./localsend-send <host> <file> [file...] [--pin PIN] [--port PORT] [--alias NAME]

# Example: send to a phone running LocalSend
./localsend-send 192.168.1.42 ./photo.png ./notes.txt --pin 123456
```

**Receive files:**
```
./localsend-receive [--dir FOLDER] [--port PORT] [--alias NAME] [--pin PIN] [--auto]

# Example: receive into ./received with PIN
./localsend-receive --dir ./received --pin 1234

# Auto-accept all incoming transfers (no confirmation prompt)
./localsend-receive --auto
```

## Build

Requires Haiku with GCC, OpenSSL (`openssl_devel`), and standard system
libraries (`libbe`, `libnetwork`, `libtracker`).

```
make                 # builds localsend-send, localsend-receive, LocalSend (GUI)
make test-receive    # runs protocol-level unit tests (no network needed)
make clean           # removes all build artifacts
```

## Architecture

```
src/
  protocol/          # Shared models, JSON, constants, fingerprint (L0-L3)
  net/               # HTTP client/server, TLS, multicast announcer
  client/            # Sender logic (UploadSession, FileSource)
  server/            # Receiver logic (ReceiveSession, FileSink + BFS attrs)
  app/               # CLI entry points + GUI application + i18n
```

The protocol layer is transport-independent and fully tested. The GUI combines
sender, receiver, and discovery in a single BApplication with a background
HTTPS server thread.

## Protocol (v2.1)

- Multicast UDP 224.0.0.167:53317 for device discovery
- HTTPS TCP 53317 for file transfer
- Upload API: `prepare-upload` (metadata) -> `upload` (binary) -> `cancel`
- Source: official `localsend/protocol` repository

## Documentation (Italian)

- `docs/00-verifica-haiku.md`: technical verification checklist for Haiku
- `docs/01-design-L0.md`: sender design (L0)
- `docs/02-design-L1.md`: receiver design (L1)
- `docs/coding-style.md`: coding style (Haiku guidelines)

## Milestone status

| Level | Description | Status |
|-------|-------------|--------|
| L0 | Haiku sender (HTTPS) | Done |
| L1 | Haiku receiver (HTTPS server) | Done |
| L2 | Automatic discovery (multicast + /register + /info) | Done |
| L3 | Security (TLS self-signed, fingerprint, PIN, accept/reject) | Done |
| L4 | Haiku integration (BFS MIME attrs, GUI, notifications, i18n) | Done |
| L5 | Download API (browser mode) | Planned |

## License

MIT. See `LICENSE`.
