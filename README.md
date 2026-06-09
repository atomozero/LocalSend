# LocalSend for Haiku

Native Haiku client for the LocalSend v2.1 protocol: share files and text
messages over LAN with any device (Android, iOS, Windows, macOS, Linux)
running LocalSend, without internet and without third-party servers.

![LocalSend on Haiku](screenshots/LocalSend_V1.0.png)

## Features

- **GUI application** (`LocalSend`) with device discovery, file/text sending,
  accept/reject dialog, system notifications, settings, transfer history,
  favorites, and share-via-link
- **CLI tools** (`localsend-send`, `localsend-receive`) for terminal use
- **HTTPS** with self-signed certificates (TLS via OpenSSL 3)
- **Automatic discovery** via UDP multicast (224.0.0.167:53317)
- **Text messaging** — send and receive text messages (shown as popup dialog)
- **Download API** — share files via browser link (any device on LAN can
  download without LocalSend installed)
- **Drag & drop** — drop files from Tracker onto the window to send
- **Tracker add-on** — right-click files in Tracker and choose
  "Send with LocalSend"
- **Favorites** — mark devices for auto-accept with a gold star
- **Transfer history** — persistent log of sent/received files
- **BFS integration** — received files get the correct MIME type attribute
- **Parallel uploads** — multiple files are sent simultaneously
- **Localized** in Italian, English, Japanese, Chinese, and Spanish
  (auto-detects system language)
- **PIN protection** and interactive accept/reject for incoming transfers
- **Command-line file arguments** — `./LocalSend photo.png` opens the GUI
  with the file ready to send
- Zero external dependencies beyond Haiku system libraries and OpenSSL

## Quick start

### GUI (recommended)

```
make
./LocalSend
```

The GUI window shows discovered devices on the LAN. Select a device and:
- Click **"Send file..."** to send files (or drag files onto the window)
- Click **"Send text..."** to send a text message
- Click **"Share via link..."** to generate a browser-downloadable link
- Click **★** to add/remove a device from favorites

Incoming transfers show an accept/reject dialog (favorites are auto-accepted).

### Tracker integration

```
make install-addon
```

Right-click any file in Tracker → Add-ons → **"Send with LocalSend"**.

### CLI

**Send files:**
```
./localsend-send <host> <file> [file...] [--pin PIN] [--port PORT] [--alias NAME]
```

**Receive files:**
```
./localsend-receive [--dir FOLDER] [--port PORT] [--alias NAME] [--pin PIN] [--auto]
```

## Build

Requires Haiku with GCC, OpenSSL (`openssl_devel`), and standard system
libraries (`libbe`, `libnetwork`, `libtracker`).

```
make                 # builds everything: GUI, CLI tools, Tracker add-on
make install-addon   # installs Tracker context menu add-on
make test-receive    # runs protocol-level unit tests
make clean           # removes all build artifacts
```

## Be careful

This software may contain traces of peanuts and LLM.

## Support

If you find this project useful, you can buy me a coffee:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)
