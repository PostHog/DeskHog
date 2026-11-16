# Writing DeskHog plugins

A plugin is one Lua file. Put it on the device and it shows up as a card, with no
recompiling and no reflashing. The files in this folder are working examples. This
folder is also the plugin directory: what the device's web page offers under
"Get plugins" (see [Publishing a plugin](#publishing-a-plugin)).

| Example | Shows |
|---|---|
| [hello.lua](hello.lua) | The smallest useful plugin |
| [weather.lua](weather.lua) | Several cards from one plugin, shared settings, fetching JSON, a chart |
| [posthog.lua](posthog.lua) | A real service: one account, a card per insight, an API key, large answers |
| [flappy.lua](flappy.lua) | A game: per-frame updates, free drawing, saved high score |
| [webhook.lua](webhook.lua) | A button that calls a URL and lights the LED |
| [clock.lua](clock.lua) | The time: working with `time.now()`, repainting only when something changed |
| [pomodoro.lua](pomodoro.lua) | A timer that keeps going while another card is on screen |
| [pet.lua](pet.lua) | State that survives restarts, and catching up on the time the device was off |
| [snake.lua](snake.lua), [breakout.lua](breakout.lua) | Games that draw a lot with few calls |
| [github.lua](github.lua) | Two requests per card, an optional token, the LED as an alarm |
| [trains.lua](trains.lua) | A list that counts down between answers |
| [homeassistant.lua](homeassistant.lua) | Reading and controlling something on your own network |
| [friend.lua](friend.lua) | Max the hedgehog: images and animation |
| [question.lua](question.lua) | Icebreaker questions: wrapping text, and scrolling what does not fit |
| [paddle.lua](paddle.lua) | A game with held buttons, that gives the buttons back when it is paused |
| [rps.lua](rps.lua), [whack.lua](whack.lua) | Games made for three buttons: one button per choice |
| [trivia.lua](trivia.lua) | A quiz: fetching a batch once and playing from it, leaving out what does not fit |
| [lander.lua](lander.lua), [runner.lua](runner.lua) | Simple physics, and a button that works pressed and held |
| [blocks.lua](blocks.lua) | A grid game that shows its chain reactions a step at a time |

The rest of the folder is there to be used rather than studied: countdown, crypto
prices, dad jokes, a deploy button, Homey, theme park queues, rain, a reaction timer,
Simon, the space station and rocket launches, a starfield and an uptime check.

## Five-minute start

```lua
-- counter.lua
plugin = { name = "Counter" }

local count = 0

function draw()
  ui.title("Counter")
  ui.big(tostring(count))
end

function button(name)
  if name == "center" then
    count = count + 1
    return true
  end
end
```

See it on your computer first, without a device:

```sh
tools/sim/run.sh counter.lua --press center@0.5,center@1   # writes counter.png
```

Then put it on the device. It restarts within a second of every push:

```sh
tools/deskhog push counter.lua     # install, then report how it started
tools/deskhog watch counter.lua    # push again on every save
tools/deskhog logs counter -f      # follow print() output
```

`tools/deskhog` looks for the device at `deskhog.local`. If that name does not
resolve on your network, add `--host <ip>` with the address on the device's status
card. You can also paste or upload plugins on the device's web page, under **Plugins**.

More of the tool, for working without getting up:

```sh
tools/deskhog list                        # plugins and cards; cards are named connection:card
tools/deskhog screenshot counter          # save what the card shows right now as a PNG
tools/deskhog press counter center        # press a button for it
tools/deskhog config weather unit=F       # change a connection's shared settings
tools/deskhog config weather:2 city=Oslo  # change one card's settings
```

## How a plugin runs

The file runs once from top to bottom. After that the device calls the functions
you defined. All of them are optional.

| Function | Called |
|---|---|
| `load()` | Once at start, again every `plugin.refresh` seconds, and when WiFi connects |
| `update(dt)` | Every frame while the card is on screen. `dt` is the seconds since the last call. Return `false` when nothing you draw has changed, and the card is not repainted |
| `draw()` | When the card needs repainting: after any of the other functions ran |
| `button(name)` | On a press. `name` is `"up"`, `"center"` or `"down"` |

Two tables at the top of the file describe the plugin:

```lua
plugin = {
  name = "Weather",   -- shown on the web page and on the crash card
  description = "The temperature now, for any number of cities", -- one line, for the plugin directory
  author = "You",
  version = "1.0",    -- change it when you publish a change
  refresh = 900,      -- call load() again every 900 seconds; leave out for never
  fps = 30,           -- how often update(dt) runs; 30 if you leave it out, 1 to 60
}

config = {
  city = { label = "City", default = "Ghent" },
  days = 3,           -- short form: just the default
}
```

After the file has run, each `config` entry is replaced by its value: the saved
setting if there is one, otherwise the default. So in `load()` and everywhere else,
`config.city` is simply `"Ghent"`. People change settings on the device's web page;
the plugin restarts with the new values. A setting keeps the type of its default
(text, number or true/false).

A setting can say more about itself:

```lua
config = {
  key  = { label = "API key", secret = true, order = 1 },           -- write-only on the web page
  unit = { label = "Unit", default = "C", options = { "C", "F" } }, -- a dropdown
}
```

Lua tables have no order, so the web page shows fields by their `order` (lowest first)
and the rest by name.

## Several cards from one plugin

A plugin can offer more than one card, and more than one kind of card. Think of a
PostHog plugin: one account, many insight cards. Instead of the global `draw()` and
friends, give the plugin a `cards` table with an entry per kind:

```lua
plugin = { name = "Weather", refresh = 900 }

config = {                                   -- shared by all of this plugin's cards
  unit = { label = "Unit", default = "C", options = { "C", "F" } },
}

cards = {
  city = {                                   -- a kind of card
    name = "City",
    config = {                               -- set per card
      city = { label = "City", default = "Ghent" },
    },
    load = function(card) ... end,           -- at start, on refresh, when WiFi connects
    update = function(card, dt) ... end,     -- every frame while this card is on screen
    draw = function(card) ... end,
    button = function(card, name) ... end,
  },
}
```

Each function gets `card`, a table that belongs to that one card:

| Field | Holds |
|---|---|
| `card.config` | This card's settings, resolved like `config` |
| `card.id`, `card.kind` | Which card this is |
| `card.title` | Set it to tell the web page what to call the card, e.g. the city it found |
| anything else | Yours. Keep the card's data here: `card.temp = 21` |

What this buys you:

- **One program, many cards.** All cards of a connection run in the same Lua state,
  so they share variables, one login and one set of requests. A global `load()` still
  runs first, for data every card needs.
- **Connections.** The shared `config` plus the cards using it is called a connection.
  Someone with two accounts makes two connections of your plugin; each runs as its own
  copy, with its own settings and its own `store`.
- **A crash is shared too.** If any card's function fails, every card of that
  connection shows the error.

A plugin without a `cards` table has one kind of card, and every card someone adds
runs as its own copy. Two Flappy cards are two separate games.

[weather.lua](weather.lua) is the complete example.

### Buttons and moving between cards

Up and down switch cards. Your `button` function sees every press first. Return
`true` to use the press yourself; return nothing or `false` to let it switch cards.
A plugin that wants all three buttons can have them, and the person holding the
device can still leave: **holding centre and pressing up always goes to the previous
card.**

## Drawing

The card is 233 by 135 pixels (`gfx.width`, `gfx.height`), black, with `0,0` in the
top left. Drawing only works inside `draw()`. Each `draw()` starts from a black card.

### `ui`: laid out for you

For cards that show data. Call these in any order and the device arranges them.

| Call | Draws |
|---|---|
| `ui.title(text, [color])` | Small text in the top left |
| `ui.big(text, [color])` | The main value, large and centred. It shrinks to fit |
| `ui.label(text, [color])` | Small text under the value. Text too wide for the card continues on the next line. Use several for separate lines |
| `ui.chart(values, [color])` | A line chart along the bottom, from a list of numbers |

### `gfx`: draw it yourself

For games and anything custom. Coordinates may be fractional; they are rounded.

| Call | Draws |
|---|---|
| `gfx.clear([color])` | Fill the card |
| `gfx.rect(x, y, w, h, [color], [radius])` | Filled rectangle, optionally with round corners |
| `gfx.outline(x, y, w, h, [color], [width])` | Rectangle outline |
| `gfx.line(x1, y1, x2, y2, [color], [width])` | Line |
| `gfx.circle(x, y, radius, [color])` | Filled circle around `x, y` |
| `gfx.pixel(x, y, [color])` | One pixel |
| `gfx.text(x, y, text, [color], [font], [align])` | Text, up to 255 bytes per call. `y` is its top edge |
| `gfx.textWidth(text, [font])` | Width in pixels. Works outside `draw()` too |
| `gfx.textHeight([font])` | Line height in pixels |
| `gfx.wrap(text, width, [font])` | Not a drawing call: returns the text as a list of lines that each fit `width`. Works outside `draw()` too |
| `gfx.clip(x, y, w, h)` | Until `gfx.clip()` without arguments, only draw inside this rectangle. For text that scrolls |
| `gfx.draw(image, x, y, [scale])` | An image made with `gfx.image`, optionally 2 to 8 times its size |

**Colors** are `"#rrggbb"`, `"#rgb"`, a number like `0xFF8800`, or a name: `white`,
`black`, `red`, `green`, `blue`, `yellow`, `orange`, `purple`, `gray`. White is the
default.

**Fonts** are `"small"` (the default, 15px), `"medium"` (16px, semibold), `"large"`
(36px, semibold) and `"loud"` (20px, a chunky display face with basic Latin only).
The first three cover Western European characters such as `é` and `°`.

**Align** is `"left"` (the default), `"center"` or `"right"`. It says which part of
the text sits at `x`.

### Images

An image is written as text, one character per pixel, with a table that says which
colour each character stands for. Make it once, at the top of the file, and draw it
as often as you like:

```lua
local heart = gfx.image({
  colors = { ["."] = "none", r = "#e74c3c", w = "white" },
  ".rr.rr.",
  "rwrrrrr",
  "rrrrrrr",
  ".rrrrr.",
  "..rrr..",
  "...r...",
})

function draw()
  gfx.draw(heart, 20, 20, 4) -- four times its size: 28 by 24 pixels
end
```

`"none"` is see-through. `heart.width` and `heart.height` give its size. For frames
of an animation, make one image per frame and pick one in `draw()`.

`tools/deskhog image walk1.png walk2.png --name walk` writes this text for you from
PNG files, with one shared set of colours. Pixel art with a handful of colours is
what fits: a plugin file is at most 64 KB, and every pixel is a character in it.
[`friend.lua`](friend.lua) is six frames of Max walking.

## Talking to the world

### `http`

```lua
http.get(url, function(res) ... end)
http.get(url, { headers = { Authorization = "Bearer ..." }, timeout = 5 }, function(res) ... end)
http.post(url, body, function(res) ... end)   -- options go between body and callback
```

Requests run in the background and your function is called with the result, after
which the card is repainted. `body` is text, or a table that is sent as JSON.

| Field | Meaning |
|---|---|
| `res.ok` | `true` for a 2xx status |
| `res.status` | The HTTP status, or `0` if no answer came |
| `res.body` | The response as text |
| `res.json` | The response decoded, when it is JSON; otherwise `nil` |
| `res.error` | Why it failed, when no answer came (for example `"no WiFi"`) |

Limits: twelve requests in flight per connection, responses up to 48 KB, a timeout of 10
seconds unless you set one (1 to 30). HTTPS works, but certificates are not verified yet, so do not send secrets you could not afford to leak on an untrusted network.

**Large answers.** Many APIs send far more than a small screen can show. Say which
parts you want with `only`, and the device reads answers up to 256 KB while keeping
just those parts in memory:

```lua
http.get(url, { only = { results = { { name = true, value = true } } } }, function(res)
  print(res.json.results[1].name)   -- everything else was never stored
end)
```

The filter mirrors the shape of the data. `true` keeps a value whole. A table keeps
only the named keys of an object. For a list, the filter's first entry applies to every
item; a table without one is applied to each item as it is, so one filter covers "an
object or a list of them". With `only`, `res.body` is not available.

### `json`

`json.decode(text)` returns the value, or `nil` and a message. `json.encode(value)`
returns text. Arrays are 1-based tables. A JSON `null` becomes `nil`, except inside
an array, where it becomes `json.null` so that later items keep their position.

### `led`

```lua
led.set("green")          -- until you change it
led.set("#ff8800", 3)     -- for three seconds, then back to normal
led.set(255, 136, 0)      -- red, green, blue
led.off()                 -- back to the normal breathing glow
```

### `store`

Small values that survive restarts and power cuts, private to one connection of your
plugin (so all of its cards share them).

```lua
store.set("best", 42)
local best = store.get("best", 0)   -- the second argument is returned when nothing is saved
```

Values can be numbers, text, true/false or tables of those. Each connection gets 32 keys,
up to 2 KB per value and 8 KB in total. Writes go to flash, so save when something
changes, not every frame.

### `time` and `input`

| Call | Returns |
|---|---|
| `time.now()` | Unix time in seconds (UTC), or `0` until the device has set its clock |
| `time.ms()` | Milliseconds since the device started, for measuring intervals. It starts again from 0 after about 25 days |
| `input.held(name)` | Whether `"up"`, `"center"` or `"down"` is held right now |

### `print`

`print(...)` goes to the device's serial log, to `tools/deskhog logs <name>` and to
the web page. The device keeps the last 30 lines per plugin.

## What Lua you get

Lua 5.4 with `string`, `table`, `math`, `utf8` and `coroutine`. Left out on purpose:
`io`, `os`, `debug`, `require`, `load`, `dofile`, `string.dump` and `__gc` finalizers.
A plugin is one file of source text.

Numbers are 32-bit: integers run from about -2.1 to 2.1 billion, and fractions have
about 7 significant digits. That is plenty for a dashboard or a game. For amounts
of money, work in cents.

## Limits, and what happens when you hit them

A plugin cannot take the device down. When one fails, its card shows the error and
the line number, and every other card carries on. Fix the file and push again.

| Limit | Value |
|---|---|
| Plugin file | 64 KB |
| Images | 512 KB of pixels per connection (about 130,000 pixels), at most 240 by 240 each |
| Memory | 256 KB of Lua heap per connection |
| Time per call | 250 ms for `update`, `draw`, `button` and HTTP callbacks; 2 s for the file itself and `load` |
| Nesting | 40 levels of nested calls through `pcall`, metamethods and the like |
| Plugin cards | 12 at once, fewer if memory runs short |
| Connections | 16 |

One known gap: the time limit is checked between Lua instructions, so a single
slow built-in call (a pathological `string.find` pattern on a long string, say) can
overrun it.

## The device's API

Everything the web page and the tools do goes through plain HTTP on the device, so
you can script it.

```
GET    /api/plugins                     plugin files, storage use and headroom
GET    /api/plugins/<name>              the source
PUT    /api/plugins/<name>              install or replace (body: the Lua source). A plugin that no card shows yet
                                        gets a first card, so you see it run; add ?card=no to only install it
DELETE /api/plugins/<name>              remove the plugin with its connections and cards
GET    /api/plugins/<name>/status       how it is running, last error, print() output

GET    /api/state                       plugins, connections, the card stack and their live state
POST   /api/directory                   {source}: where plugins are offered from; "" for the default
POST   /api/cards/add                   {plugin, kind, name, settings, connection}
POST   /api/cards/update?key=<c>:<n>    {name, settings}
POST   /api/cards/layout                ["PLUGIN|weather:1", ...] in the new order; cards left out are removed
GET    /api/cards/screen?key=<c>:<n>    the card's pixels, raw RGB565 (X-Width, X-Height, X-Frames say what it is).
                                        &packed=1: as runs of (count, pixel), 3 bytes each, if X-Packed says "runs".
                                        &since=<X-Frames you have>: 204 and no picture if the card was not painted since
POST   /api/cards/press?key=..&button=  up, center or down
POST   /api/cards/show?key=..           bring the card to the front
POST   /api/connections/add             {plugin, name, settings}
POST   /api/connections/update?id=<c>   {name, settings}
POST   /api/connections/remove?id=<c>
```

```sh
curl -T counter.lua http://deskhog.local/api/plugins/counter
```

When you send plugin source yourself, do not label it `text/plain` or as form data:
the device's web server would try to read it as a form. `curl -T` sends no content
type, which works; `application/octet-stream` does too.

Like the rest of the web page, this API has no login: anyone on the same network can
install plugins. Run it on a network you trust.

## Publishing a plugin

A plugin directory is a folder of plugins with an `index.json` that lists them, in
a git repository. This repository's `plugins/` folder is one, and it is the one a
device uses unless told otherwise. To add yours to it:

1. Put `yourplugin.lua` in `plugins/`. Its `plugin = { ... }` table needs `name`,
   `description` and `version`, each written as plain text in quotes.
2. Add a picture of its card: `plugins/screenshots/yourplugin.png`. `tools/screenshots
   yourplugin` makes one with the simulator, at twice the card's size. If the card
   needs settings, button presses or an account to show something, give it a recipe
   in that script (the others are examples). A screenshot of the real thing works
   too: `tools/deskhog screenshot yourplugin plugins/screenshots/yourplugin.png`.
3. Run `tools/deskhog index`. It rewrites `plugins/index.json` and the list of
   plugins in the main README.
4. Open a pull request with these files. The checks run the plugin through the
   runtime's tests (it has to load) and make sure the index and the list match.

Once it is merged, it shows up on every device's web page, on the Plugins page and
among the choices of Add card, with an Install button, and with Update when the
published file differs from the one on the device. (GitHub serves the files from a
cache, so that can take a few minutes.) Nothing is fetched by the device itself: the web page
(or `tools/deskhog install`) downloads the file and hands it to the device.

Your own directory works the same way. Any GitHub repository with a `plugins`
folder and an `index.json` in it will do: on the web page choose Change next to the
directory's name and enter `owner/repo` (or `owner/repo@branch`, or the full address
of an `index.json` anywhere). From the command line:

```sh
tools/deskhog directory                      # what the device's directory offers
tools/deskhog directory --from you/your-repo
tools/deskhog install clock                  # install or update
tools/deskhog index path/to/folder           # write index.json for a folder of plugins
```

Plugins from a directory run in the same sandbox as any other, but they can reach
the internet and they see the settings you give them, API keys included. Only use
directories you trust, and read the code when in doubt: every entry links to it.

`index.json` looks like this. `hash` is a fingerprint of the file that the device
also reports for its installed copy, which is how "Installed", "Update" and "Changed
on this device" are told apart.

```json
{
  "format": 1,
  "plugins": [
    { "name": "clock", "title": "Clock", "description": "…", "author": "…", "version": "1.0",
      "file": "clock.lua", "size": 2743, "hash": "657fd6bc", "sha256": "…",
      "screenshot": "screenshots/clock.png" }
  ]
}
```

`screenshot` is optional: a PNG inside the directory, which the web page shows next
to the plugin. Like the plugin files, it is fetched by the browser, not by the device.

A firmware build can also carry plugins inside it, for devices that have to work
without any of this: list their names in `plugins/bundled.txt`. By default it
carries none.

## Porting a card from the original firmware

Cards for PostHog's firmware are C++ classes built on LVGL, compiled into the
firmware; the games and apps in that repository's pull requests are of that kind.
This firmware has no place to register such a card. As a plugin, the same card is
usually shorter. What goes where:

| In the C++ card | In a plugin |
|---|---|
| `handleButtonPress(button_index)`, returning `true` when used | `button(name)`, returning `true` when used |
| `update()`, called at the screen's rate | `update(dt)`, with the seconds since the last call. Move things by `speed * dt` |
| LVGL objects made once and changed later | `draw()` paints the whole card each time, with `ui` or `gfx` |
| Sprites as C arrays (`png2c.py`) | `gfx.image`, written by `tools/deskhog image *.png` |
| `Preferences`, or a config value from the web page | `store` for what the card saves, `config` for what the person sets |
| `HTTPClient` and ArduinoJson, on the network task | `http.get` with a callback; `res.json` is already decoded |
| Events through `EventQueue` to reach the UI task | Nothing: a plugin only ever runs on the UI task |

[flappy.lua](flappy.lua), [paddle.lua](paddle.lua), [friend.lua](friend.lua) and
[question.lua](question.lua) were ported this way, and
[posthog.lua](posthog.lua) replaces the insight cards with their client and parser.

## Testing without a device

- `tools/sim/run.sh <file>` runs a plugin through the same Lua runtime, drawing code,
  graphics library and fonts as the device and saves a PNG. `http` calls go out for
  real through `curl`. Options: `--seconds`, `--press center@0.5,up@2`,
  `--hold center@1-2.5` (a button held down for a while), `--set key=value`,
  `--store key=value` (something the plugin saved earlier, such as `best=42`),
  `--card <kind>`, `--reply <part of URL>=<file>` (answer matching requests from a
  file, for services that need a login), `--no-http`, `--scale`, `--out`.
- `tools/screenshots` makes the pictures in `plugins/screenshots/` that way.
- `test/native/run.sh` runs the runtime's own tests, including each example here.
- `test/portal/run.sh` tests the web page against a pretend device.
- `node test/portal/serve.js` serves the web page from `html/` on your computer, against
  the same pretend device, or against your real one with `--device <address>`. For
  working on the page without flashing.
