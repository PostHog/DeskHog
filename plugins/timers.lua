-- Timers: three kitchen or desk timers, one press away. Tea, eggs, pasta,
-- or whatever you set them to.
--
-- Everything happens on the centre button, so up and down keep switching
-- cards and you never get stuck here:
--
--   Waiting:  tap starts the highlighted timer, hold picks the next one.
--   Running:  tap pauses, hold adds a minute (keep holding for more).
--   Paused:   tap carries on, hold stops.
--   Done:     any button stops the blinking.
--
-- The timer keeps going while another card is on screen, and when it runs out
-- the LED blinks for up to two minutes. A plugin cannot bring its card to the
-- front, so the LED is the alarm. The end time is saved, so a running timer
-- picks up where it was after a restart (once the device knows the time).

plugin = {
  name = "Timers",
  description = "Three kitchen timers, one press away. The LED blinks when time is up",
  author = "DeskHog",
  version = "1.0",
  fps = 10,
  refresh = 1, -- load() every second, also while another card is showing
}

config = {
  minutes1 = { label = "First timer, minutes", default = 3, order = 1 },
  name1 = { label = "First timer, name", default = "Tea", order = 2 },
  minutes2 = { label = "Second timer, minutes", default = 5, order = 3 },
  name2 = { label = "Second timer, name", default = "Eggs", order = 4 },
  minutes3 = { label = "Third timer, minutes", default = 10, order = 5 },
  name3 = { label = "Third timer, name", default = "Pasta", order = 6 },
}

local W, H = gfx.width, gfx.height
local HOLD = 600          -- ms of holding centre that count as a hold
local ALARM = 120000      -- ms the LED blinks for at most
local BLINK = { "#ff4000", "#000000", "#ffc000", "#000000" }

local phase = "idle"      -- "idle", "running", "paused" or "done"
local selected = 1        -- which of the three is highlighted
local current = 1         -- which one is running
local endsAt = 0          -- time.ms() at which it runs out
local length = 0          -- how long it runs in total, in ms, minutes added included
local pausedLeft          -- ms left, while paused
local doneAt = 0          -- time.ms() at which it ran out
local blinkStep           -- which colour of BLINK the LED shows
local blinkAt = 0         -- time.ms() at which it last changed
local note, noteUntil     -- a short message, and until when it shows (nil: for good)
local restored = false    -- whether a saved timer has been looked at
local loaded = false
local press               -- { at, holds, seen }: centre is down and not yet dealt with
local shown

local function preset(i)
  local minutes = tonumber(config["minutes" .. i]) or 1
  local ms = math.max(1000, math.floor(minutes * 60000))
  local name = tostring(config["name" .. i] or "")
  if name:match("^%s*$") then name = nil end
  return ms, name
end

local function clock(ms)
  local seconds = (ms + 999) // 1000
  if seconds >= 6000 then -- from 100 minutes on, with hours
    return string.format("%d:%02d:%02d", seconds // 3600, seconds % 3600 // 60, seconds % 60)
  end
  return string.format("%d:%02d", seconds // 60, seconds % 60)
end

local function nameOf(i)
  local _, name = preset(i)
  return name or "Timer " .. i
end

local function left()
  if pausedLeft then return pausedLeft end
  return math.max(0, endsAt - time.ms())
end

local function say(text, seconds)
  note = text
  noteUntil = seconds and time.ms() + seconds * 1000 or nil
end

-- What survives a restart: which timer, when it ends (Unix time) or how much was left.
local function save()
  if phase == "running" and time.now() > 0 then
    store.set("timer", { i = current, ends = time.now() + (endsAt - time.ms()) // 1000, len = length })
  elseif phase == "paused" then
    store.set("timer", { i = current, left = pausedLeft, len = length })
  else
    store.set("timer", false)
  end
end

local function start(i)
  current = i
  length = preset(i)
  endsAt = time.ms() + length
  pausedLeft = nil
  phase = "running"
  note = nil
  save()
end

local function stop(message)
  phase, pausedLeft = "idle", nil
  if blinkStep then led.off() end
  blinkStep = nil
  if message then say(message, 4) else note = nil end
  save()
end

local function finish(ago)
  phase = "done"
  doneAt = time.ms() - (ago or 0)
  pausedLeft = nil
  blinkStep = nil
  store.set("timer", false)
end

-- Pick up a timer that was running before a restart. Needs the clock for a running one.
local function restore()
  if restored then return end
  local saved = store.get("timer", false)
  if type(saved) ~= "table" then
    restored = true
    return
  end
  current = math.max(1, math.min(3, tonumber(saved.i) or 1))
  length = tonumber(saved.len) or preset(current)
  if saved.left then
    restored = true
    phase, pausedLeft = "paused", math.floor(saved.left)
    return
  end
  local now = time.now()
  if now == 0 then return end -- try again once the clock is set
  restored = true
  local remaining = math.floor(saved.ends) - now
  if remaining > 0 then
    phase, pausedLeft = "running", nil
    endsAt = time.ms() + remaining * 1000
  elseif -remaining * 1000 < ALARM then
    finish(-remaining * 1000)
  else
    store.set("timer", false)
    say(nameOf(current) .. " ran out while I was off", 30)
  end
end

-- Runs from load() every second and from update() on every frame: the clock,
-- and the LED while the alarm is on.
local function step()
  restore()
  if noteUntil and time.ms() >= noteUntil then note, noteUntil = nil, nil end
  if phase == "running" and left() <= 0 then finish() end
  if phase == "done" then
    local since = time.ms() - doneAt
    if since >= ALARM then
      stop()
      say(nameOf(current) .. " is done", 60)
      return
    end
    -- The next colour every quarter second on screen. Away, load() only runs
    -- once a second, so it goes one colour a second: still a blink.
    if not blinkStep or time.ms() - blinkAt >= 250 then
      blinkStep = (blinkStep or 0) % #BLINK + 1
      blinkAt = time.ms()
      led.set(BLINK[blinkStep])
    end
  end
end

-- The centre button: a tap is acted on when it is let go, a hold after HOLD ms.
local function tap()
  if phase == "idle" then
    start(selected)
  elseif phase == "running" then
    pausedLeft = left()
    phase = "paused"
    save()
  elseif phase == "paused" then
    endsAt = time.ms() + pausedLeft
    pausedLeft = nil
    phase = "running"
    save()
  end
end

local function hold()
  if phase == "idle" then
    selected = selected % 3 + 1
    store.set("selected", selected)
  elseif phase == "running" then
    endsAt = endsAt + 60000
    length = length + 60000
    say("+1 min", 1)
    save()
  elseif phase == "paused" then
    stop("Stopped")
    return false -- once is enough
  end
  return true -- keep holding to do it again
end

local function watchCenter()
  if not press then return end
  local now = time.ms()
  -- The card was away (centre and up switch cards): forget the press.
  if now - press.seen > 400 then
    press = nil
    return
  end
  press.seen = now
  if input.held("center") then
    if now - press.at >= HOLD * (press.holds + 1) then
      press.holds = press.holds + 1
      if not hold() then press = nil end
    end
  else
    if press.holds == 0 then tap() end
    press = nil
  end
end

function load()
  if not loaded then
    loaded = true
    selected = math.max(1, math.min(3, tonumber(store.get("selected", 1)) or 1))
  end
  step()
end

function update()
  step()
  watchCenter()
  local now = phase .. selected .. (left() // 1000) .. tostring(note) .. (blinkStep or "")
  if now == shown then return false end
  shown = now
end

-- A ring, lit for the part that is left, going round clockwise from the top.
local function ring(x, y, radius, part, lit)
  local steps = 72
  local on = math.ceil(part * steps - 0.001)
  local function at(i)
    local angle = -math.pi / 2 + i * 2 * math.pi / steps
    return x + radius * math.cos(angle), y + radius * math.sin(angle)
  end
  for i = 0, steps - 1 do
    local x1, y1 = at(i)
    local x2, y2 = at(i + 1)
    gfx.line(x1, y1, x2, y2, i < on and lit or "#333333", 7)
  end
end

-- The text, shortened with "..." if it is wider than width.
local function fit(text, width, font)
  if gfx.textWidth(text, font) <= width then return text end
  while utf8.len(text) and utf8.len(text) > 1 and gfx.textWidth(text .. "...", font) > width do
    text = text:sub(1, utf8.offset(text, -1) - 1)
  end
  return text .. "..."
end

local function drawIdle()
  gfx.text(8, 5, "Timers", "gray")
  for i = 1, 3 do
    local ms, name = preset(i)
    local y = 25 + (i - 1) * 30
    local chosen = i == selected
    if chosen then gfx.rect(4, y, W - 8, 27, "#3a2a10", 6) end
    local color = chosen and "orange" or "white"
    gfx.text(14, y + 5, fit(nameOf(i), W - 100, "medium"), color, "medium")
    gfx.text(W - 14, y + 5, clock(ms), color, "medium", "right")
  end
  if note then
    gfx.text(W / 2, H - 19, fit(note, W - 8), "yellow", "small", "center")
  else
    gfx.text(W / 2, H - 19, "Tap centre: start  -  hold: next", "gray", "small", "center")
  end
end

local function drawTimer()
  local cx, cy, r = 58, 68, 50
  local name = nameOf(current)
  local done = phase == "done"
  local color = phase == "paused" and "gray" or "orange"

  local flash = done and (blinkStep or 1) % 2 == 1 -- in step with the LED
  ring(cx, cy, r, done and 1 or length > 0 and left() / length or 0, color)
  if flash then gfx.circle(cx, cy, r + 3, "orange") end
  -- The name inside the ring, on up to three lines.
  local font, lines = "medium", gfx.wrap(name, 80, "medium")
  if #lines > 2 then font, lines = "small", gfx.wrap(name, 80, "small") end
  local tall = gfx.textHeight(font)
  local top = cy - math.min(#lines, 3) * tall / 2
  for i = 1, math.min(#lines, 3) do
    local line = i == 3 and #lines > 3 and fit(lines[3] .. " " .. lines[4], 80, font) or fit(lines[i], 80, font)
    gfx.text(cx, top + (i - 1) * tall, line, flash and "black" or "white", font, "center")
  end

  local x = 172
  if done then
    gfx.text(x, 26, "Done!", "orange", "large", "center")
    gfx.text(x, 94, "Any button", "gray", "small", "center")
    gfx.text(x, 111, "stops the light", "gray", "small", "center")
    return
  end
  local text = clock(left())
  local big = gfx.textWidth(text, "large") <= 116 and "large" or "loud"
  gfx.text(x, 22, text, phase == "paused" and "gray" or "white", big, "center")
  if note then
    gfx.text(x, 66, note, "yellow", "medium", "center")
  elseif phase == "paused" then
    gfx.text(x, 66, "Paused", "gray", "medium", "center")
  end
  if phase == "paused" then
    gfx.text(x, 94, "Tap: carry on", "gray", "small", "center")
    gfx.text(x, 111, "Hold: stop", "gray", "small", "center")
  else
    gfx.text(x, 94, "Tap: pause", "gray", "small", "center")
    gfx.text(x, 111, "Hold: +1 min", "gray", "small", "center")
  end
end

function draw()
  if phase == "idle" then drawIdle() else drawTimer() end
end

function button(name)
  if phase == "done" then
    stop()
    return true -- any button stops the alarm, and only that
  end
  if name == "center" then
    local now = time.ms()
    press = { at = now, holds = 0, seen = now }
    return true
  end
  return false -- up and down switch cards; a running timer keeps going
end
