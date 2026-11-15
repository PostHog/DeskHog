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

**Core 1 (Application CPU) tasks:**

- LVGL tick (maintains timing, animations, etc for the graphics library) 
- UI: screen drawing and input handling

We have to keep this stuff carefully isolated or we're going to crash.

### Buttons

<img width="500" alt="diagram" src="https://github.com/user-attachments/assets/14ea2440-90d8-4540-bebb-045c18fbbc99" />

If the board isn't responding:

- Hold **▼ (Page down/D0)**
- Press **Reset**
- Release **▼ (Page down/D0)**

The board will restart in bootloader mode, where it can be re-flashed using PlatformIO.

### Pin definitions

You don't have to guess the pin definitions. You'll find them documented here:

~/.platformio/packages/framework-arduinoespressif32/variants/adafruit_feather_esp32s3_reversetft/pins_arduino.h

## UI progress

- Status card: working
- WiFi provisioning card with QR Code: working
- Friend card to give you (mild) reassurance: working
- PostHog insights: now a plugin (`plugins/posthog.lua`) with big numbers, line charts and funnels; other kinds of insight are not supported yet

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

**Web portal budget:** the page is about 70 KB before it is inlined. We allow up to **100KB**. Everything it needs must be in those files, since the page has to work when the device has no internet (that is when you set up WiFi).

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

Plugins reach a device from a plugin directory: a folder with an `index.json` in a git repository, by default `plugins/` in this one (`PLUGIN_DIRECTORY` in `PluginManager.cpp`, changeable per device on the web page). The device only stores the directory's address. The web page and `tools/deskhog` do the fetching, because the browser has the internet connection, the memory and certificate checks that the device would have to spend scarce internal RAM on. `tools/deskhog index` writes the index; `plugins2c.py` embeds the plugins named in `plugins/bundled.txt` (none by default) into the firmware for a first boot without internet.

Two ways to check plugin work without a device:

- `test/native/run.sh` builds the runtime with your computer's compiler and runs its tests, including every plugin in `plugins/`.
- `tools/sim/run.sh plugins/weather.lua` runs a plugin through the real runtime, `LvglGfx`, LVGL and fonts, and saves a PNG of the card.

The plugin API is documented in [plugins/README.md](plugins/README.md).

### Flash layout

The 4 MB flash holds one firmware slot (2.9 MB) and a 1 MB LittleFS partition for plugins; see `partitions.csv`. There is no second firmware slot, so there are no over-the-air firmware updates: firmware goes over USB, plugins go over WiFi.

### LVGL

This project relies on the powerful [LVGL project](https://docs.lvgl.io/9.2/intro/index.html) at [v9.2.2](https://registry.platformio.org/libraries/lvgl/lvgl?version=9.2.2) for drawing, animation and other UI tasks.

### Config manager and captive portal

`ConfigManager` handles persistent storage and retrieval of WiFi credentials and the list of cards. `CaptivePortal` provides the web server and interacts with `ConfigManager` to read and write to persistent storage.

### Images

Plugins hold their images as text in the Lua file (see "Images" in [plugins/README.md](plugins/README.md)). `tools/deskhog image *.png` converts PNG files into that text. The PNGs that Max's walking frames were made from are in `raw-png/walking`.
