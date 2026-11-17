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
| [clock.lua](clock.lua) | Local time with `time.date()` and `time.format()`, repainting only when something changed |
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
| [worldclock.lua](worldclock.lua) | The time in other cities: a `type = "timezone"` setting, and a zone handed to `time.date()` |
| [reminders.lua](reminders.lua) | Daily reminders: `notify()` to come to the front, a timer that runs off screen, `time.make()` for the countdown |
| [doorbell.lua](doorbell.lua) | Messages pushed to the plugin with `message()`, and `gfx.color()` to check a colour a stranger sent |
| [ciwatch.lua](ciwatch.lua) | GitHub Actions runs as a dense list in the tiny font; polling politely with `res.headers`, an ETag and 304 |
| [nowplaying.lua](nowplaying.lua) | What you are listening to, with the album cover: a picture from the network with `gfx.fetchImage` |
| [gauge.lua](gauge.lua) | A dial for any number in a JSON answer: `gfx.arc`, and an `only` filter built from a setting |
| [wifi.lua](wifi.lua) | A signal meter to carry around: `net.rssi()` and its friends, sampled by a timer |
| [charge.lua](charge.lua) | A game on one button that is held: the `"press"`, `"long"` and `"release"` events |
| [homey.lua](homey.lua) | Homey Pro devices and flows: settings with `choices` that follow the form, `only` with `"*"`, `http.put` |

The rest of the folder is there to be used rather than studied: air quality and pollen,
bike share stations, certificate and domain expiry, countdown, crypto prices, daylight and
the moon, dad jokes, a deploy button, habits, Hacker News, on this day, the planes
overhead, theme park queues, rain, a reaction timer, GitHub releases, Simon, the space
station and rocket launches, a starfield, service status pages, a tally counter, kitchen
timers, an uptime check and a word of the day.

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
tools/deskhog config weather:2 front=no   # this card may not come to the front by itself (see notify)
tools/deskhog send doorbell "Pizza's here" # push a message to a plugin: text, JSON or @file
```

## How a plugin runs

The file runs once from top to bottom. After that the device calls the functions
you defined. All of them are optional.

| Function | Called |
|---|---|
| `load()` | Once at start, again every `plugin.refresh` seconds, and when WiFi connects |
| `update(dt)` | Every frame while the card is on screen. `dt` is the seconds since the last call. Return `false` when nothing you draw has changed, and the card is not repainted |
| `draw()` | When the card needs repainting: after any of the other functions ran |
| `button(name, event)` | On a press. `name` is `"up"`, `"center"` or `"down"`. See [Buttons](#buttons-and-moving-between-cards) for `event` |
| `message(msg)` | When something on your network sends the plugin a message. See [Messages](#messages-pushed-to-your-plugin) |

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
  city = { label = "City", default = "Antwerp" },
  days = 3,           -- short form: just the default
}
```

After the file has run, each `config` entry is replaced by its value: the saved
setting if there is one, otherwise the default. So in `load()` and everywhere else,
`config.city` is simply `"Antwerp"`. People change settings on the device's web page;
the plugin restarts with the new values. A setting keeps the type of its default
(text, number or true/false).

Mind the "after the file has run". While the file itself runs, top to bottom,
`config.city` is still the description you wrote: a table, or the bare default. So
use settings inside `load()` and the other functions, not at the top of the file:

```lua
local url = "https://example.com/" .. config.city      -- wrong: the file is still running

local url
function load()
  url = "https://example.com/" .. config.city          -- right
end
```

The first one stops the plugin with "attempt to concatenate a table value", and the
device adds a line that says what this paragraph says.

A setting can say more about itself:

```lua
config = {
  key  = { label = "API key", secret = true, order = 1 },           -- write-only on the web page
  unit = { label = "Unit", default = "C", options = { "C", "F" } }, -- a dropdown
  zone = { label = "Time zone", type = "timezone" },                -- a list of places
}
```

A `type = "timezone"` setting shows the web page's list of places ("Europe, Brussels").
Your plugin gets the choice as text to hand to `time.date` or `time.format`. Left
alone it is `""`, which those two read as the device's own zone.

Lua tables have no order, so the web page shows fields by their `order` (lowest first)
and the rest by name.

### Choices your plugin fills in

`options` is a fixed list. When the list has to come from somewhere, say the devices
in someone's home, name a function instead:

```lua
config = {
  url    = { label = "Address", order = 1 },
  device = { label = "Device", choices = "listDevices", order = 2 },
}

function listDevices(done, ctx)
  http.get(config.url .. "/api/devices", function(res)
    if not res.ok then return done(nil, "Could not reach it (" .. res.status .. ")") end
    local list = {}
    for _, device in ipairs(res.json) do
      list[#list + 1] = { value = device.id, label = device.name }
    end
    done(list)
  end)
end
```

The web page shows the setting as a dropdown with a Refresh button. It calls your
function when someone opens the form, and again on Refresh.

| | |
|---|---|
| `choices = "name"` | The name of a global function. It works for shared settings and for a card kind's settings |
| `done(list)` | Answers with the list. Each entry is `{ value = ..., label = ... }`. `label` is what people see; leave it out and they see the value. A plain string or number works too |
| `done(nil, "why")` | Answers that there is no list, and why |
| `ctx.settings` | The form as it is filled in right now, saved or not: `ctx.settings.advanced`. For a list that depends on another setting |
| `ctx.kind` | The card kind whose setting it is, or `nil` for one of the plugin's shared settings |

Your plugin gets the `value` as the setting, as text. Call `done` once, straight away
or after an `http` call. If it has not been called within 10 seconds the web page gives
up. Then, and when you answer with `nil`, the setting becomes a plain text box, so
people can still type a value. A list holds at most 100 entries of 200 characters
each, and 8 KB in total; what comes after that is left out.

`ctx.settings` has every setting of the form that asks: a card kind's settings for a
card, the shared ones for a connection. They have their types, as in `config`. A value
is what the form holds; where the page sent none it is the default (for shared
settings: the value in use). What someone typed into a `secret` field is never sent,
so for those you get the saved one. The list is fetched when the form opens and on
Refresh, not on every keystroke: someone who changes the other setting presses Refresh.
[homey.lua](homey.lua) lists flows or advanced flows that way, going by a tick box.

A setting whose `default` is `""` is optional, and the dropdown says so: it starts
with an empty entry, "—", which saves `""`. So there is no need to add a "None" to
your list.

The function runs in the connection's own program, with its settings. So the list is
there when someone edits a card or a connection, and when they add a card to a
connection that exists. For a connection that is still being made there is nobody to
ask yet: the page shows a text box then. A plugin without card kinds gets its
connection together with its card, so its list shows once the card is added. Keep
that in mind: let people type a value, or ship a sensible `default`.

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
      city = { label = "City", default = "Antwerp" },
    },
    load = function(card) ... end,           -- at start, on refresh, when WiFi connects
    update = function(card, dt) ... end,     -- every frame while this card is on screen
    draw = function(card) ... end,
    button = function(card, name) ... end,
    message = function(card, msg) ... end,   -- a message sent to this card
  },
}
```

Each function gets `card`, a table that belongs to that one card:

| Field | Holds |
|---|---|
| `card.config` | This card's settings, resolved like `config` |
| `card.id`, `card.kind` | Which card this is |
| `card.title` | Set it to tell the web page what to call the card, e.g. the city it found |
| `card.notify(text, opts)` | Ask for attention for this card. See [notify](#notify-asking-for-attention) |
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

Give `button` a second parameter and it also hears what happens after the press:

```lua
function button(name, event)       -- for a kind of card: button(card, name, event)
  if name ~= "center" then return end
  if event == "press" then charging = true end
  if event == "long" then charge = "full" end
  if event == "release" then charging = false end
  return true
end
```

| `event` | When |
|---|---|
| `"press"` | The button goes down. What you return decides whether the cards switch, as above |
| `"long"` | It has been held for 0.6 seconds. Once per hold, and only while the card is on screen |
| `"release"` | It is let go. Every press gets one, also when the card has left the screen by then |

What you return for `"long"` and `"release"` changes nothing: only a press can switch
cards. A `button` function with one parameter is called for presses only, exactly as
before there were events. A press from the web page or `tools/deskhog press` is
released at once.

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
| `gfx.circle(x, y, radius, [color], [width])` | Filled circle around `x, y`. With a `width`: a ring that thick, drawn inwards from the radius |
| `gfx.arc(x, y, radius, from, to, [color], [width])` | Part of a ring, from one angle to the other, in degrees. 0 is straight up and angles grow clockwise, as on a clock. Without a `width`: a filled slice |
| `gfx.poly(points, [color])` | A filled shape. `points` is `{x1, y1, x2, y2, ...}`, 3 to 64 corners |
| `gfx.triangle(x1, y1, x2, y2, x3, y3, [color])` | A filled triangle |
| `gfx.pixel(x, y, [color])` | One pixel |
| `gfx.text(x, y, text, [color], [font], [align])` | Text, up to 255 bytes per call. `y` is its top edge |
| `gfx.textWidth(text, [font])` | Width in pixels. Works outside `draw()` too |
| `gfx.textHeight([font])` | Line height in pixels |
| `gfx.wrap(text, width, [font])` | Not a drawing call: returns the text as a list of lines that each fit `width`. Works outside `draw()` too |
| `gfx.clip(x, y, w, h)` | Until `gfx.clip()` without arguments, only draw inside this rectangle. For text that scrolls |
| `gfx.draw(image, x, y, [scale])` | An image made with `gfx.image` or `gfx.fetchImage`, optionally 2 to 8 times its size |
| `gfx.color(value)` | Not a drawing call: returns the colour as a number, or `nil` if `value` is not a colour. Works outside `draw()` too |

**Colors** are `"#rrggbb"`, `"#rgb"`, a number like `0xFF8800`, or a name: `white`,
`black`, `red`, `green`, `blue`, `yellow`, `orange`, `purple`, `gray`. White is the
default. Anything else stops the plugin with an error, which is what you want for a
typo. For a colour that comes from outside (a message, a setting someone typed), ask
first: `gfx.color(text)` gives `nil` for what is not a colour, and never an error.

**Fonts:**

| Font | Size | A line is | |
|---|---|---|---|
| `"small"` | 15px | 18px | The default |
| `"medium"` | 16px, semibold | 21px | |
| `"large"` | 36px, semibold | 46px | |
| `"loud"` | 20px | 23px | A chunky display face, basic Latin only |
| `"tiny"` | 10px | 14px | For lists with many lines: nine fit on a card |

"A line is" is what `gfx.textHeight` returns: the room a line of text takes, which is
more than the size of its letters. All but `"loud"` cover Western European characters
such as `é` and `°`.

**Angles** go the way a clock does. `gfx.arc(x, y, r, 0, 90)` is the quarter from
12 o'clock to 3 o'clock; `gfx.arc(x, y, r, 350, 10)` is the short piece over the top.
A dial that is 62% full: `gfx.arc(x, y, 50, 225, 225 + 270 * 0.62, "orange", 8)`.

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

**Pictures from the network.** `gfx.fetchImage` gets a PNG or JPEG, such as an album
cover, and hands you an image to draw:

```lua
local cover

function load()
  gfx.fetchImage("https://example.com/cover.jpg", { width = 100, height = 100 }, function(image, err)
    if cover then gfx.release(cover) end   -- give the old one's memory back
    cover = image                           -- nil if it failed; err says why
  end)
end

function draw()
  if cover then gfx.draw(cover, 8, 17) end
end
```

`width` and `height` are the box it has to fit in: the picture is scaled down to
fit, keeping its shape, and never up. Without them the box is 240 by 240, the most
an image can be. `image.width` and `image.height` say what you got. The options also
take `headers`, `timeout` and `insecure`, as `http.get` does.

Pictures count towards the same 512 KB as other images: a 100 by 100 picture is 40 KB.
`gfx.release(image)` forgets an image you no longer draw. Do that before fetching the
next one, or the callback will tell you there is no room.

What it can read: JPEG up to 4 megapixels (not the "progressive" kind), PNG up to
about 300 by 300 pixels (not interlaced), files up to 256 KB. Anything else comes back
as `nil` and a reason.

## Talking to the world

### `http`

```lua
http.get(url, function(res) ... end)
http.get(url, { headers = { Authorization = "Bearer ..." }, timeout = 5 }, function(res) ... end)
http.post(url, body, function(res) ... end)   -- options go between body and callback
http.put(url, body, function(res) ... end)    -- http.patch is the same
http.delete(url, function(res) ... end)       -- no body
http.request({ method = "HEAD", url = url }, function(res) ... end)
```

Requests run in the background and your function is called with the result, after
which the card is repainted. `body` is text, or a table that is sent as JSON.

`http.request` takes everything in one table: `method` (`GET`, `POST`, `PUT`, `PATCH`,
`DELETE` or `HEAD`; `GET` if you leave it out), `url`, `body`, and the options
`headers`, `timeout`, `only` and `insecure`. The other calls are shorthand for it.

| Field | Meaning |
|---|---|
| `res.ok` | `true` for a 2xx status |
| `res.status` | The HTTP status, or `0` if no answer came |
| `res.body` | The response as text |
| `res.json` | The response decoded, when it is JSON; otherwise `nil` |
| `res.error` | Why it failed, when no answer came (for example `"no WiFi"`) |
| `res.headers` | The answer's headers, with their names in lower case: `res.headers.etag`, `res.headers["x-ratelimit-remaining"]`. About 2 KB of them are kept |

Limits: twelve requests in flight per connection, responses up to 48 KB, a timeout of 10
seconds unless you set one (1 to 30).

**HTTPS is checked.** For an `https://` address the device makes sure the server's
certificate is signed by one of the well-known authorities and belongs to that host,
as a browser does. If it is not, the request fails with `res.status` 0 and a
`res.error`. Firmware up to 0.3.1 did not check: it accepted any certificate.

When the certificate is the problem, `res.error` says which way: `"certificate not
trusted"` (no well-known authority vouches for it) or `"certificate is for another
host"`. Other requests that never got an answer say `"could not resolve host"`,
`"connection refused"`, `"connection timed out"` or `"no WiFi"`. One thing a browser
checks and the device does not is the date: a certificate that has expired still passes.

**Redirects are followed**, five in a row at most. A `GET` or `HEAD` follows 301, 302,
303, 307 and 308; any other method follows 302 and 303 only, as a `GET` without its
body. `Location` may be a whole address or only a path. http may be sent on to https,
but https never back to http, and `Authorization` and `Cookie` headers are not sent
along to another host. A redirect that is not followed is your answer as it is:
`res.status` 3xx, with `res.headers.location`. The timeout counts for each address
on the way, not for all of them together.

A box on your own network with a certificate it made itself (a NAS, a router, some
Home Assistant setups) does not pass that check. For those, say so per request:

```lua
http.get("https://nas.local/api/status", { insecure = true }, function(res) ... end)
```

The traffic is still encrypted, but nothing proves who is answering. Do not use it
for addresses on the internet.

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

Some APIs answer with an object whose keys are ids, not names: `{ "a1f3": {...},
"9c2e": {...} }`. The key `"*"` stands for every key you did not name:

```lua
only = { ["*"] = { name = true } }   -- the name of each, whatever its key
```

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

### `net`

| Call | Returns |
|---|---|
| `net.online()` | `true` while the device is on WiFi and has an address |
| `net.rssi()` | Signal strength in dBm: about -50 is strong, -80 is weak. `nil` while offline |
| `net.ssid()` | The name of the network, or `nil` while offline |
| `net.ip()` | The device's address as text, or `nil` while offline |

### `time` and `input`

| Call | Returns |
|---|---|
| `time.now()` | Unix time in seconds (UTC), or `0` until the device has set its clock |
| `time.date([t], [zone])` | That moment as people write it: a table, see below |
| `time.format(format, [t], [zone])` | The same as text: `time.format("%H:%M")` is `"20:49"` |
| `time.offset([t], [zone])` | How many seconds the zone is ahead of UTC at that moment |
| `time.make(fields, [zone])` | The other way round from `time.date`: the Unix time at which a clock in the zone shows these fields |
| `time.zone()` | The device's time zone, as a POSIX string such as `"CET-1CEST,M3.5.0,M10.5.0/3"` |
| `time.ms()` | Milliseconds since the device started, for measuring intervals. It starts again from 0 after about 25 days |
| `input.held(name)` | Whether `"up"`, `"center"` or `"down"` is held right now |

**Local time.** The device has one time zone, picked on its web page (Device, Time
zone); until someone picks one it is UTC. Summer time is part of the zone, so nobody
has to change anything when the clocks change. `t` is a time like `time.now()` gives,
and is now if you leave it out. `zone` is the device's zone if you leave it out or
pass `""`. To use another one, pass a POSIX string: from a setting with
`type = "timezone"`, or written out, as in `time.format("%H:%M", nil, "JST-9")`.

`time.date()` returns `{ year, month, day, hour, min, sec, wday, yday, isdst, offset, zone }`.
`wday` is 1 for Sunday to 7 for Saturday, `yday` is 1 on 1 January, `isdst` is `true`
during summer time, `offset` is in seconds east of UTC and `zone` is the short name in
use, such as `"CEST"`.

`time.format` knows these, in English:

| Code | Gives | Code | Gives |
|---|---|---|---|
| `%H` `%M` `%S` | 20, 49, 05 | `%I` `%p` | 08, PM |
| `%Y` `%y` | 2025, 25 | `%m` `%d` `%e` | 11, 05, " 5" |
| `%A` `%a` | Saturday, Sat | `%B` `%b` | November, Nov |
| `%j` | Day of the year, 001 to 366 | `%Z` `%z` | CET, +0100 |
| `%F` `%T` `%R` | 2025-11-15, 20:49:05, 20:49 | `%%` | % |

A `-` drops the padding: `%-d` is `5`, `%-I` is `8`.

`time.make{ year = 2026, month = 11, day = 17, hour = 15, min = 0 }` is the moment a
clock here says 15:00 on that day. `year`, `month` and `day` have to be there; `hour`,
`min` and `sec` are 0 if you leave them out. A table from `time.date()` goes straight
back in. Fields past their range carry over, so "tomorrow" is `day + 1`, also on the
31st, and the count stays right on the days the clocks change:

```lua
local today = time.date()
today.hour, today.min, today.sec = 15, 0, 0
local wait = time.make(today) - time.now()   -- seconds until 15:00 here, negative once it has been
```

Twice a year the clock does something odd. Where it jumps ahead, a time is skipped:
02:30 on that night does not exist, and `time.make` reads it as that long after the
jump, 03:30. Where it falls back, a time happens twice: you get the first of the two,
or the second with `isdst = false` in the table. (A table from `time.date()` has
`isdst`, so it comes back as the moment it was.)

Until the device has set its clock there is no "now": `time.date()` and
`time.format(...)` without a `t` return `nil`. Check `time.now() == 0` first, as
[clock.lua](clock.lua) does.

### `timer`

```lua
timer.after(5, function() status = "done" end)             -- once, in five seconds
local blink = timer.every(0.5, function() on = not on end)  -- again and again
blink:cancel()                                              -- until you stop it
```

Timers run whether the card is on screen or not, which `update(dt)` does not. After a
timer's function ran the card is repainted, unless it returns `false`. A card's
function reaches its card the usual Lua way, by being made inside `load(card)`:

```lua
load = function(card)
  card.beat = card.beat or timer.every(60, function() card.minutes = (card.minutes or 0) + 1 end)
end
```

A plugin has at most 16 timers at a time. `timer.every` runs at most ten times a
second, and once a second while none of the plugin's cards is on screen. Timers are
not saved: after a restart, start them again.

`math.random` starts from a different number on every boot, so there is no need to
seed it. (In the simulator, `--seed` makes it the same on every run.)

### `notify`: asking for attention

A card that is not on screen cannot show anything. `notify` asks the device to do
something about that:

```lua
notify("Tea is ready")                    -- the card comes to the front, with this text over it
notify("Build failed", { led = "red" })   -- and the LED flashes red
notify()                                  -- just come to the front
card.notify("Rain in 10 minutes")         -- in a plugin with card kinds: say which card
```

The text shows as a bar over the top of the card for 4 seconds. The device draws
it, so it looks the same for every plugin. It fits two lines; longer text ends in
dots.

Your plugin only asks. The device decides, by rules a plugin cannot change:

| Rule | What it means |
|---|---|
| Each card has a setting, "May come to the front" | People switch it off on the web page, per card. It is on unless they do |
| Nobody is interrupted mid-game | While the card on screen has used a button press in the last 5 seconds, or a button is held, yours waits. It comes up after 5 quiet seconds |
| Once per 30 seconds | A card comes to the front at most that often. Call `notify` more and the calls become one, with the latest text |
| A card that may not come to the front still gets heard | The LED flashes, and the text waits. It shows when someone next arrives at the card |

The LED flashes three times: in the `led` colour if you gave one, otherwise in white,
and then only when the card had to wait. After that it goes back to normal, also when
you had set it with `led.set`. If your card is already on screen it just gets the
bar. A press that your card uses takes the bar away.

In a plugin with card kinds, use `card.notify`: plain `notify` cannot know which card
is meant, and stops the plugin with an error saying so. Timers and `http` callbacks
reach their card through a closure, as [timer](#timer) shows.

### Messages pushed to your plugin

Polling is not the only way to learn something. Anything on your network can tell a
plugin right away:

```sh
tools/deskhog send doorbell "Pizza's here"
curl -H 'Content-Type: text/plain' -d "Pizza's here" 'http://deskhog.local/api/cards/message?key=doorbell:1'
```

```lua
function message(msg)                 -- for a kind of card: message = function(card, msg)
  last = msg.json and msg.json.text or msg.body
  notify(last, { led = "orange" })
end
```

| Field | Holds |
|---|---|
| `msg.body` | What was sent, as text |
| `msg.json` | The same decoded, if it was a JSON object or list. Otherwise `nil` |

After `message` ran, the plugin's cards are repainted.

| Sent to | Calls |
|---|---|
| `POST /api/cards/message?key=<connection>:<card>` | The card kind's `message(card, msg)`. In a plugin without card kinds: the global `message(msg)` |
| `POST /api/connections/message?id=<connection>` | The global `message(msg)`, also in a plugin with card kinds |

The body is text or JSON, 4 KB at most. Label it `text/plain` or `application/json`;
`tools/deskhog send` does. The device answers:

| Answer | Means |
|---|---|
| `200` | Taken. The plugin gets it within a moment |
| `404` | No such card or connection, or the plugin has no `message` function for it |
| `409` | The plugin is not running: it crashed, or none of its cards is on the device |
| `413` | More than 4 KB |
| `429` | 8 messages for this connection (or 16 KB for all plugins together) are still waiting. Try again in a moment |

Like the rest of the device's API there is no login: anyone on the same network can
send a message. Treat what arrives as you would treat anything from the network, and
do not let a message do something you would not let a stranger do.

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
| Time per call | 250 ms for `update`, `draw`, `button`, `message`, a `choices` function, timers and HTTP callbacks; 2 s for the file itself and `load` |
| Timers | 16 per connection |
| `notify` | A card comes to the front once per 30 seconds at most; 120 characters of text are kept |
| Messages | 4 KB each, 8 waiting per connection |
| Choices | 100 entries and 8 KB per list, 10 seconds to answer, 4 lists at a time |
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
POST   /api/timezone                    {posix, name}: the device's time zone as a POSIX string, and the place it
                                        was picked by (only shown back). "" for UTC. /api/state reports both
POST   /api/cards/add                   {plugin, kind, name, settings, connection, front}
POST   /api/cards/update?key=<c>:<n>    {name, settings, front}. front: may the card come to the front by itself
                                        when its plugin calls notify() (true unless you say false)
POST   /api/cards/layout                ["PLUGIN|weather:1", ...] in the new order; cards left out are removed
GET    /api/cards/screen?key=<c>:<n>    the card's pixels, raw RGB565 (X-Width, X-Height, X-Frames say what it is).
                                        &packed=1: as runs of (count, pixel), 3 bytes each, if X-Packed says "runs".
                                        &since=<X-Frames you have>: 204 and no picture if the card was not painted since
POST   /api/cards/press?key=..&button=  up, center or down
POST   /api/cards/show?key=..           bring the card to the front
POST   /api/cards/message?key=<c>:<n>   hand the body (text or JSON, 4 KB at most) to the card's message() function
POST   /api/connections/add             {plugin, name, settings}
POST   /api/connections/update?id=<c>   {name, settings}
POST   /api/connections/remove?id=<c>
POST   /api/connections/message?id=<c>  hand the body to the plugin's global message() function
GET    /api/connections/choices?id=<c>&field=<key>[&kind=<kind>][&settings=<JSON>]
                                        what the plugin offers for a setting with `choices`:
                                        {choices: [{value, label}]}. Give kind for a card kind's setting, and
                                        settings for what the form holds: {"advanced":"true"}, 1 KB at most.
                                        Answers 202 {pending: true} while the plugin is working on it: ask again
```

`/api/state` also lists, under `notifications`, what plugins said with `notify()` that
nobody has seen yet: `{card, text, count, age}`, with `age` in seconds. Each card in
it says whether it may come to the front (`front`).

A list of choices can take the plugin a few seconds, and plugins run on the same task
as the screen. So the device does not make the request wait: the first `GET` starts
the plugin on it and answers `202` at once, and the same `GET` again collects the
list. The web page asks a few times a second. A list is handed over once; the next
`GET` after that fetches a fresh one. "The same" includes `settings`: with other
values it is another question, and starts the plugin on another list.

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
  real through `curl`. Its log says what the plugin did: every request with its method
  and body, every `print`, `notify` and LED change.

  | Option | Does |
  |---|---|
  | `--seconds 4` | How long to run before the picture is taken (2 without it) |
  | `--press center@0.5,up@2` | Button presses, and when |
  | `--hold center@1-2.5` | A button held down for a while. For a long press give both: `--press center@1 --hold center@1-2.5` |
  | `--set key=value` | A setting, of the plugin or of the card |
  | `--card <kind>` | Which kind of card, for plugins with several (the first without it) |
  | `--store key=value` | Something the plugin saved earlier, such as `best=42` |
  | `--tz <POSIX zone>` | The device's time zone; UTC without it |
  | `--time 2026-11-17T14:05` | What the clock says when the run starts, in the `--tz` zone. Seconds may follow (`T14:05:30`), a `Z` at the end means UTC, and a plain number is Unix time. Now, without it. The clock runs with the simulation either way, so `--seconds 70` crosses a minute |
  | `--seed 7` | `math.random` gives the same numbers on every run, for a game whose picture should not change |
  | `--rssi -52,-60,-71` | WiFi signal strength: one value per second, over and over. -58 without it |
  | `--offline` | No WiFi: `net.online()` is false and every request fails with `"no WiFi"` |
  | `--reply <part of URL>=<file>` | Answer requests whose address contains that part from a file, with status 200. For services that need a login, and for pictures that stay the same |
  | `--reply-status <part>=304` | Answer them with another status. Works without `--reply` too: the body is then empty |
  | `--reply-header '<part>=ETag: "abc"'` | Give the answer a header; repeat for more |
  | `--no-http` | Answer every request that has no `--reply` with a network error |
  | `--message 1="Pizza's here"` | Push a message one second in; `1=@order.json` sends a file |
  | `--choices listDevices` | Run a `choices` function and print its list. Its `ctx.settings` is what `--set` gave |
  | `--arrive 4` | Keep the card off screen for the first four seconds, as if you were looking at another card |
  | `--no-front` | Switch its "May come to the front" setting off |
  | `--scale 3`, `--out file.png` | How much the picture is enlarged, and where it goes |

  The log says what each `notify` led to, and its bar is in the PNG if it is still up
  at the end. A reminder going off, for instance:

  ```sh
  tools/sim/run.sh plugins/reminders.lua --tz CET-1CEST,M3.5.0,M10.5.0/3 --time 2026-11-17T14:59:30 --seconds 32
  ```

  When a request fails, `res.error` is a short reason as on the device, such as
  `"could not resolve host"` or `"connection refused"`.
- `tools/screenshots` makes the pictures in `plugins/screenshots/` that way, with a
  fixed `--time` and `--seed` where a card needs one, so a picture only changes when
  its plugin does.
- `test/native/run.sh` runs the runtime's own tests, including each example here.
- `test/portal/run.sh` tests the web page against a pretend device.
- `node test/portal/serve.js` serves the web page from `html/` on your computer, against
  the same pretend device, or against your real one with `--device <address>`. For
  working on the page without flashing.
