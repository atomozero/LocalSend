# LocalSend for Haiku

Native Haiku client for the LocalSend: share files and text messages over LAN with any device (Android, iOS, Windows, macOS, Linux) running LocalSend, without internet and without third-party servers.

![LocalSend on Haiku](screenshots/LocalSend_V1.0.png)

If LocalSend for Haiku saves you time, consider supporting development: [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)


## Features

* Native Haiku GUI and CLI clients
* Secure HTTPS transfers with self-signed TLS certificates
* Automatic LAN device discovery via multicast
* Send files and text messages to any LocalSend-compatible device
* Share files through browser-downloadable links
* **Shared board**: publish files once, every Haiku peer on the LAN sees
  them in real time and can download them (or auto-sync them from favorites)
* **Desktop replicant**: a drop zone on your desktop — drop files to publish
  them on the board, Shift+drop to send them straight to all online favorites
* **Send to circle**: one click sends files to every online favorite at once
* Drag & drop support and Tracker context-menu integration
* Favorites, transfer history, notifications, and PIN protection
* BFS MIME type integration for received files
* Parallel file uploads for improved performance
* Localized via the Haiku Locale Kit (English, Italian, Japanese, Chinese,
  Spanish) — the UI follows your system language automatically
* No external dependencies beyond Haiku system libraries and OpenSSL

## Quick start

### GUI (recommended)

```
make
./LocalSend
```

The GUI window shows discovered devices on the LAN. Select a device and:
- Click **"Send file..."** to send files (or drag files onto the window)
- Click **"Send text..."** to send a text message
- Click **"Send to circle..."** to send files to every online favorite at once
- Click **"Share via link..."** to generate a browser-downloadable link
- Click **"Board"** to open the shared board window
- Click **★** to add/remove a device from favorites

Incoming transfers show an accept/reject dialog (favorites are auto-accepted).

### The circle and the shared board

The **circle** is your set of favorite devices (★). Two ways to share with it:

- **Push**: "Send to circle..." (or Shift+drop on the desktop replicant)
  sends the files immediately to every favorite currently online. With
  "Auto-accept from favorites" enabled on their side, files arrive silently.
- **Board**: publish files on your board (drop them on the desktop replicant,
  or "Board" → "Add files..."). Every Haiku peer on the LAN is notified in
  under a second, sees the list in its own Board window, and downloads what
  it wants with a double click. Favorites can enable
  **"Auto-download from favorites' boards"** (Settings → Security) to sync
  new board files automatically — already-downloaded files are remembered
  and never fetched twice.

The board is served on port 53318 with the standard LocalSend v2.1 download
API, so official LocalSend clients (and browsers, via the share link) can
also browse and download it. **"Board visible to favorites only"**
(Settings → Security) restricts it to your circle.

### Desktop replicant

Enable **"Show replicants"** in the Deskbar menu, open
Settings → Integration, and drag the drop-zone widget onto your desktop.
The replicant shows a badge with the number of files on your board and a
green dot per online favorite; it survives reboots and keeps working even
when the app is closed (drops relaunch it automatically). Right-click it
for quick actions, remove it via its dragger menu.

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
make                 # builds everything: GUI, CLI tools, add-on, replicants, catalogs
make install-addon   # installs Tracker context menu add-on
make install-catalogs # installs locale catalogs for testing (non-packaged)
make check           # runs all protocol-level unit tests
make test-receive    # receive-side protocol tests only
make test-board      # shared-board protocol tests only
make clean           # removes all build artifacts
```

### Translations

The UI uses the Haiku Locale Kit and follows your system language
(Preferences → Locale). Translatable strings are the `B_TRANSLATE(...)`
calls in the source; translations live in `locales/<lang>.catkeys`.

To add or update a language:

```
make catkeys              # regenerate locales/en.catkeys after string changes
# copy en.catkeys to locales/<lang>.catkeys and fill in the translations
make catalogs             # compile locales/*.catkeys into .catalog files
make install-catalogs     # install them for local testing
```

Keep the fingerprint on the first line of each `.catkeys` identical to
`en.catkeys` — it ties the translations to the exact set of source strings.

Note: the `LocalSendDesktop` replicant binary is registered in the MIME
database by `mimeset` during the build. If you move it, run
`mimeset -f LocalSendDesktop` on the new location so Tracker can restore
the desktop replicant after a reboot.

## Be careful
> **Developer's Note**: This software may contain traces of peanuts and LLM. It has been developed with passion for the Haiku platform.

## Support

If you find this project useful, you can buy me a coffee: [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)
