# DeskHog: A developer toy for sparking developer joy

<img width="992" alt="Screenshot 2025-04-18 at 1 31 29 AM" src="https://github.com/user-attachments/assets/6e5d1fe6-1887-4d66-8497-4b582eb0391a" />

## What is DeskHog?
DeskHog is an open-source, 3D printed, palm-sized developer toy. 

DeskHog ships with a library of simple example game and apps, including a pomodoro timer and versions of Pong, Rogue, Flappy Bird, but it isn't a micro-console or mobile device. It's a toy to tinker with, adapt, and to get curious with.

We think that sounds like fun and, if you do too, then DeskHog is for you. 

## OK, but what is it?

DeskHog packs a ESP32-S3 Reverse TFT Feather in a custom-made 3D printed case. It comes complete with a 240x135 color TFT display, a 10-hour battery life, WiFi, and a cute little LED do you can find it in the dark.

A [plug-and-play DeskHog hardware kit](https://posthog.com/merch) is coming soon (so you can get everything in one box). In the meantime, here’s what you’ll need if you want to make DeskHog yourself right now. 

- ESP32-S3 Reverse TFT Feather – [`Adafruit 5691`](https://learn.adafruit.com/esp32-s3-reverse-tft-feather) – buy: [Adafruit](https://www.adafruit.com/product/5691), [DigiKey](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5691/18627502?s=N4IgjCBcoLQBxVAYygMwIYBsDOBTANCAPZQDa4ArAEwIC6AvvYVWSBQGwCcEDQA), [Mouser](https://www.mouser.com/ProductDetail/Adafruit/5691?qs=mELouGlnn3eeALy2e3r3sw%3D%3D), [Botland (EU)](https://botland.store/arduino-compatible-boards-adafruit/22891-feather-esp32-s3-reverse-with-tft-display-wifi-module-4mb-flash-2mb-psram-compatible-with-arduino-adafruit-5691.html), [Cool Components (UK)](https://coolcomponents.co.uk/products/esp32-s2-reverse-tft-feather?_pos=1&_psq=reverse+tft+fea&_ss=e&_v=1.0), [Electromaker](https://www.electromaker.io/shop/product/adafruit-esp32-s3-reverse-tft-feather-4mb-flash-2mb-psram-stemma-qt?srsltid=AfmBOorFfshQCLVi9EDKfmKFKMNC_cE3Ww0NaY0U0evm5ZU2OEM2Yn_B), [BerryBase (EU)](https://www.berrybase.de/en/adafruit-esp32-s3-reverse-tft-feather)
- Optional: PKCell 552035 350mAh 3.7V LiPoly battery – buy: [Adafruit](https://www.adafruit.com/product/2750), [Tinytronics (EU)](https://www.tinytronics.nl/en/power/batteries/li-po/pkcell-li-po-battery-3.7v-350mah-jst-ph-lp552035), [BerryBase (EU)](https://www.berrybase.de/en/lp-552035-lithium-polymer-lipo-akku-3-7v-350mah-mit-2-pin-jst-stecker)
- 3D printed enclosure – print: [3mf file](3d-printing)

## What can DeskHog do?

DeskHog can do a couple of things by default, but the real joy lies in expanding the capabilities by adding them yourself. 

### Out of the box functionality

A couple of basic [cards](#card-stack) (screens) are included on the firmware and were built by [Danilo Campos](https://posthog.com/community/profiles/31731).

- **ProvisioningCard**: Displays a QR code to connect to the device and shows connection stats.
- **InsightCard**: Visualizes PostHog data. Still a bit of a WIP, so contributions welcome!
- **FriendCard**: Lets Max the hedgehog visit with you and provide encouragement.

### Available games and apps

We're working on adding some of the following games to an `/examples` folder which will include a selection of games and apps for you to play with. If you can't see them in the `/examples` folder it's probably because they exist as [unapproved PRs](https://github.com/PostHog/DeskHog/pulls) while we work on them. No problem, just checkout the branch!

Example games include:

- **Pog**: A Pong clone by [Leon Daly](https://posthog.com/community/profiles/30833)
- **IdleHog**: An idle clicker by [Chris McNeill](https://posthog.com/community/profiles/33534)
- **One Button Dungeon**: An idle Roguelike by [Joe Martin](https://posthog.com/community/profiles/29070)
- **Dictator or Techbro: DeskHog Edition**: A quiz game by [Chris McNeill](https://posthog.com/community/profiles/33534)
- **Notchagotchi**: A virtual pet by [Sophie Payne](https://posthog.com/community/profiles/33385)
- **Flappy Hog**: A Flappy Bird clone by [Joe Martin](https://posthog.com/community/profiles/29070)
- **Hog Speed**: A reaction game by [Chris McNeill](https://posthog.com/community/profiles/33534)
- **Pineapple Reflex**: A pizza-based game by [Sophie Payne](https://posthog.com/community/profiles/33385)
- **Three Button Dungeon**: An Ultima-like by [Joe Martin](https://posthog.com/community/profiles/29070)

Example apps include:

- **Awkwardness Avoider**: A smalltalk prompter by [Annika Schmid](https://posthog.com/community/profiles/28619)
- **Pomodoro**: A productivity timer by [Annika Schmid](https://posthog.com/community/profiles/28619)

All of the example apps were built in 24 hours at [a PostHog hackathon](https://posthog.com/handbook/company/offsites) by people who hadn't coded before at all. These example cards are a testament to [what's possible when vibe coding on DeskHog](#-vibe-coding-with-ai-agents), but some lack polish - so make sure you know [how to reset DeskHog](#buttons-and-reset-sequences). Just in case.

## Developing for DeskHog

Microcontrollers are a pain. They've got limited memory and, for our purposes here, you've got to write C++ - but the good news is that platforms like Cursor make that a breeze nowadays. Check out our [vibe-coding guide for tips](#-vibe-coding-with-ai-agents).

We recommend using [PlatformIO](https://platformio.org/) to ship changes from your computer to DeskHog. It’s an all-in-one tool (works great with VS Code or Cursor) that handles the tricky bits of library management, firmware, and flashing the microcontroller. PlatformIO streamlines the workflow so you don’t have to wrangle compiler settings or board drivers manually. It's even suitable for vibe coders!

If you want to get into the details, read on. If you plan on just vibe coding some ideas into reality, [skip ahead](#-vibe-coding-with-ai-agents). 

### Buttons and reset sequences

DeskHog has three buttons on the right, and a reset button on the left. Turn it on with the Reset button, navigate the UI with the arrows, select and interact with the center button. 

If DeskHog ever stops working, you can use a secret button combo to restart it in bootloader mode, ready for new firmware:

- Hold the ▼ button (labeled D0).
- While holding ▼, press Reset.
- Continue holding ▼, then release Reset.
- Finally, release ▼.

The board will restart in bootloader mode, ready for new firmware. Upload via PlatformIO (or another tool) without regular firmware auto-running.

### Event queue

`EventQueue` is how the project manages communication between tasks and prevents coupling. Events – changes via the web UI, returned requests from the PostHog client – are dispatched out of core 0 to be received by the UI task. Any important data can be safely copied from one context into the other, preventing crashes and other drama.

### Card stack

The UI is a stack of cards (screens). You navigate between them using built-in buttons (the arrow keys)

- `CardNavigationStack` manages the UI presentation of these cards, animating transitions.
- `CardController` manages updates to the stack contents. If an insight is deleted or added via web UI, the controller processes that update reactively.

### Web UI

DeskHog hosts a captive web portal. On first launch, scan the QR code (or visit the device’s IP on the status screen) to configure Wi-Fi and select insights.

- Preview changes by opening `html/portal.html` in your browser.
- We inline the portal into firmware via `htmlconvert.py`.
- Portal size: ~18 KB (budget: up to 100 KB); all assets must be local.

If you want to try adding a more complex UI framework than hand-rolled JS and HTML, you're welcome to try! The build system must be quick and the final static output has to be under 100KB.

### Insight parser and PostHog client

- `InsightParser` ingests PostHog API responses and makes them available to the UI. 
- `PostHogClient` constructs requests and dispatches responses.

### LVGL

This project relies on the powerful [LVGL project](https://docs.lvgl.io/9.2/intro/index.html) at [v9.2.2](https://registry.platformio.org/libraries/lvgl/lvgl?version=9.2.2) for drawing, animation and other UI tasks.

### Config manager and captive portal

- `ConfigManager` handles persistent storage and retrieval of credentials and insights.
- `CaptivePortal` provides the web server and interacts with `ConfigManager` to read and write to persistent storage.

### Core and task isolation

If you've ever written mobile code, you'll feel right at home: you can only update the UI via the UI thread, otherwise the board crashes. There are two cores and multiple "tasks" assigned between them – 'task' is [FreeRTOS](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/FreeRTOS/BasicMultiThreading)-speak for 'threads':

**Core 0 (Protocol CPU) tasks:**

- WiFi
- Web portal server
- Insight parsing
- NeoPixel control

**Core 1 (Application CPU) tasks:**

- LVGL tick (maintains timing, animations, etc for the graphics library) 
- UI: screen drawing and input handling

Keep this stuff carefully isolated or you're going to crash.

## 🤖 Vibe Coding with AI Agents
DeskHog was built with AI coding assistants — and we encourage agent-driven development with a few caveats. LLMs often struggle with multi-threaded embedded systems. DeskHog’s firmware has strict rules for core/task roles; AI might overlook them unless you guide it.

If you're looking to get started, we recommend using [Cursor](https://www.cursor.com/en) with the [PlatformIO extenstion](https://platformio.org/install/ide?install=vscode). They work well together and you can use Cursor's chat function to start building all sorts of things with natural language.

We've prepared a `tech-details.md` file in this repo which we optimized to give your agent broad context on the device. Still, even with that you should bear in mind the following advice:

- You should encourage AI to follow existing patterns: use`EventQueue` for cross-core messaging and update the UI only on the UI task.
- Flag any AI suggestions that touch the UI from the wrong core.
- Review and test thoroughly—watch for “LLM slop” (unused vars, odd delays) and clean it up.

Think of your AI as a speedy junior dev: brilliant but prone to mistakes. You’re the senior dev keeping it on track. With that balance, AI can boost productivity while keeping DeskHog reliable.

## UI progress
- Status card: working
- WiFi provisioning card with QR Code: working
- Friend card to give you (mild) reassurance: working
- Numeric card for Big Number insights: working
- Funnel card: needs a redesign; probably should be horizontal layout instead, won't display more than three steps right now
- Line graph card: broken, not properly scaling larger data sets, probably fine if you have an insight scoped between 7-30 days
- Other insights: not yet supported

## Request for PRs

The following PRs would be interesting, and may earn you a free DeskHog kit or other merch:

- Additional insight parsing and visualizations for Insight Card
- More games and apps! We think limiting things to one available button prompts creativity
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
