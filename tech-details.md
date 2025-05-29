# DeskHog: a display for your data, a friend for building your product

This is a super-lean file we've prepared for you to feed your vibe-coding platform so it has more context on what DeskHog is. 

While there's nothing wrong with a little agent-driven coding, you should still beware: LLM agents are very bad at modeling cross-thread interactions and thread-safe architectures on their own. You'll need to lead them explicitly. The existing architecture seems pretty stable and predictable at this point. Lean on it. If your robot ventures off the trail and takes *initiative* that breaks these patterns, you'll end up with crashes and your pull requests will not be accepted.

## Hardware

DeskHog includes the following hardware.

- ESP32-S3 Reverse TFT Feather
- Optional: PKCell 552035 350mAh 3.7V LiPoly battery 
- 3D printed enclosure – print: [3mf file](3d-printing)

## Requirements

Use [PlatformIO](https://platformio.org) to open this project. It works with VSCode and Cursor, streamlining package management, builds and flashing microcontroller boards.

## How it works

This micro-controller is programmed in C++. 

Here's what we're dealing with.

### Core and task isolation

It's like writing code for mobile devices: we can only update the UI via the UI thread, otherwise the board crashes.

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

### Buttons

<img width="500" alt="diagram" src="https://github.com/user-attachments/assets/14ea2440-90d8-4540-bebb-045c18fbbc99" />

DeskHog has four buttons:

- A reset button for turning on the device
- A up (Page up/D2/GPIO2) and down (Page down/D0/GPIO0) button for navigating the interface
- A center button (Action/D1/GPIO1) for interacting with cards

If the board isn't responding, it can be restarted in bootloader mode and reflasahed using Platform.io. 

- Hold **▼ (Page down/D0)**
- Press **Reset**
- Release **▼ (Page down/D0)**

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