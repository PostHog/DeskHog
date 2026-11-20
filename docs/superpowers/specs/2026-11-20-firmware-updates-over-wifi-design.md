# Firmware updates over WiFi: design

## Why

Updating DeskHog's firmware needs a USB cable and either PlatformIO or a flashing page.
Plugins already update over WiFi; the firmware should too. Tim chose the one-button
form on 2026-11-20: the device's web page says a newer version exists and installs it.

## Goals

1. The Device tab shows the firmware version, says when a newer release exists, and
   installs it with one button and a confirmation.
2. The same path takes a file: "Choose a file" on the page and
   `tools/deskhog update [file]`, for builds from source.
3. An update that fails or loses power halfway leaves a device that can be updated
   again over WiFi, without a cable.
4. Moving an existing device to the new flash layout keeps its plugins, cards, WiFi
   and settings.
5. Releases publish what the page needs, without anyone doing it by hand.

## Non-goals

- No signed firmware and no login. Anyone on the network can already install plugins
  and restart the device; this adds "install firmware" to that list. The guide says so.
- No automatic updates. A person presses the button.
- The device itself never calls out to check for or download an update. The browser does.
- No backup and restore feature on the web page. The move to the new layout is done by
  a script over USB (below).
- The updater does not update itself over WiFi in this version (see "Later").
- No WiFi setup in the updater. It uses the saved network.

## Anti-goals

- A device that needs a cable after a failed update.
- An update that silently loses plugins, cards or settings.
- A screen that looks dead during an update.
- A plugin starting an update: `POST /api/update/start` refuses requests that come
  from the device itself, as `/api/restart` does.

## Constraints

- 4 MB flash. The firmware is 2,070 KB (2,120,128 bytes) and cannot be stored twice.
- A browser page cannot download GitHub release files (no CORS header; checked
  2026-11-20). It can download from `raw.githubusercontent.com` and call
  `api.github.com` (both send `access-control-allow-origin: *`; checked the same day).
- The page is served over plain HTTP, so `crypto.subtle` is not available in it:
  the hash check happens on the device.
- Secrets in plugin settings are never sent by the device. That stays true.
- Task rules from `tech-details.md` apply to the main firmware as before.

## Approaches

- **A small updater firmware in its own slot, fed by the browser (chosen).** The
  device restarts into the updater; the page that started the update sends it the new
  firmware; the updater writes it over the main slot. The browser does the download,
  so the updater needs no TLS and stays small.
- **The updater downloads the release itself.** No browser needed while updating, but
  it needs TLS and certificates in the updater, which no longer fits beside the main
  firmware. Rejected.
- **Two full slots.** The standard way. 4.2 MB of firmware in 3.9 MB of flash, and
  nothing left for plugins. Rejected.
- **A one-button page that flashes over USB (Web Serial).** Cheap and keeps the whole
  layout, but still needs the cable, which is the thing to get rid of. Rejected for
  this work; the README's browser install stays as it is for first installs.

## Flash layout

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000      unchanged
otadata,  data, ota,     0xf000,   0x2000      unchanged
phy_init, data, phy,     0x11000,  0x1000      unchanged
updater,  app,  factory, 0x20000,  0x100000    1,024 KB (new)
ota_0,    app,  ota_0,   0x120000, 0x220000    2,176 KB (was 2,944 KB at 0x20000)
spiffs,   data, spiffs,  0x340000, 0xC0000       768 KB (was 1,024 KB at 0x300000)
```

- The bootloader starts `ota_0` when `otadata` says so and the factory slot (the
  updater) when `otadata` is blank. That gives the switch in both directions, and a
  device whose main firmware is missing or half-written lands in the updater.
- The main firmware has about 106 KB to grow. The build fails when it no longer fits,
  and `tech-details.md` states the ceiling.
- The updater measured 912 KB as a bare test (WiFi, a web server, the write and a
  hash). The real one adds the name on the network and a status on the screen, and
  must fit in 1,024 KB with room to spare. If it does not, the screen status goes
  first (the LED takes over), not the slot size.
- NVS keeps its place, so WiFi, the device's settings and the time zone survive.

## How an update goes

1. The page asks `raw.githubusercontent.com/<repo>/firmware/latest.json` for the
   newest release: `{ version, layout, size, sha256, file, notes }`. When `version` is
   newer than the device's and `layout` matches, the Device tab offers
   "Update to 0.5.0" with a link to the release notes.
2. On the button (and a confirmation), the page downloads the file and checks its size.
3. The page sends `POST /api/update/start`. The main firmware answers, shows
   "Restarting to update" on the screen, selects the updater and restarts.
4. The page waits until `GET /api/status` at the same address answers with
   `updater: true`.
5. The page sends the file to the updater: `POST /api/firmware`, with the expected
   SHA-256 and size in headers. The updater writes it to `ota_0` as it arrives, shows
   progress, and at the end checks the hash and the image. Only then does it select
   `ota_0`, answer, and restart.
6. The page waits for the main firmware and shows the version it now reports.

With a chosen file, steps 1 and 2 are replaced by the file; the hash is computed by
`tools/deskhog`, and left out by the page (the image's own checksum still applies).

### When it goes wrong

- **The upload breaks, the hash is wrong, or power is lost while writing:** `otadata`
  still points at the updater, so the device comes back in the updater. The page (or a
  reload of the device's address) offers to try again.
- **Nobody sends anything:** after 5 minutes without an upload the updater returns to
  the main firmware, if that is still intact (it is until the first byte is written).
- **The updater cannot join WiFi:** same 5-minute rule. A device with a broken main
  firmware and no WiFi needs the cable; that takes two failures at once.
- **The page was closed:** opening the device's address shows the updater's own small
  page: what is happening, a file field, and "Restart without updating".
- **The file is too large for the slot or for another layout:** refused before
  anything is written.

## The updater

A second firmware in this repository (`updater/`), built by its own PlatformIO
environment, with nothing from the main firmware except the names of the WiFi keys in
NVS and the device name. It:

- joins the saved WiFi under the device's name (`<name>.local`), so the page keeps
  its address;
- answers `GET /api/status` (`updater: true`, its version, whether the main firmware
  is intact), `POST /api/firmware`, `POST /api/restart`, and `GET /` with its small page;
- shows what it is doing on the screen: waiting, a percentage, done, or what failed
  and the address to go to.

## Moving a device to the new layout

Once per device, over USB, because the partition table changes and plugin storage
moves and shrinks.

- `tools/deskhog migrate --port <port>` reads the plugin storage from the device,
  unpacks it on the computer, packs it for the new size, and writes the new partition
  table, the updater, the main firmware and the storage in one go. It refuses, before
  writing anything, when the files do not fit in 768 KB, and keeps the image it read
  as a file so nothing is lost if a later step fails.
- It uses the tools PlatformIO already has (`esptool`, `mklittlefs`).
- Secrets travel inside the storage image on the computer and never over the network.
- A first install from the browser (`deskhog-full.bin`) gets the new layout directly.
- The main firmware knows which layout it runs on and reports it (`layout: 2` in
  `/api/device` status). On the old layout the page explains the one-time move
  instead of offering an update.

## Releases

`build-release.yml` builds as today and additionally:

- attaches `deskhog-<version>-updater.bin` to the release, and puts the updater into
  `deskhog-<version>-full.bin`;
- commits `deskhog-update.bin` and `latest.json` to a `firmware` branch, which is what
  the page reads.

Which repository the page asks is a build setting (`UPDATE_REPO`). Builder's call:
it defaults to the repository the build runs in, so a release from Tim's fork points
devices at Tim's fork.

## API and tools

```
GET  /api/device           status gains `firmware: "0.4.0"`, `layout: 2`
POST /api/update/start     select the updater and restart; 403 from the device itself;
                           409 on the old layout
tools/deskhog update            install the newest release
tools/deskhog update <file>     install this file
tools/deskhog migrate --port P  the one-time move, over USB
```

## Left to the builder

- The updater's web server and screen code, as long as it fits; names of files and
  classes; wording on the page and the screen.
- How `pio run -t upload` is kept working for the main firmware at its new offset,
  and how `merge_firmware.py` builds the full file.
- How the pretend device in `test/portal` plays an update, including a failed one.

## Verification

- Native and portal tests pass; the portal tests cover: an update offered, not offered
  when up to date, the old layout, a failed upload followed by a retry.
- Both firmwares build, and their sizes are noted against their slots.
- On Tim's device, with him: the migrate script (plugins, cards and settings still
  there), an update from a file, a release update from the `firmware` branch, pulling
  the upload halfway and retrying, and the 5-minute return.

## Later

- The main firmware can rewrite the updater slot, so the updater could be updated
  over WiFi too.
- Signed releases.
