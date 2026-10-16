# Site permissions (Pacitum 1.7.5)

All grants are scoped per origin and per profile (`<profile>/pacitum-site-permissions.json`; in-memory for guest/off-the-record). Nothing is granted without an explicit user decision; a dismissed/timed-out prompt is not a grant.

## Implemented
- Camera, microphone, location, notifications, clipboard, mouse/pointer lock, persistent storage, automatic downloads, popups, screen sharing, USB, HID, serial, Bluetooth, MIDI.
- Qt-native kinds via `QWebEnginePage::permissionRequested`; Qt's own store is set to in-memory and mirrored from Pacitum's store.
- Screen share picker (Tabs / Windows / Entire screen) for `desktopMediaRequested`, live screen previews via `QScreen::grabWindow`.
- USB/HID/Serial/Bluetooth/MIDI/storage via a per-page MainWorld `QWebEngineScript` shim + `QWebChannel` (`pacitumPermissions`), nonce-protected, top frame only. Serial does real I/O through QSerialPort.
- Multiple automatic downloads held and prompted (downloadRequested); popups blocked without a user gesture (createWindow), with omnibox chip and "Always allow".
- Omnibox chips, prompt bubble (500 ms anti-clickjack guard), site controls panel, Settings > Site settings pages, tab recording indicators, title/taskbar recording marker; 1.7.0 motion timings; reduced motion = opacity-only.
- `--permissions-test` self-test, run in CI.

## Not possible (and why)
- Camera live preview, mic level meter, choosing a specific camera/mic in the prompt: Qt WebEngine has no device-selection hook, and opening the device via QtMultimedia would contend with the page for exclusive camera access.
- "Share audio" toggle for screen share: QWebEngineDesktopMediaRequest has no audio option.
- WebUSB / WebHID / Web Bluetooth GATT I/O: Qt WebEngine ships no backend for them. Pacitum provides the chooser and per-origin grant; `open()`/`gatt.connect()` reject with NotSupportedError.
- Web MIDI and persistent storage: after Pacitum consent, actual behavior is Chromium's implementation (Qt exposes no grant hook).
- Tab sharing: implemented by frame-polling the tab into a canvas stream: video only, no tab audio, background tabs can show stale frames.
- Window share thumbnails: Qt only gives window names, so window tiles show an icon, not a live preview.
- Held downloads are cancelled and re-issued via `page->download()` (Qt deletes un-accepted downloads after the signal); POST/blob downloads may not re-issue.
- A blocked `window.open()` returns a window object instead of `null` (Qt requires returning a page).
- "Delete data" clears cookies only: Qt has no per-origin storage-clearing API.
- Site grouping uses an eTLD+1 heuristic, not the full Public Suffix List.
- OS-level camera/mic denial detection is Windows only (ConsentStore); not implemented on macOS/Linux.
- `qt.webChannelTransport` is visible to page scripts (Qt limitation); the bridge enforces origin + nonce checks.
