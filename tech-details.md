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

▲ and ▼ move between cards; ● belongs to the card on screen. A plugin may claim all three buttons, so two combinations never reach it: ● held with ▲ always goes to the previous card (`PluginCard::handleButtonPress`), and ● with ▼ held for two seconds powers the device off (deep sleep, in the main loop; reset wakes it).

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

**Web portal budget:** the page is about 87 KB before it is inlined. We allow up to **100KB**. Everything it needs must be in those files, since the page has to work when the device has no internet (that is when you set up WiFi).

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

The 4 MB flash holds one firmware slot (2.9 MB) and a 1 MB LittleFS partition for plugins; see `partitions.csv`. There is no second firmware slot, so there are no over-the-air firmware updates: firmware goes over USB, plugins go over WiFi.

Why not both: the usual way to update over the air needs two firmware slots, so that the running firmware can write the new one beside itself. The firmware is about 1.9 MB. Two slots for it take 3.8 MB of the 3.9 MB there is, which leaves nothing for plugins. Ways to get updates without a cable on this board, none of them built:

- A small second firmware that can only join WiFi and receive a file (well under 1 MB). To update, the device restarts into it, takes the new firmware and writes it over the main slot. That would fit as roughly 0.8 MB + 2.2 MB + 0.85 MB for plugins, and a failed update still leaves the small one to try again with.
- Flashing over USB from a web page (Web Serial) is what the README's install steps use today, with a general-purpose flasher and a file from the release. A page of our own built on [ESP Web Tools](https://esphome.github.io/esp-web-tools/) could make that one button, and keep the settings when updating.

Every build leaves two files in `.pio/build/<env>/`: `firmware.bin`, the firmware alone, which goes at 0x20000, and `deskhog-full.bin`, made by `merge_firmware.py`, which also holds the bootloader, the partition table and the boot-selection data and goes at 0x0. The full file blanks the settings area on the way, because that lies between its pieces. `.github/workflows/build-release.yml` builds both when a release is created and attaches them to it.

### LVGL

This project relies on the powerful [LVGL project](https://docs.lvgl.io/9.2/intro/index.html) at [v9.2.2](https://registry.platformio.org/libraries/lvgl/lvgl?version=9.2.2) for drawing, animation and other UI tasks.

### Config manager and captive portal

`ConfigManager` handles persistent storage and retrieval of WiFi credentials, the list of cards and the address of the plugin directory. `CaptivePortal` provides the web server and interacts with `ConfigManager` to read and write to persistent storage.

### Images

Plugins hold their images as text in the Lua file (see "Images" in [plugins/README.md](plugins/README.md)). `tools/deskhog image *.png` converts PNG files into that text. The PNGs that Max's walking frames were made from are in `raw-png/walking`.
