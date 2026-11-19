# Desk manners: design

## Why

DeskHog sits on a desk all day and, for many people, in a room they sleep or relax in.
Today the screen is fixed at 80% brightness, the LED breathes at full strength around
the clock, cards only change when someone presses a button, the battery level is
invisible, every device calls itself `deskhog.local`, restarting means reaching for the
reset tab, and buttons are read only twenty times a second. None of that is a plugin's
job: it is how the device itself behaves. This release gives the device a set of
settings of its own.

Tim approved the feature list and the behaviour below on 2026-11-19. He was asked whether
his device runs on a battery and did not say, so battery support is the reading only
(builder's call, below).

## Goals

One group of device settings, saved on the device, changed on the web page's Device tab,
through the HTTP API and with `tools/deskhog`:

1. Screen brightness, by day and by night.
2. Night hours.
3. LED brightness, by day and by night.
4. Auto-rotate through the cards.
5. Battery level: on the status card, in About, and readable by plugins.
6. A device name that is also its address on the network.
7. Restart from the web page and the command line.
8. Buttons read every 10 ms.

Everything is covered in the firmware (`src/`), the shared logic (`lib/plugin_core`), the
simulator (`tools/sim`) where a plugin can see it, the web page (`html/`), the pretend
device (`test/portal`), `tools/deskhog`, the native and portal tests, `plugins/README.md`,
`README.md` and `tech-details.md`.

## Non-goals

- No idle dimming. A desk display is idle by nature; only the night hours dim it.
- Plugins cannot change brightness, rotation or any other device setting. They can read
  the battery, nothing else.
- No low-battery warning and no dimming while unplugged.
- No live web page (the next project), no firmware update changes, no login.
- No change to the flash layout.

## Anti-goals

- A device that someone cannot get out of the dark: any button press must light the
  screen, whatever the settings say, and a first boot or a device whose clock is not set
  must never be in night mode.
- Auto-rotate that takes a card away from someone who is using it.
- A setting that existing devices get surprised by: after updating, a device behaves as
  before (80% screen, LED as it is, no rotation, name `deskhog`) until someone changes
  something.
- A plugin taking the device down or hijacking it: the existing rules stay as they are.

## Constraints

- Task rules from `tech-details.md`: LVGL and plugins only on the UI task (core 1). The
  web server runs on core 0 and hands changes over the way `setTimeZone` does today. The
  LED has its own task on core 0 and takes values through atomics, as `setOverride` does.
- Settings live in NVS through `ConfigManager`, next to the time zone. NVS keeps its
  offset, so nothing is lost on update.
- The web page is 113 KB of source against the 100 KB budget in `tech-details.md`. This
  work adds to it. Keep the addition small (a target of 5 KB) and correct the number in
  `tech-details.md` to what is true after the change; cutting the page down is not part
  of this work.
- Fully backward compatible for plugins: every plugin in `plugins/` keeps working
  unchanged, and `test/native/run.sh` proves it.

## Approaches considered

- **A device settings block with the rules in `plugin_core` (chosen).** One struct of
  settings in NVS, one `GET`/`POST` pair in the API, and a small class without hardware
  in `lib/plugin_core` that answers "is it night?", "what brightness now?" and "rotate
  now?". The firmware applies its answers. The rules get native tests, and the rotation
  shares the "someone is playing" knowledge that `Attention` already has.
- **Each feature on its own**, with a route and NVS keys per feature, as the time zone
  has today. Rejected: seven routes and seven panels for what people see as one page of
  settings, and the night rule would be written twice (screen and LED).
- **A built-in settings card on the device**, changed with the three buttons. Rejected
  for now: three buttons make a poor form, and everything else is set on the web page.
  One thing is offered on the device because it is needed away from a computer: night
  mode by hand (below).

## Behaviour

### Settings

| Setting | Values | Default |
|---|---|---|
| `name` | 1 to 24 characters: lowercase letters, digits and `-`; not starting or ending with `-` | `deskhog` |
| `screen.day` | 5 to 100 (%) | 80 |
| `screen.night` | 0 to 100 (%); 0 is off | 10 |
| `led.day` | 0 to 100 (%) | 100 |
| `led.night` | 0 to 100 (%) | 0 |
| `night.enabled` | true/false | false |
| `night.from`, `night.until` | minutes after midnight, in the device's time zone | 22:00, 07:00 |
| `rotate.seconds` | 0 (off), or 10 to 600 | 0 |
| `battery` | true/false: this device has a battery | false |

`screen.day` has a floor of 5% so that nobody locks themselves out by day.

### Night hours

- It is night when night hours are enabled, the clock is set, and the local time (device
  time zone, summer time included) is between `from` and `until`. `from` later than
  `until` crosses midnight; equal values mean never.
- At night the screen and the LED use their night levels. Changes between levels fade
  over about half a second, they do not jump.
- **Any button press at night wakes the screen** to the day level for 30 seconds after
  the last press. The press that wakes a screen that was off (night level 0) does only
  that: it is not passed to the card and does not switch cards. With a night level above
  0 the press works as always and also brightens the screen.
- A `notify()` that brings a card to the front, or shows its banner, also wakes the
  screen for the time the banner shows. The LED flash of a notify, and a plugin's
  `led.set`, are scaled by the LED level in use, so at the default night level of 0 the
  LED stays dark all night. That is what the night setting is for; the guide says so.
- **Night mode by hand.** On the status card, pressing ● switches night mode on or off
  right away. The choice holds until the next time the schedule would change the mode
  (or, when night hours are disabled, until ● is pressed again or the device restarts).
  The status card shows a line saying so while it applies. It is not saved across a
  restart.

### LED

`led.day` and `led.night` scale everything the LED shows: the breathing glow, `led.set`
colours and notify flashes. 0 switches it off.

### Auto-rotate

- With `rotate.seconds` above 0 the device moves to the next card when the card on
  screen has been showing that long, and wraps from the last to the first.
- It skips the status card, and cards with "skip in rotation" set. Each card has that
  setting next to "May come to the front", stored and reported the same way (`rotate`,
  true unless set to false).
- The rotation never arrives at such a card, and never leaves one either: someone who
  pages to the status card to read the address, or to a skipped card such as a timer,
  keeps it on screen until they press a button. So "skip in rotation" is also how a
  card is pinned. The web page's hint and the guide say both halves.
- It waits: while the card on screen is being played (the `Attention` rule: a used press
  in the last 5 seconds, or a button held), while a notify banner is up, for 60 seconds
  after someone changed cards by hand or from the web page, and while the screen is off
  at night.
- With fewer than two cards to rotate between it does nothing.
- A card that came to the front through `notify()` stays for a full interval before the
  rotation moves on.

### Battery

- The firmware reads the board's fuel gauge (MAX17048 on the I2C bus, per Adafruit's
  documentation for this board) every 30 seconds, on a task that is allowed to block,
  never on the UI task.
- It reports a percentage (0 to 100), the voltage, and whether it is charging (from the
  gauge's rate of change).
- The gauge cannot say that no battery is plugged in: on USB alone it is expected to
  read the charger, about 4.2 V and 100%. So the reading is only shown when the person
  says there is a battery: a setting `battery` (true/false, default false), a tick box
  "This device has a battery" on the Device tab. With it off, or when the gauge does not
  answer, there is no battery: the status card and About leave the line out and plugins
  get `nil`. `GET /api/device` reports the raw reading either way, as `gauge`, so the
  page can say "a battery gauge was found" next to the tick box, and so the behaviour
  without a battery can be checked on a real device.
- The status card shows "Battery 82%" (with "charging" when it is); About shows the same.
- Plugins: `device.battery()` returns `nil` without a battery, otherwise
  `{ percent = 82, volts = 3.98, charging = false }`. `device.name()` returns the
  device's name. The simulator takes `--battery 82` (and `--battery 82+` for charging);
  without it there is no battery.
- This has never been tried on a device. If the gauge is not there or behaves
  differently, the feature must fail to "no battery", not to wrong numbers or a hang.

### Device name

- The name becomes the mDNS name (`<name>.local`) and the WiFi hostname. Changing it
  takes effect at once; the API answers first, so the web page can tell the person the
  new address and send them there.
- The web page's title and header show the name, so two devices' tabs can be told apart.
- `tools/deskhog` keeps `deskhog.local` as its default host; `--host` is how a renamed
  device is reached.

### Restart

`POST /api/restart` answers, then restarts the device after a moment, so the answer gets
out. The web page asks for confirmation, then waits for the device to come back.
`tools/deskhog restart` does the same.

### Buttons

Buttons are read every 10 ms instead of every 50 ms. Everything else about them stays:
the 5 ms debounce, ● with ▲ for the previous card, ● with ▼ for two seconds to power
off. The power-off also switches the backlight and the LED off before sleeping.

## API

```
GET  /api/device     the settings above as JSON, plus `status`: {night: true|false, manual: true|false,
                     battery: {percent, volts, charging} | null, gauge: the same, raw | null}
POST /api/device     any subset of the settings; what is left out stays. 400 with a reason for a bad value,
                     and then nothing is changed.
POST /api/restart
```

`/api/state` also carries `device` (the same object), so the page needs no extra request.
`/api/cards/add` and `/api/cards/update` take `rotate` the way they take `front`.

`tools/deskhog`:

```
tools/deskhog device                          show the settings and the battery
tools/deskhog device screen.day=60 night.from=22:30 night.enabled=yes rotate.seconds=30
tools/deskhog config weather:2 rotate=no      as front=no works today
tools/deskhog restart
```

## Web page

On the Device tab, between Time zone and About, in the page's existing style:

```
Display and light
  Screen      [====|----] 80%        at night [=|-------] 10%
  LED         [========|] 100%       at night [|--------] off
  Night hours [x] from [22:00] until [07:00]      (uses the time zone above)
  Rotate cards [ Off v ]   every 15 s / 30 s / 1 min / 2 min / 5 min

Name
  [ deskhog ]   http://deskhog.local

Battery
  [ ] This device has a battery     (a battery gauge was found)

About
  ... Battery 82%, charging ...
  [ Restart ]
```

Sliders apply while they are dragged, so people see the screen and the LED change; while
a night slider is dragged the device shows the night level for a few seconds as a
preview. Each card's settings get "Skip in rotation" next to "May come to the front".

## Left to the builder

- Names and layout of classes and files, the NVS key names, the fade, the gauge driver
  (a library or a few register reads), exact wording on the web page and status card.
- How the rules class in `plugin_core` is shaped, as long as it has no Arduino or LVGL
  in it and the native tests cover: night across midnight, summer-time changes, the
  clock not set, the manual override ending at the next boundary, the wake on a press,
  and every reason the rotation waits.
- How the pretend device in `test/portal` plays these settings.

## Builder's calls already made

- Battery is the reading only (no warning, no dimming on battery): Tim did not say
  whether he uses one.
- Defaults that keep a device as it is after the update; night hours off until enabled.
- `screen.day` floor of 5%; the first press with the screen off only wakes it.
- The manual night switch lives on ● of the status card and is not saved.
- `device.name()` for plugins, alongside `device.battery()`.
- The battery reading is opt-in ("This device has a battery"), because the gauge reads
  about 100% with no battery plugged in. Raised by the design review.
- A card that the rotation skips also holds the screen when someone goes to it by hand.
  Raised by the design review.

## Verification

- `test/native/run.sh`, `test/portal/run.sh` and `tools/deskhog index --check` pass.
- The firmware builds (`pio run`), and its size is noted against the 2.9 MB slot.
- On a real device, which the builder cannot do alone: brightness and LED sliders, a
  night window set a minute ahead, waking by button with the night level at 0, rotation
  with a game on screen, the battery reading with and without a battery, renaming and
  reaching the new address, restart. These are listed for Tim to try.
