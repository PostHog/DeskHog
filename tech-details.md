# DeskHog technical details

Microcontrollers are a pain. They've got limited memory and, for our purposes here, you've got to write C++ 🫠

But in exchange, our code can touch reality like no other kind of project. Here's what we're dealing with.

### Core and task isolation

If you've ever written mobile code, you'll feel right at home: we can only update the UI via the UI thread, otherwise the board crashes.

We've got two cores and multiple "tasks" assigned between them – task is [FreeRTOS](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/FreeRTOS/BasicMultiThreading)-speak for threads:

**Core 0 (Protocol CPU) tasks:**

- WiFi
- Web portal server
- Network requests for plugins
- NeoPixel control
- Battery gauge (reads the fuel gauge over I2C every 30 s and hands the reading over through an atomic)

**Core 1 (Application CPU) tasks:**

- LVGL tick (maintains timing, animations, etc for the graphics library) 
- UI: screen drawing and input handling

We have to keep this stuff carefully isolated or we're going to crash.

### Buttons

<img width="500" alt="diagram" src="https://github.com/user-attachments/assets/14ea2440-90d8-4540-bebb-045c18fbbc99" />

Buttons are read every 10 ms. ▲ and ▼ move between cards; ● belongs to the card on screen. A plugin may claim all three buttons, so two combinations never reach it: ● held with ▲ always goes to the previous card (`PluginCard::handleButtonPress`), and ● with ▼ held for two seconds powers the device off (deep sleep, in the main loop; reset wakes it).

If the board isn't responding:

- Hold **▼ (Page down/D0)**
- Press **Reset**
- Release **▼ (Page down/D0)**

The board will restart in bootloader mode, where it can be re-flashed using PlatformIO.

### Pin definitions

You don't have to guess the pin definitions. You'll find them documented here:

~/.platformio/packages/framework-arduinoespressif32/variants/adafruit_feather_esp32s3_reversetft/pins_arduino.h

## Important components

### Event queue

`EventQueue` is how the project manages communication between tasks and prevents coupling. Events – changes via the web UI, WiFi coming and going – are dispatched out of core 0 to be received by the UI task. Any important data can be safely copied from one context into the other, preventing crashes and other drama.

### Card stack

The UI is a stack of cards. The user navigates between them using built-in buttons (the arrow keys)

`CardNavigationStack` manages the UI presentation of these cards, animating transitions.

`CardController` manages updates to the stack contents. If a card is added, removed or moved via the web UI, the controller processes that update reactively.

### Web UI

The device serves one web page for everything: WiFi setup, cards, connections and plugin code. You reach it via a QR code on first launch, and by the IP shown in the status screen once WiFi is configured.

It is three hand-written files in `html/` with no dependencies, inlined into one file on each build by `htmlconvert.py`. It is laid out for a phone first; from 900 pixels wide it has a sidebar, a grid of cards and a pane at the side for the card you picked.

To work on it without flashing, run `node test/portal/serve.js` and open `http://localhost:8080`. It serves `html/` as it is on disk, against a pretend device, or against your real one with `--device <address>`. `test/portal/run.sh` runs its tests.

**Web portal budget:** the page is about 119 KB (121,371 bytes) before it is inlined. The budget was 100 KB, and the device settings took it past that; the limit that counts is the flash, where the firmware leaves room. Everything it needs must be in those files, since the page has to work when the device has no internet (that is when you set up WiFi).

### Device settings

How bright the screen and the LED are, night hours, rotation, the battery and the device's name are settings of the device, saved in NVS next to the time zone (`ConfigManager`).

- `lib/plugin_core/src/DeskRules.h` has the rules without any hardware: `DeviceSettings` and their checks, `DeskRules` (is it night, which levels now, the wake on a press, night mode by hand until the next boundary) and `Rotation` (when to move on, and every reason to wait). It has no Arduino, LVGL or FreeRTOS in it, and `test/native` covers it.
- `src/DeskManners` applies the rules. The UI task calls `tick()` every loop: it fades the backlight and hands the LED its level through atomics, rotates the cards, and gives the status card its night and battery lines. The web server (core 0) changes settings under a lock and sets a flag that `tick()` picks up; it never calls LVGL. The press that wakes a dark screen is swallowed in `buttonPressed()`.
- `src/hardware/BatteryGauge` reads the MAX17048 on the I2C bus every 30 seconds on its own task (core 0), so a slow or missing chip cannot hold up the UI. It publishes one atomic word. The gauge cannot tell that no battery is connected, which is why plugins and the status card only get the reading when the `battery` setting is on.
- `PluginApi` serves `/api/device` and `/api/restart`; a new name changes the mDNS name at once, after the answer has gone out. The router learns the new hostname at the next WiFi connection.

### Card system

Every card you can add is a Lua plugin (next section). `CardController` rebuilds the stack from the saved list whenever it changes and asks `PluginManager` for each card.

One card is part of the firmware: `ProvisioningCard`, always first, which displays a QR code to connect to the device and, once WiFi is connected, the connection stats.

Older firmware had more cards written in C++ (Max, questions, Flappy Hog, Paddle, hello world, PostHog insights), saved with a type of their own. `PluginManager::migrateOldCards()` and `migrateBuiltInPostHog()` turn such saved cards into cards of the plugin that replaced them on the first boot. A card whose plugin is not on the device yet says so on its screen, and the web page offers to install it.

### Lua plugins

Plugins are Lua files stored on the device and run as cards. The pieces:

- `lib/lua`: Lua 5.4, vendored, configured for 32-bit numbers.
- `lib/plugin_core`: the runtime (`LuaPlugin`). It sandboxes one plugin, enforces its memory and time limits, and exposes the `gfx`, `ui`, `http`, `json`, `led`, `store`, `time` and `input` APIs. It has no Arduino or LVGL dependencies, so it also builds on a desktop.
- `src/plugins/LvglGfx`: draws a plugin's output onto an LVGL canvas, which is the card.
- `src/plugins/PluginConnection`: a running connection: one Lua program plus the shared settings its cards use. It serves every card of that connection in the stack.
- `src/plugins/PluginCard`: one card in the stack: the canvas a connection paints on.
- `src/plugins/PluginManager`: plugin files and saved connections on flash (LittleFS), the network task for `http`, hot reload, and the state the web API reports.
- `src/plugins/PluginApi`: the HTTP routes for plugins, connections and cards.

The three levels are plugin (the code), connection (a plugin plus settings its cards share) and card (one screen). A card is stored in the card stack as `<connection id>:<card id>`. The card stack is rebuilt from scratch on every change; a connection keeps its state for three seconds without cards, so a rebuild does not restart running plugins.

Plugins only ever run on the UI task (core 1). The web server and the network task reach them through queues that `PluginManager::tick()` drains on the UI task, which fits the task rules above.

Plugins reach a device from a plugin directory: a folder with an `index.json` in a git repository, by default `plugins/` in this one (`PLUGIN_DIRECTORY` in `PluginManager.cpp`, changeable per device on the web page). The device only stores the directory's address. The web page and `tools/deskhog` do the fetching, because the browser has the internet connection, the memory and certificate checks that the device would have to spend scarce internal RAM on. `tools/deskhog index` writes the index and the list of plugins in the README, and `tools/screenshots` makes the picture of each plugin's card (`plugins/screenshots/`) with the simulator; `plugins2c.py` embeds the plugins named in `plugins/bundled.txt` (none by default) into the firmware for a first boot without internet.

Two ways to check plugin work without a device:

- `test/native/run.sh` builds the runtime with your computer's compiler and runs its tests, including every plugin in `plugins/`.
- `tools/sim/run.sh plugins/weather.lua` runs a plugin through the real runtime, `LvglGfx`, LVGL and fonts, and saves a PNG of the card.

The plugin API is documented in [plugins/README.md](plugins/README.md).

### Flash layout

The 4 MB flash holds two firmwares and a filesystem; see `partitions.csv`, which calls this layout 2:

| Slot | Size | Holds |
|---|---|---|
| `updater` | 1,024 KB | A small firmware that can only join WiFi and write a new main firmware (`updater/main.cpp`). 965 KB today |
| `ota_0` | 2,176 KB | The main firmware. 2,079 KB today, so about 97 KB is left to grow; the build fails when it no longer fits |
| `spiffs` | 768 KB | LittleFS, for plugins and cards |

Why an updater: the usual way to update over the air needs two slots for the firmware, so that the running one can write the new one beside itself. Two times 2.1 MB is more than the 3.9 MB there is. So the firmware is written over itself, by something that is not the firmware.

- **Which one starts.** The bootloader starts `ota_0` when the boot-selection data (`otadata`) names it, and the `updater` slot when that data is blank. `POST /api/update/start` selects the updater and restarts. The updater selects `ota_0` only after a whole file has arrived, the image checks out and the SHA-256 the sender gave matches. An update that is cut short therefore lands in the updater again.
- **Who brings the file.** The browser (or `tools/deskhog update`) downloads the release and posts it to the updater: `POST /api/firmware?size=<bytes>&sha256=<hex>`, as a form upload. The updater has no TLS, which is what keeps it small. The page is served over plain HTTP, where browsers have no hashing, so the page passes on the hash from the release and the updater does the check.
- **Where releases are found.** A browser may not download a release's files (no CORS header), so the release workflow also commits `deskhog-update.bin` and `latest.json` to the `firmware` branch, which `raw.githubusercontent.com` serves to any page. `latest.json` is `{version, layout, size, sha256, file, notes}`. The repository is a build setting, `UPDATE_REPO`, which the workflow sets to the repository it runs in; `/api/device` reports it as `status.update_repo`, with `status.firmware` and `status.layout`.
- **The updater's own API.** `GET /api/status` (`{updater: true, main_intact, writing, written, done, slot_size, error}`), `POST /api/firmware`, `POST /api/restart` (back to the firmware, when that is whole) and `GET /` for its small page. It gives up after five minutes without an upload if the firmware is whole.
- **Building.** `pio run` builds the main firmware; `updater/include_updater.py` builds the updater when it is missing or stale (`pio run -e updater`) and adds it to what **Upload** flashes and to `deskhog-full.bin`. `pio run -e updater -t upload` flashes the updater alone.
- **Changing the layout** means every device has to be moved over USB, with `tools/deskhog migrate` (and `tools/repack_storage.py`) changed to match. Raise the layout number in `partitions.csv` when you do: a release is only offered to devices with the same number.
- **Not done:** the main firmware could rewrite the updater's slot, so the updater could be updated over WiFi too. Today a new updater needs USB. Releases are not signed.

Every build leaves two files in `.pio/build/<env>/`: `firmware.bin`, the firmware alone, which is what an update over WiFi installs (over USB it goes at 0x120000), and `deskhog-full.bin`, made by `merge_firmware.py`, which also holds the bootloader, the partition table and the boot-selection data and goes at 0x0. The full file blanks the settings area on the way, because that lies between its pieces. `.github/workflows/build-release.yml` builds both when a release is created and attaches them to it.

### LVGL

This project relies on the powerful [LVGL project](https://docs.lvgl.io/9.2/intro/index.html) at [v9.2.2](https://registry.platformio.org/libraries/lvgl/lvgl?version=9.2.2) for drawing, animation and other UI tasks.

### Config manager and captive portal

`ConfigManager` handles persistent storage and retrieval of WiFi credentials, the list of cards and the address of the plugin directory. `CaptivePortal` provides the web server and interacts with `ConfigManager` to read and write to persistent storage.

### Images

Plugins hold their images as text in the Lua file (see "Images" in [plugins/README.md](plugins/README.md)). `tools/deskhog image *.png` converts PNG files into that text. The PNGs that Max's walking frames were made from are in `raw-png/walking`.
