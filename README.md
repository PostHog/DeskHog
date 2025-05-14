# DeskHog: a display for your data, a friend for building your product

Keep an eye on your data at all times with this desktop terminal for PostHog.

<img width="992" alt="Screenshot 2025-04-18 at 1 31 29 AM" src="https://github.com/user-attachments/assets/6e5d1fe6-1887-4d66-8497-4b582eb0391a" />

## Hardware

- **Coming soon:** Order a kit with all hardware
- ESP32-S3 Reverse TFT Feather – [`Adafruit 5691`](https://learn.adafruit.com/esp32-s3-reverse-tft-feather) – buy: [Adafruit](https://www.adafruit.com/product/5691), [DigiKey](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5691/18627502?s=N4IgjCBcoLQBxVAYygMwIYBsDOBTANCAPZQDa4ArAEwIC6AvvYVWSBQGwCcEDQA), [Mouser](https://www.mouser.com/ProductDetail/Adafruit/5691?qs=mELouGlnn3eeALy2e3r3sw%3D%3D), [Botland (EU)](https://botland.store/arduino-compatible-boards-adafruit/22891-feather-esp32-s3-reverse-with-tft-display-wifi-module-4mb-flash-2mb-psram-compatible-with-arduino-adafruit-5691.html), [Cool Components (UK)](https://coolcomponents.co.uk/products/esp32-s2-reverse-tft-feather?_pos=1&_psq=reverse+tft+fea&_ss=e&_v=1.0), [Electromaker](https://www.electromaker.io/shop/product/adafruit-esp32-s3-reverse-tft-feather-4mb-flash-2mb-psram-stemma-qt?srsltid=AfmBOorFfshQCLVi9EDKfmKFKMNC_cE3Ww0NaY0U0evm5ZU2OEM2Yn_B), [BerryBase (EU)](https://www.berrybase.de/en/adafruit-esp32-s3-reverse-tft-feather)
- Optional: PKCell 552035 350mAh 3.7V LiPoly battery – buy: [Adafruit](https://www.adafruit.com/product/2750), [Tinytronics (EU)](https://www.tinytronics.nl/en/power/batteries/li-po/pkcell-li-po-battery-3.7v-350mah-jst-ph-lp552035), [BerryBase (EU)](https://www.berrybase.de/en/lp-552035-lithium-polymer-lipo-akku-3-7v-350mah-mit-2-pin-jst-stecker)
- 3D printed enclosure – print: [3mf file](3d-printing)

## Requirements

Use [PlatformIO](https://platformio.org) to open this project. It works with VSCode and Cursor, streamlining package management, builds and flashing microcontroller boards.

## How it works

Microcontrollers are a pain. They've got limited memory and, for our purposes here, you've got to write C++ 🫠

But in exchange, our code can touch reality like no other kind of project. Here's what we're dealing with.

### Core and task isolation

If you've ever written mobile code, you'll feel right at home: we can only update the UI via the UI thread, otherwise the board crashes.

We've got two cores and multiple "tasks" assigned between them – task is [FreeRTOS](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/FreeRTOS/BasicMultiThreading)-speak for threads:

**Core 0 (Protocol CPU) tasks:**

- WiFi
- Web portal server
- Insight parsing
- NeoPixel control

**Core 1 (Application CPU) tasks:**

- LVGL tick (maintains timing, animations, etc for the graphics library) 
- UI: screen drawing and input handling

We have to keep this stuff carefully isolated or we're going to crash.

#### ⚠️ Vibe coding advisory

Nothing wrong with a little agent-driven coding. This project has leaned on it plenty.

But beware: the LLM agents are very bad at modeling cross-thread interactions and thread-safe architectures on their own. You'll need to lead them explicitly. The existing architecture seems pretty stable and predictable at this point. Lean on it. If your robot ventures off the trail and takes *initiative* that breaks these patterns, you'll end up with crashes and your pulls will not be accepted.

### Buttons

<img width="500" alt="diagram" src="https://github.com/user-attachments/assets/14ea2440-90d8-4540-bebb-045c18fbbc99" />

If the board isn't responding:

- Hold **▼ (Page down/D0)**
- Press **Reset**
- Release **▼ (Page down/D0)**

The board will restart in bootloader mode, where it can be re-flashed using PlatformIO.

## Power modes

If you want to turn the device off, simply press and hold the Page up and Page down buttons for 2 seconds. The device will enter deep sleep mode. 

To wake the device, simply press Reset once. 

## UI progress

- Status card: working
- WiFi provisioning card with QR Code: working
- Friend card to give you (mild) reassurance: working
- Numeric card for Big Number insights: working
- Funnel card: needs a redesign; probably should be horizontal layout instead, won't display more than three steps right now
- Line graph card: broken, not properly scaling larger data sets, probably fine if you have an insight scoped between 7-30 days
- Other insights: not yet supported

## Important components

### Event queue

`EventQueue` is how the project manages communication between tasks and prevents coupling. Events – changes via the web UI, returned requests from the PostHog client – are dispatched out of core 0 to be received by the UI task. Any important data can be safely copied from one context into the other, preventing crashes and other drama.

### Card stack

The UI is a stack of cards. The user navigates between them using built-in buttons (the arrow keys)

`CardNavigationStack` manages the UI presentation of these cards, animating transitions.

`CardController` manages updates to the stack contents. If an insight is deleted or added via web UI, the controller processes that update reactively.

### Web UI

A basic provisioning and configuration UI is provided. You can access it via a QR code on first launch, and by the IP shown in the status screen once WiFi is configured.

Open `html/portal.html` in your browser to preview changes. The contents of `html` are inlined into a single file on each build by `htmlconvert.py`.

**Web portal budget:** Right now the portal costs about 18KB. We'll allow up to **100KB**. All portal assets must be locally available, since the portal needs to work when the device doesn't have WiFi. If you want to try adding a more complex UI framework than hand-rolled JS and HTML, you're welcome to try as long as its build system is quick and the final static output is under 100KB.

### Cards

`ProvisioningCard` displays a QR code to connect to the device. If WiFi is connected, it displays connection stats.

`InsightCard` visualizes PostHog data. Numeric card is working best. The rest need help.

`FriendCard` lets Max the hedgehog visit with you and provide encouragement.

### Insight parser and PostHog client

`InsightParser` ingests PostHog API responses and makes them available to the UI. `PostHogClient` constructs requests and dispatches responses.

### LVGL

This project relies on the powerful [LVGL project](https://docs.lvgl.io/9.2/intro/index.html) at [v9.2.2](https://registry.platformio.org/libraries/lvgl/lvgl?version=9.2.2) for drawing, animation and other UI tasks.

### Config manager and captive portal

`ConfigManager` handles persistent storage and retrieval of credentials and insights. `CaptivePortal` provides the web server and interacts with `ConfigManager` to read and write to persistent storage.


## Request for PRs

The following PRs would be interesting, and may earn you a free DeskHog kit:

- Additional insight parsing and visualization
- Flappy hog or other silly game things to do; constraint: you can only use the center button as input
- Support for other boards and displays
- Enhance the web UI and `ConfigManger` to allow re-ordering of insights, set custom titles per-insight
- LLM slop mitigation: if you see anything obviously stupid in this code that hasn't yet been caught and cleaned up
- DX improvements around task isolation: if your experience with embedded code says there's a better way to architect this, happy to follow your lead
- Improved C++: not my preferred language, feel free to suggest idiomatic and architectural improvements
- Desk utilities, like a pomodoro timer; also constrained: center button only. Be creative!
- PlatformIO config improvements: flashing builds causes a reboot that seems unnecessary, maybe you know a better configuration
- OTA update mechanisms

## Feedback

PRs over issues, but if you've got any trouble, feel free to open an issue. You can also contact the maintainer, danilo@posthog.com.
