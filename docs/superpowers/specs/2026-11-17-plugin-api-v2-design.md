# Plugin API v2: design

## Why

Plugins work around missing pieces of the API today: `clock.lua` and `countdown.lua`
ask for "hours ahead of UTC" (wrong half the year with summer time), `daylight.lua`
polls a weather API for its offset, `homey.lua` can't switch devices (no PUT), no
plugin can tell you a timer ended while you're on another card, and every service has
to be polled. v2 closes those gaps, and ships new plugins that show each feature off.

## Goals

- Every feature below, in the runtime (`lib/plugin_core`), the firmware host
  (`src/plugins`, `src/ui`), the simulator (`tools/sim`), the web page (`html/`),
  `tools/deskhog` where relevant, the native tests and `plugins/README.md`.
- Fully backward compatible: every existing plugin keeps working unchanged
  (`test/native/run.sh` runs them all).
- New demo plugins (below), each with a screenshot and an `index.json` entry.

## Non-goals / anti-goals

- No sound, no OTA, no login for the device API (it stays "trusted network", as
  documented today).
- A plugin must still never take the device down: every new callback runs through the
  same protected call, time budget and memory cap. New features have hard limits.
- A plugin must never be able to hijack the screen: interruptions are opt-out per card,
  rate-limited and wait while someone is playing.

## Features (API as plugins see it)

### 1. Local time
- Device-wide time zone setting, stored as a POSIX TZ string, chosen on the web page
  from a list of IANA names (the page maps name → POSIX string; no tz database on the
  device). Default: UTC. Exposed in `/api/state`, settable by an API call.
- `time.date([t], [tz])` → `{year, month, day, hour, min, sec, wday (1=Sunday), yday,
  isdst, offset (seconds east of UTC)}`. `t` defaults to `time.now()`, `tz` (a POSIX TZ
  string) defaults to the device zone.
- `time.format(fmt, [t], [tz])`: strftime subset (`%Y %m %d %H %M %S %I %p %a %A %b %B %j %Z %%` at least).
- `time.zone()` → the device's POSIX string; `time.offset([t], [tz])` → seconds east of UTC.
- Implement POSIX TZ parsing and DST rules in `plugin_core` itself (portable, same in
  sim and firmware, unit-tested), not via global `setenv("TZ")`.
- Config field `type = "timezone"`: the web page shows the time-zone picker and saves
  the POSIX string (for World Clock's per-card zone). Empty means the device zone.
- Migrate `clock.lua` and `countdown.lua` to the device zone; drop their offset setting
  (bump their versions).

### 2. HTTP
- `http.request{ method, url, body, headers, timeout, only, insecure }` with a callback,
  plus shortcuts `http.put`, `http.patch`, `http.delete` (same shape as `http.post`;
  `delete` has no body). `HEAD` allowed via `request`.
- `res.headers`: table of response headers, names lowercased; total kept capped (~2 KB).
- TLS verified by default using the ESP-IDF certificate bundle (replace `setInsecure()`
  in `PluginManager.cpp`). `insecure = true` per request opts out (self-signed LAN
  boxes). The sim honours it (`curl -k`). README states the new behaviour.
- `math.random` seeded from a hardware random source at start (check first whether it
  already is).

### 3. Network status
- `net.online()`, `net.rssi()` (dBm, or nil offline), `net.ssid()`, `net.ip()`.

### 4. Drawing
- `gfx.circle(x, y, r, [color], [width])`: with `width`, an outline ring.
- `gfx.arc(x, y, r, startDeg, endDeg, [color], [width])`: 0° = up (12 o'clock), clockwise.
- `gfx.poly(points, [color])`: filled polygon, `points = {x1,y1,x2,y2,...}`;
  `gfx.triangle(x1,y1,x2,y2,x3,y3,[color])` as a convenience.
- Font `"tiny"`: ~10 px, Western European characters, for dense lists. Generated like
  the other fonts (from `typography/`).
- Images from the network: `gfx.fetchImage(url, [opts], callback)`, callback gets
  `(image, err)`. PNG and JPEG (LVGL 9.2 already ships decoders; the sim needs a
  matching decoder). `opts.width/height` scale it down to fit; max 240×240 after
  scaling; counts against the connection's existing 512 KB image budget; uses the same
  HTTP path and limits. `gfx.release(image)` gives budget back.

### 5. Buttons
- `button(name, event)`: `event` is `"press"` (today's behaviour, unchanged return
  semantics), `"long"` (held ≥ 600 ms, once per hold), `"release"`. Existing plugins
  ignore the second argument and see exactly what they see today. Navigation only
  ever reacts to an unconsumed `"press"`. The "hold centre + up = previous card" escape
  still always works.

### 6. Timers
- `timer.after(seconds, fn)`, `timer.every(seconds, fn)` → handle with `:cancel()`.
  Run whether or not the card is visible; min interval 0.1 s visible / 1 s hidden; at
  most 16 per connection. Callbacks repaint the card. Card functions get the card as
  usual through closures.

### 7. Attention (decision: option (c), per card)
- `notify([text], [opts])` (single-card plugins) / `card.notify(...)` (multi-card).
  `opts.led` optional colour to flash.
- Each card gets a built-in setting "May come to the front" (default yes), shown on the
  web page for every plugin card, stored with the card.
- When allowed: the card comes to the front, with `text` as a banner the host draws
  over the card for ~4 s. If the card on screen is being played (it consumed a button
  press in the last 5 s), wait until it's been idle 5 s, then interrupt.
- When not allowed (or still waiting): LED flash only, and the request is remembered so
  the card shows the banner when you next arrive at it.
- Rate limit: one interruption per card per 30 s; extras collapse into the pending one.
- `/api/state` exposes pending notifications.

### 8. Push messages
- `POST /api/cards/message?key=<c>:<n>` (body: text or JSON, ≤ 4 KB) calls
  `message(msg)` / `card` kind's `message(card, msg)` with `msg = {body, json}`.
  `POST /api/connections/message?id=<c>` calls the global `message(msg)`.
  Queue at most 8 pending per connection; 429 beyond that; 404 if the plugin has no
  `message` handler. No extra auth (same as the rest of the API; README says so).
- `tools/deskhog send <card> <text|@file>`; sim `--message <seconds>=<text|@file>`.

### 9. Choices filled in by the plugin
- A config field may say `choices = "functionName"`. The web page calls
  `GET /api/connections/choices?id=<c>&field=<key>[&kind=<kind>]`, which runs that
  global function in the connection's Lua state as `fn(done)`, and `done(list)` answers
  with `{ {value=, label=}, ... }` (may call `http` first; 10 s timeout). The page shows
  a dropdown with a Refresh button, falling back to free text on error.

## Demo plugins (new unless noted)

| Plugin | Shows |
|---|---|
| `worldclock.lua` | A card per city: `time.date` with a per-card `timezone` field, day/night tint |
| `doorbell.lua` | Push: `deskhog send doorbell "Pizza's here"` → banner, LED, last few messages |
| `ciwatch.lua` | GitHub Actions runs in the tiny font; ETag + `If-None-Match`, rate limit from headers; `notify` when a run goes red |
| `reminders.lua` | Daily reminders from config ("10:30 Stand-up; 15:00 Stretch"); local time, `timer`, `notify` |
| `nowplaying.lua` | Last.fm recent track with album art: `gfx.fetchImage` |
| `gauge.lua` | Round dial for a number from a JSON URL (or Home Assistant sensor): `arc`, `poly` needle |
| `wifi.lua` | Signal meter to walk around with: `net.*`, arcs, history chart |
| `charge.lua` | Hold-and-release power shot game: `long`/`release` events |
| `homey.lua` (update) | Toggle devices with PUT; pick device/flow from `choices` |

Each new plugin needs `name`, `description`, `version`, a screenshot via
`tools/screenshots` (with a recipe where it needs settings/presses/replies), and then
`tools/deskhog index` once at the end.

## Approaches considered

- **Host does local time via `setenv("TZ")`/`localtime_r`**: rejected, global state
  shared with other tasks and no per-call zone; parse POSIX TZ in `plugin_core` instead.
- **Interrupt policy**: (a) never interrupt, (b) always interrupt unless playing,
  (c) per card setting defaulting to (b). Tim picked (c).
- **Push auth token**: rejected for consistency with the rest of the unauthenticated
  API; documented instead.

## Builder's calls

Exact limits not stated above, file layout inside `plugin_core`, how LVGL draws arcs
and polygons, the sim's image decoder, banner styling, and web page layout.

## Verification

`test/native/run.sh` (new unit tests per feature, all examples load),
`test/portal/run.sh`, `pio run` builds the firmware, `tools/sim/run.sh` renders each
new plugin, `tools/screenshots` produces their pictures.
