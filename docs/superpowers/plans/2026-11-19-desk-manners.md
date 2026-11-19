# Desk Manners Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the device its own settings: screen and LED brightness by day and night, night hours, auto-rotate, battery reading, device name, restart, and faster buttons.

**Architecture:** The rules (is it night, which brightness, rotate now) are plain C++ in `lib/plugin_core`, with no Arduino or LVGL, tested by `test/native/run.sh`. A new firmware class `DeskManners` owns the settings, saves them through `ConfigManager`, runs the rules on the UI task and applies their answers to the display, the LED and the card stack. The web API, the web page, the pretend device and `tools/deskhog` all speak one JSON shape for the settings.

**Tech Stack:** C++17 (Arduino on ESP32-S3, PlatformIO, LVGL 9.2, FastLED, ArduinoJson 6, ESPAsyncWebServer), Lua 5.4 runtime in `lib/plugin_core`, plain JS web page tested with jsdom under node, Python 3 for `tools/deskhog`.

**Spec:** `docs/superpowers/specs/2026-11-19-desk-manners-design.md`. Read it before any task; this plan does not repeat its behaviour rules.

## Global Constraints

- Work on a branch `desk-manners` made from `main`; never commit to `main`.
- LVGL and plugins are touched only from the UI task (`lvglHandlerTask`, core 1). The web server (core 0) never calls LVGL: it changes settings under a lock and the UI task picks them up. The LED task takes values through atomics.
- Nothing in `lib/plugin_core` may include Arduino, LVGL or FreeRTOS headers.
- Every existing plugin keeps working unchanged: `test/native/run.sh` must pass after every task.
- After the update a device behaves as before until someone changes a setting. Defaults: name `deskhog`, screen 80 / 10, LED 100 / 0, night hours off 22:00–07:00, rotate 0, battery false.
- `screen.day` is 5–100; all other levels 0–100; `rotate.seconds` is 0 or 10–600; name is 1–24 of `a-z 0-9 -`, not starting or ending with `-`.
- All timers compare `uint32_t` differences (`now - then < span`), so they survive `millis()` wrapping.
- Web page addition target: 5 KB of source. No new dependencies anywhere.
- Use PlatformIO as `~/.platformio/penv/bin/pio` (the Homebrew `pio` on this machine is broken).
- Comments and docs in the project's voice: plain sentences, what and why, as in the surrounding files.
- No version bump and no release notes: Tim decides when this ships.

## Review Focus

1. **The clock or the zone changes under it** (NTP sets the clock at boot in the middle of the night; someone changes the time zone at night): the levels follow at the next tick, and a manual override does not get stuck. Test in Task 1.
2. **Bad values in storage** (out-of-range numbers, an invalid name, from a bug or another firmware): loading clamps each to its default; the screen is never below 5% by day. Test in Task 1, used in Task 4.
3. **The card stack changes while rotating** (cards removed, added, reordered; zero or one rotating card; the on-screen index past the end): no rotation target out of range, no crash. Test in Task 2.
4. **A bad `POST /api/device`** (wrong types, unknown keys, half-valid body, empty body): 400 with a reason and nothing changed, not even the valid half. Test in Task 1 (the atomic apply) and Task 8 (the page shows the reason).
5. **No gauge, or a gauge that stops answering**: the battery task never blocks more than 50 ms per read, never touches the UI, and reports "no gauge". Manual check in Task 6; `device.battery()` returning `nil` is tested in Task 3.

## File Structure

| File | Responsibility |
|---|---|
| `lib/plugin_core/src/DeskRules.h/.cpp` (new) | `DeviceSettings`, validation and parsing, `DeskRules` (night, levels, wake, manual override), `Rotation` |
| `lib/plugin_core/src/Attention.h` | Two read-only accessors the rotation needs |
| `lib/plugin_core/src/PluginHost.h`, `LuaPlugin.cpp` | `BatteryStatus`, two new host calls, the `device` Lua table |
| `test/native/test_plugin_core.cpp`, `MockHost.h` | Tests for all of the above |
| `tools/sim/sim.cpp` | `--battery`, `--name` |
| `src/DeskManners.h/.cpp` (new) | Owns settings and rules in the firmware; applies them |
| `src/hardware/BatteryGauge.h/.cpp` (new) | Reads the MAX17048 on its own task |
| `src/hardware/DisplayInterface.*`, `NeoPixelController.*` | A brightness level each |
| `src/ConfigManager.*` | Save and load the settings |
| `src/main.cpp` | Create and tick `DeskManners`, 10 ms buttons, swallow the wake press, dark before power-off |
| `src/ui/ProvisioningCard.*`, `CardController.cpp`, `CardNavigationStack.*` | ● on the status card, the night and battery lines, "moved by hand" |
| `src/plugins/PluginManager.*`, `PluginApi.*`, `PluginConnection.*` | Per-card `rotate`, `/api/device`, `/api/restart`, mDNS name, host calls |
| `html/portal.html/.js/.css`, `test/portal/*` | Device tab, card tick box, tests |
| `tools/deskhog` | `device`, `restart`, `config … rotate=no` |
| `plugins/README.md`, `README.md`, `tech-details.md` | Docs |

---

### Task 1: Settings and night rules in `plugin_core`

**Files:**
- Create: `lib/plugin_core/src/DeskRules.h`, `lib/plugin_core/src/DeskRules.cpp`
- Test: `test/native/test_plugin_core.cpp` (new section `// ---- desk rules`, after the attention tests)

**Interfaces:**
- Consumes: `deskhog::TimeZone` from `TimeZone.h` for local time.
- Produces (namespace `deskhog`):

```cpp
struct DeviceSettings {
    std::string name = "deskhog";
    int screenDay = 80, screenNight = 10;
    int ledDay = 100, ledNight = 0;
    bool nightEnabled = false;
    int nightFrom = 22 * 60, nightUntil = 7 * 60;  // minutes after midnight
    int rotateSeconds = 0;
    bool battery = false;
};
bool isValidDeviceName(const std::string& name);
/** "22:30" or "1350" to minutes; false if neither. */
bool parseClock(const std::string& text, int& minutes);
std::string formatClock(int minutes);  // "07:00"
/** Keys: name, screen.day, screen.night, led.day, led.night, night.enabled, night.from,
 *  night.until, rotate.seconds, battery. Booleans are "true"/"false"/"yes"/"no"/"1"/"0".
 *  All or nothing: on false, `settings` is untouched and `error` names the key and the rule. */
bool applyDeviceSettings(DeviceSettings& settings,
                         const std::vector<std::pair<std::string, std::string>>& changes, std::string& error);
/** Whatever was stored: every field out of range goes back to its default. */
DeviceSettings sanitized(const DeviceSettings& stored);

class DeskRules {
public:
    static const uint32_t kWakeMs = 30000;     // screen stays at the day level after a press at night
    static const uint32_t kPreviewMs = 3000;   // a slider on the web page shows its level this long
    struct Levels { int screen; int led; bool night; bool manual; };
    void setSettings(const DeviceSettings& settings);
    /** @param epochSeconds 0 while the clock is not set: then it is never night by schedule. */
    Levels tick(int64_t epochSeconds, const std::string& posixZone, uint32_t nowMs);
    /** A button went down. True if the press only woke a dark screen and must go no further. */
    bool press(uint32_t nowMs);
    /** Something is worth seeing (a notify banner): day level for this long. */
    void wake(uint32_t nowMs, uint32_t forMs);
    /** ● on the status card. */
    void toggleNight();
    /** The web page is dragging a slider. */
    void preview(bool night, uint32_t nowMs);
};
```

`Levels.screen` and `.led` are percentages. A wake raises only the screen to `screenDay`; the LED keeps its night level.

- [ ] **Step 1: Write the failing tests**, one `TEST` each, names and key assertions:
  - `device_settings_defaults_change_nothing`: default `DeviceSettings` gives `tick(...)` `{80, 100, night=false}` at any time.
  - `device_settings_are_all_or_nothing`: `{{"screen.day","60"},{"led.night","300"}}` returns false, error contains `led.night`, `screenDay` still 80. `{{"screen.day","4"}}` false; `"5"` true. `{{"rotate.seconds","5"}}` false; `"0"`, `"10"`, `"600"` true; `"601"` false. `{{"bogus","1"}}` false.
  - `device_names`: `"kitchen"`, `"desk-2"`, `"a"` valid; `""`, `"-a"`, `"a-"`, `"Desk"`, `"a_b"`, 25 characters invalid.
  - `clock_text`: `parseClock("22:30")` 1350, `"7:05"` 425, `"1350"` 1350; `"24:00"`, `"12:60"`, `"x"` false; `formatClock(425)` is `"07:05"`.
  - `stored_settings_are_sanitized`: `screenDay = 0`, `ledNight = 999`, `name = "Bad Name"`, `nightFrom = 5000`, `rotateSeconds = 3` come back as 80, 0, `"deskhog"`, 1320, 0.
  - `night_crosses_midnight`: zone `"UTC0"`, enabled 22:00–07:00: 21:59 day, 22:00 night, 03:00 night, 06:59 night, 07:00 day. With from == until: never night.
  - `night_follows_the_time_zone_and_summer_time`: zone `"CET-1CEST,M3.5.0,M10.5.0/3"`, 22:00–07:00: epoch for 2026-07-01 20:30 UTC is night (22:30 local); 2026-01-01 20:30 UTC is day (21:30 local).
  - `no_night_until_the_clock_is_set`: enabled, `epochSeconds = 0`: day levels.
  - `a_press_at_night_wakes_the_screen`: night level 10: `press()` returns false, screen is 80 for the next 29.9 s, 10 after 30 s; LED stays at `ledNight` throughout. Night level 0: first `press()` returns true, a second within the wake returns false.
  - `press_by_day_is_just_a_press`: returns false, levels unchanged.
  - `night_by_hand_holds_until_the_schedule_changes`: by day at 20:00, `toggleNight()` gives night with `manual=true`; still night at 21:59; at 22:00 scheduled night takes over (`manual=false`); at 07:00 it is day. Toggle during scheduled night gives day until 07:00, then stays day, and is night again at 22:00.
  - `night_by_hand_without_night_hours`: disabled schedule: toggle gives night until toggled again.
  - `clock_set_or_zone_changed_mid_override` (Review Focus 1): toggle to night by day; then tick with a zone in which it is scheduled night, then one where it is day again: ends in day, `manual=false`.
  - `preview_shows_a_level_briefly`: by day `preview(true, t)` gives night levels until `t + 3000`, then day.
  - `wake_timer_survives_millis_wrapping`: press at `nowMs = 0xFFFFFFF0`, tick at `0x00000010` still woken.

- [ ] **Step 2: Run** `test/native/run.sh device` and `test/native/run.sh night`. Expected: does not compile (`DeskRules.h` not found).

- [ ] **Step 3: Implement** `DeskRules.h/.cpp`. The manual override is two values: the mode chosen by hand, and what the schedule said at that moment; it ends on the first tick where the schedule says something else.

- [ ] **Step 4: Run** `test/native/run.sh`. Expected: everything passes, including the plugin tests that existed.

- [ ] **Step 5: Commit** (also adds the spec and this plan):

```bash
git checkout -b desk-manners
git add docs/superpowers lib/plugin_core/src/DeskRules.* test/native/test_plugin_core.cpp
git commit -m "Device settings and night rules, with tests"
```

---

### Task 2: Rotation rules

**Files:**
- Modify: `lib/plugin_core/src/DeskRules.h/.cpp`, `lib/plugin_core/src/Attention.h`
- Test: `test/native/test_plugin_core.cpp`

**Interfaces:**
- Produces:

```cpp
// Attention.h, made public / added:
bool playing(uint32_t nowMs) const;   // was private
bool bannerShowing() const;           // bannerOn_

// DeskRules.h
class Rotation {
public:
    static const uint32_t kHandPauseMs = 60000;  // after someone changed cards themselves
    struct Input {
        std::vector<bool> rotates;  // one per card in stack order; false for the status card and skipped cards
        int onScreen = 0;           // index into `rotates`
        bool playing = false;       // Attention::playing
        bool banner = false;        // Attention::bannerShowing
        bool screenOff = false;     // Levels.screen == 0
    };
    void setSeconds(int seconds);   // 0 switches it off
    /** Someone changed cards with a button or from the web page. */
    void movedByHand(uint32_t nowMs);
    /** @return the index to show now, or -1. */
    int tick(const Input& input, uint32_t nowMs);
};
```

The dwell clock restarts whenever `onScreen` differs from the last tick, whatever caused it. While `playing`, `banner` or `screenOff` is true nothing rotates; they do not restart the clock.

- [ ] **Step 1: Write the failing tests:**
  - `rotation_is_off_by_default`: never returns anything but -1.
  - `rotation_moves_on_and_wraps`: 30 s, rotates `{false,true,true,true}`, on 1: -1 at 29.9 s, 2 at 30 s; from 3 it returns 1 (wraps past the status card).
  - `rotation_skips_cards`: `{false,true,false,true}` on 1 goes to 3; on 3 goes to 1.
  - `rotation_never_leaves_a_skipped_card`: on 0 (status) or on a `false` card: -1 forever.
  - `rotation_needs_two_cards`: `{false,true}` and `{false,true,false}`: -1.
  - `rotation_waits_while_playing_a_banner_or_dark`: each flag alone holds it at 31 s; clearing the flag lets it go on the next tick.
  - `rotation_pauses_after_a_move_by_hand`: 10 s interval, `movedByHand` at t=0 with the card changed: -1 until 60 s, then moves.
  - `a_card_that_came_to_the_front_gets_a_full_interval`: 30 s; at 25 s `onScreen` changes without `movedByHand`: next move at 55 s.
  - `rotation_survives_a_changing_stack` (Review Focus 3): `onScreen` past the end, empty `rotates`, a list that shrinks between ticks: always -1 or a valid index whose entry is true.
  - `rotation_timer_survives_millis_wrapping`.

- [ ] **Step 2: Run** `test/native/run.sh rotation`. Expected: does not compile.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** `test/native/run.sh`. Expected: all pass.
- [ ] **Step 5: Commit** `git commit -am "Rotation rules, with tests"`.

---

### Task 3: `device.battery()` and `device.name()` for plugins

**Files:**
- Modify: `lib/plugin_core/src/PluginHost.h`, `lib/plugin_core/src/LuaPlugin.cpp`, `test/native/MockHost.h`, `tools/sim/sim.cpp`, `src/plugins/PluginConnection.h/.cpp`, `src/plugins/PluginManager.cpp` (the inspector's stub services near line 618), `plugins/README.md`
- Test: `test/native/test_plugin_core.cpp`

**Interfaces:**
- Produces:

```cpp
// PluginHost.h
struct BatteryStatus { int percent = 0; float volts = 0; bool charging = false; };
// PluginServices, both pure virtual:
virtual bool battery(BatteryStatus& status) = 0;  // false: no battery
virtual std::string deviceName() = 0;
```

Lua: `device.battery()` returns `nil` or `{ percent = 82, volts = 3.98, charging = false }`; `device.name()` returns text. Registered next to `net` in `LuaPlugin.cpp`.
In this task `PluginConnection` and the inspector stub answer "no battery" and `"deskhog"`; Task 7 connects the real values.

- [ ] **Step 1: Write the failing tests:** `device_battery_is_nil_without_one`; `device_battery_reports_the_reading` (mock set to 82, 3.98, true: all three fields); `device_name_is_the_devices_name` (mock `"kitchen"`). `MockHost` gets public fields `hasBattery`, `batteryStatus`, `name`.
- [ ] **Step 2: Run** `test/native/run.sh device_`. Expected: does not compile.
- [ ] **Step 3: Implement** the two host calls, the Lua table, and the four implementers (MockHost, sim, PluginConnection, inspector stub). Sim options: `--battery 82` and `--battery 82+` (charging; volts shown as 3.7 + percent/200), `--name kitchen`; both in its `--help` text.
- [ ] **Step 4: Run** `test/native/run.sh`, then

```bash
printf 'function draw() local b = device.battery(); ui.title(device.name()); ui.big(b and (b.percent .. "%%") or "none") end\n' > /tmp/claude/b.lua
tools/sim/run.sh /tmp/claude/b.lua --battery 82+ --name kitchen --out /tmp/claude/b.png
```

Expected: tests pass; the PNG shows "kitchen" and "82%".
- [ ] **Step 5: Document** in `plugins/README.md`: a `### device` section after `net` with both calls, that the battery is `nil` unless the owner said the device has one, and the two sim options in the options table.
- [ ] **Step 6: Commit** `git commit -am "Plugins can read the battery and the device's name"`.

---

### Task 4: Brightness, night hours and buttons on the device

**Files:**
- Create: `src/DeskManners.h`, `src/DeskManners.cpp`
- Modify: `src/ConfigManager.h/.cpp`, `src/hardware/DisplayInterface.h/.cpp`, `src/hardware/NeoPixelController.h/.cpp` (header is in `src/hardware/` or `include/`: find it), `src/main.cpp`

**Interfaces:**
- Consumes: Task 1.
- Produces:

```cpp
// ConfigManager
deskhog::DeviceSettings getDeviceSettings();          // sanitized(); defaults when nothing is saved
void saveDeviceSettings(const deskhog::DeviceSettings& settings);

// NeoPixelController
void setLevel(uint8_t percent);   // atomic; scales breathing, overrides and flashes; 0 is dark

// DisplayInterface: setBrightness(uint8_t percent) drives the backlight PWM; 0 is off

// DeskManners (global `deskManners` in main.cpp, created after pluginManager)
class DeskManners {
public:
    DeskManners(ConfigManager&, DisplayInterface&, NeoPixelController&, PluginManager&);
    void begin();                                  // load settings, apply the day level at once
    void tick();                                   // UI task, every loop
    bool buttonPressed();                          // UI task; true: swallow this press
    void toggleNight();                            // UI task
    // Any task:
    deskhog::DeviceSettings settings();
    bool change(const std::vector<std::pair<std::string, std::string>>& changes, std::string& error);
    void preview(bool night);
    struct Status { bool night; bool manual; };
    Status status();
};
```

`change` validates with `applyDeviceSettings`, saves, and marks the settings dirty under a mutex; `tick` copies them into the rules. The fade: `tick` moves the applied screen and LED level toward the target so that a full 0–100 change takes about 500 ms.

- [ ] **Step 1: ConfigManager.** Store the settings in the `wifi_config` namespace next to `tz`, one key per field with a `dm_` prefix. Reading goes through `sanitized()`.
- [ ] **Step 2: Display and LED levels.** `DisplayInterface::begin` no longer writes 80% itself: it starts dark and `DeskManners::begin` sets the level before the first frame. `NeoPixelController::update` applies the level with `FastLED.setBrightness`.
- [ ] **Step 3: DeskManners** as above. Its `tick` asks `PluginManager::timeZone()` and `time(nullptr)` (treat anything before 2020 as "clock not set", i.e. pass 0).
- [ ] **Step 4: main.cpp.**
  - `buttonCheckInterval` 50 ms becomes 10 ms.
  - For each of the three `pressed()` branches: `if (deskManners->buttonPressed()) continue past it`; a swallowed press must not reach `handleButtonPress`. Check how `PluginCard` produces `"long"` and `"release"` and how `input.held` reads `buttons[]`: a swallowed press must not produce those either. If that needs a flag, `DeskManners` exposes `bool swallowing()` that stays true until all buttons are up.
  - Call `deskManners->tick()` in the loop after `processUIQueue()`.
  - Before `esp_deep_sleep_start()`: backlight 0, LED level 0 and one LED update, 50 ms delay.
- [ ] **Step 5: Build** `~/.platformio/penv/bin/pio run`. Expected: SUCCESS. Note `firmware.bin` size.
- [ ] **Step 6: Run** `test/native/run.sh`. Expected: pass.
- [ ] **Step 7: Commit** `git add -A src && git commit -m "Screen and LED levels, night hours and 10 ms buttons on the device"`.

---

### Task 5: Rotation, skip per card, and night by hand on the status card

**Files:**
- Modify: `src/DeskManners.*`, `src/plugins/PluginManager.h/.cpp`, `src/ui/CardController.h/.cpp`, `src/ui/CardNavigationStack.h/.cpp`, `src/ui/ProvisioningCard.h/.cpp`

**Interfaces:**
- Consumes: Task 2, Task 4.
- Produces:

```cpp
// PluginCardRecord
bool rotate = true;   // saved as "rotate": false, exactly as "front" is

// PluginManager, UI task
bool cardRotates(const std::string& key);     // like mayInterrupt
bool attentionPlaying(uint32_t nowMs);        // attention_.playing
bool attentionBanner();                       // attention_.bannerShowing
void setOnBanner(std::function<void(uint32_t forMs)> onBanner);  // called when a banner goes up

// CardNavigationStack
void setOnMovedByHand(std::function<void()> callback);  // from the nextCard/prevCard button path only

// CardController, UI task
std::vector<bool> rotationList();   // stack order; index 0 (status card) is false
int currentIndex();
void showIndex(int index);

// ProvisioningCard : public InputHandler
void setOnCenter(std::function<void()> callback);
void setNightLine(const String& text);        // "" hides it
```

- [ ] **Step 1: `rotate` per card** in `PluginManager`: field, save/load beside `front` (lines near 318 and 338), and a `rotate` pointer parameter wherever `mayInterrupt` is a parameter of the card update and add paths.
- [ ] **Step 2: Wire the rotation** in `DeskManners::tick`: build `Rotation::Input` from `CardController::rotationList()`, `currentIndex()`, the two attention accessors and `Levels.screen == 0`; on a result call `showIndex`. `movedByHand` comes from `CardNavigationStack::setOnMovedByHand` and from the web's show and press commands (`CommandKind::Show`/`Press` handling in `PluginManager`: add a callback `setOnMovedByHand` there too). A card brought to the front by `notify()` is not a move by hand.
- [ ] **Step 3: Banner wakes the screen:** `setOnBanner` calls `DeskRules::wake(now, Attention::kBannerMs)`.
- [ ] **Step 4: Status card.** `ProvisioningCard` implements `InputHandler`; `CardController` registers it like plugin cards (line near 234). ● calls the callback and returns true; ▲▼ return false. `DeskManners` sets the night line each time the mode or override changes: `"Night mode until 07:00"`, `"Day mode until 22:00"` (manual, with schedule), `"Night mode on"` (manual, no schedule), `""` otherwise. Label updates go through the card's existing `runOnUiTask`/`safeUpdateLabel` path.
- [ ] **Step 5: Build** with pio. Expected: SUCCESS.
- [ ] **Step 6: Run** `test/native/run.sh`. Expected: pass.
- [ ] **Step 7: Commit** `git commit -am "Cards rotate by themselves, and night mode by hand on the status card"`.

---

### Task 6: Battery gauge

**Files:**
- Create: `src/hardware/BatteryGauge.h`, `src/hardware/BatteryGauge.cpp`
- Modify: `src/main.cpp`, `src/DeskManners.*`, `src/ui/ProvisioningCard.*`

**Interfaces:**
- Produces:

```cpp
class BatteryGauge {
public:
    struct Reading { bool found = false; int percent = 0; float volts = 0; bool charging = false; };
    void begin();          // starts a task: core 0, priority 1, 3072 bytes of stack
    Reading reading();     // any task
};
// DeskManners
bool battery(deskhog::BatteryStatus& status);   // false unless settings.battery and the gauge was found
BatteryGauge::Reading gauge();                  // raw, for the API
// ProvisioningCard
void setBatteryLine(const String& text);        // "" hides the row
```

MAX17048 on `Wire` at address `0x36`, 16-bit big-endian registers: `VCELL 0x02` (78.125 µV per unit), `SOC 0x04` (1/256 %), `CRATE 0x16` (signed, 0.208 %/h per unit), `VERSION 0x08` to detect it. `Wire.setTimeOut(50)`. `TFT_I2C_POWER` (pin 7) must be HIGH before `Wire.begin()`; set it without disturbing the display. Read every 30 s; three failed reads in a row mean `found = false`. Percent is clamped to 0–100. Charging is `CRATE > 1 %/h`. No library: these are four register reads.

- [ ] **Step 1: Implement** `BatteryGauge`; start it in `setup()` after the display.
- [ ] **Step 2: Connect** `DeskManners::battery` and the status card row: `"82%"` or `"82%, charging"`, hidden when `battery()` is false.
- [ ] **Step 3: Build** with pio. Expected: SUCCESS.
- [ ] **Step 4: Commit** `git commit -am "Read the battery gauge"`.

This task cannot be tested without the board. It is on the device checklist in Task 10.

---

### Task 7: Device name, restart and the API

**Files:**
- Modify: `src/plugins/PluginApi.h/.cpp`, `src/plugins/PluginManager.h/.cpp`, `src/plugins/PluginConnection.cpp`, `src/main.cpp` or wherever `PluginManager` can reach `DeskManners` (give it `setDeskManners(DeskManners*)`), `src/hardware/WifiInterface.cpp`

**Interfaces:**
- Consumes: Tasks 4–6.
- Produces the JSON that Tasks 8 and 9 rely on. `GET /api/device`, and `device` inside `/api/state`:

```json
{ "name": "deskhog",
  "screen": { "day": 80, "night": 10 },
  "led": { "day": 100, "night": 0 },
  "night": { "enabled": false, "from": "22:00", "until": "07:00" },
  "rotate": { "seconds": 0 },
  "battery": false,
  "status": { "night": false, "manual": false,
              "battery": null,
              "gauge": { "percent": 100, "volts": 4.2, "charging": false } } }
```

`status.battery` is the reading when `battery` is true and the gauge was found, else `null`; `status.gauge` is the raw reading or `null` when no gauge answers.

`POST /api/device` takes the same shape without `status`, any subset. Extra field `"preview": "night"` or `"day"` calls `DeskManners::preview` and saves nothing by itself. Answers 200 with the full object as `GET` gives it, or 400 `{error}` and nothing changed. Unknown keys are a 400. The handler flattens the JSON to dotted pairs and calls `DeskManners::change`.

`POST /api/restart` answers 200, then restarts 500 ms later from a timer or task, never from inside the request handler.

Cards: `/api/cards/add` and `/api/cards/update` take `"rotate": true|false` as they take `front`; each card in `/api/state` reports `rotate`.

- [ ] **Step 1: Routes** `/api/device` and `/api/restart` in `registerPluginRoutes`, the `device` object in `handleState`, `rotate` in the two card handlers and in the card JSON. Update the comment block of routes in `PluginApi.h`.
- [ ] **Step 2: Name on the network.** `startNetworkServices` uses the name for `MDNS.begin`. On a name change (from the UI task or the network task, not the request handler): `MDNS.end()`, `MDNS.begin(newName)`, add the service again, and `WiFi.setHostname(newName)` for the next connection. `WifiInterface` sets the hostname before `WiFi.begin`.
- [ ] **Step 3: Plugins see the real values:** `PluginConnection::battery` and `deviceName` ask `DeskManners` through `PluginManager`.
- [ ] **Step 4: Build** with pio. Expected: SUCCESS.
- [ ] **Step 5: Commit** `git commit -am "The device's settings in the API, its name on the network, and restart"`.

---

### Task 8: The web page

**Files:**
- Modify: `html/portal.html`, `html/portal.js`, `html/portal.css`, `test/portal/pretend-device.js`
- Test: `test/portal/portal.test.js`

**Interfaces:**
- Consumes: the JSON of Task 7.
- Produces: in the pretend device, `device.device` (the object above, with `status.gauge` set and `battery` false), `GET/POST /api/device` with the same validation outcomes (400 with `{error}` for `screen.day < 5`, a bad name, unknown keys), `POST /api/restart`, `rotate` on cards, and a record of the last body posted to each, the way existing routes record theirs.

Layout as sketched in the spec's "Web page" section: panels "Display and light", "Name" and "Battery" between Time zone and About; Restart at the end of About.

- [ ] **Step 1: Write the failing tests:**
  - `shows the device's settings`: the four sliders hold 80, 10, 100, 0; night hours unticked with 22:00 and 07:00; rotate says Off.
  - `a slider saves its level`: moving screen day to 60 posts `{screen:{day:60}}`.
  - `dragging a night slider previews it`: the `input` event posts `{screen:{night:20}, preview:'night'}`.
  - `night hours are saved`: ticking the box and setting 23:15 posts `{night:{enabled:true}}` and `{night:{from:'23:15'}}`.
  - `rotate offers fixed steps`: options Off, 15 s, 30 s, 1 min, 2 min, 5 min; choosing 1 min posts `{rotate:{seconds:60}}`; a device value of 45 shows as an extra option "45 s".
  - `renaming sends you to the new address`: saving `kitchen` posts `{name:'kitchen'}` and the notice contains `http://kitchen.local`; the page title and header show the name.
  - `a bad name shows the device's reason` (Review Focus 4): the pretend device's 400 text appears in the notice, and the field keeps what was typed.
  - `battery line only with a battery`: with `status.battery` null About has no Battery row and the panel says a gauge was found; after ticking the box (pretend device then reports `{percent:82, volts:3.98, charging:true}`) About shows "82%, charging".
  - `no battery panel hint without a gauge`: `status.gauge` null: the tick box is still there, the hint says no gauge was found.
  - `restart asks first`: cancelling the confirm posts nothing; confirming posts to `/api/restart`.
  - `a card can be kept out of the rotation`: the card form has "Skip in rotation" unticked; ticking it posts `rotate:false` with the card update.
- [ ] **Step 2: Run** `test/portal/run.sh`. Expected: the new tests fail, the old pass.
- [ ] **Step 3: Implement.** Sliders post on `change` (saving) and on `input` at most every 150 ms (with `preview` for the night ones). Reuse `api`, `attempt`, `el` and the `field`/`check` classes. Hint under the card tick box: "The rotation passes this card by, and stays on it when you go there yourself." The rename notice: "Saved. This device is now at http://<name>.local".
- [ ] **Step 4: Run** `test/portal/run.sh`. Expected: all pass. Then `wc -c html/*` and note the total.
- [ ] **Step 5: Commit** `git commit -am "Device settings on the web page"`.

---

### Task 9: `tools/deskhog`

**Files:**
- Modify: `tools/deskhog`

**Interfaces:**
- Consumes: the JSON of Task 7.

- [ ] **Step 1: `device` command.** Without arguments it prints the settings as dotted `key = value` lines (the keys of Task 1) and a battery line from `status`. With `key=value` arguments it builds the nested JSON (`screen.day=60` to `{"screen":{"day":60}}`; numbers as numbers, `yes/no/true/false` as booleans for `night.enabled` and `battery`, `night.from` as text), posts it and prints the device's error on a 400.
- [ ] **Step 2: `restart` command:** posts, then waits up to 30 s for `/api/status` to answer again and says so.
- [ ] **Step 3: `config <card> rotate=no`** handled exactly where `front=no` is.
- [ ] **Step 4: Usage text** in the module docstring and the argparse help.
- [ ] **Step 5: Check** against the pretend device:

```bash
node test/portal/serve.js &   # port 8080
tools/deskhog --host localhost:8080 device
tools/deskhog --host localhost:8080 device screen.day=60 night.enabled=yes night.from=22:30
tools/deskhog --host localhost:8080 device screen.day=2     # prints the 400 reason, exits non-zero
kill %1
```

Expected: the second `device` call shows `screen.day = 60`, `night.enabled = true`, `night.from = 22:30`. If `--host` does not take a port, check how `serve.js` is meant to be reached and use that.
- [ ] **Step 6: Commit** `git commit -am "deskhog device and deskhog restart"`.

---

### Task 10: Docs and the final checks

**Files:**
- Modify: `README.md`, `plugins/README.md`, `tech-details.md`

- [ ] **Step 1: `README.md`.** In "Using the device": a row for ● on the status card (night mode by hand), and a short section "Brightness, night hours and rotation" that says what the Device tab offers, that the LED is dark at night by default once night hours are on (notify flashes included), that a skipped card also stays on screen, and that the battery line needs "This device has a battery". Mention `<name>.local` in "Set it up".
- [ ] **Step 2: `plugins/README.md`.** The device API block gains `/api/device` and `/api/restart` and `rotate` on the card routes; the `tools/deskhog` list gains `device`, `restart` and `config weather:2 rotate=no`; the `notify` section says the LED flash follows the device's LED level.
- [ ] **Step 3: `tech-details.md`.** A "Device settings" section: `DeskRules`/`Rotation` in `plugin_core`, `DeskManners` in the firmware, which task does what, the battery task. Correct the web page size and budget sentence to the measured number. Correct the firmware size sentence in "Flash layout" to the measured `firmware.bin`.
- [ ] **Step 4: Run everything:**

```bash
test/native/run.sh && test/portal/run.sh && tools/deskhog index --check && ~/.platformio/penv/bin/pio run
```

Expected: all four succeed.
- [ ] **Step 5: Commit** `git commit -am "Docs for the device's settings"`.
- [ ] **Step 6: Hand Tim the device checklist** (nothing here can be checked without the board):
  1. After flashing, the device looks as before: 80% screen, LED breathing, no rotation, `deskhog.local`.
  2. Sliders change screen and LED while dragged; levels fade.
  3. Night hours set a minute ahead: screen and LED go to their night levels on the minute.
  4. Night level 0: a press lights the screen and does nothing else; a second press works; dark again after 30 s.
  5. ● on the status card switches night mode and the line says until when.
  6. Rotation at 10 s: skips the status card, waits during a game and for a minute after a button press, stays on a skipped card.
  7. A `notify()` at night lights the screen for the banner.
  8. Without a battery: the Battery panel says a gauge was found (or not) and shows what it reads; with the box ticked and a battery, the status card shows a believable percentage. No freezes either way.
  9. Rename to something else: the new `.local` address answers, the old one stops.
  10. Restart from the page: the device restarts and the page finds it again.
  11. Games feel no worse, and ● + ▼ for two seconds still powers off, now with the screen and LED dark.
