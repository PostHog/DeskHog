-- Habits: one card per habit. Press centre on a day you did it, and press again
-- within five seconds to take it back. The card shows the last twelve weeks as
-- a grid, a column per week from Monday to Sunday, with your streak and your
-- best one. Optionally the LED glows orange in the evening until every habit
-- is ticked off.
--
-- The device knows the time in UTC only, so set how many hours you are ahead
-- of UTC, as on the clock, and change it when the clocks change: that is where
-- one day ends and the next begins. Until the device has set its clock nothing
-- can be ticked off, because it does not know which day it is. Days the device
-- was off count as not done, like any other day you did not press.
--
-- Each habit is saved under "h" and its card's number, as
-- { day, start, days, best, tail }, about 100 bytes:
--   day    the newest day in it, as days since 1970 in your time
--   days   the last 168 days, newest first, as hex: four days per digit, the
--          lowest bit the newest day
--   start  the first day tracked; older squares are drawn fainter than missed ones
--   best   the best streak among days that have since scrolled out of `days`
--   tail   how many done days in a row came just before the oldest of `days`
-- A history without `day` is taken to end today. The history moves on only
-- when a press is saved, so a clock that is briefly wrong changes nothing.

plugin = {
  name = "Habits",
  description = "Tick off a daily habit and see the last twelve weeks, your streak and your best",
  author = "DeskHog",
  version = "1.0",
  refresh = 60, -- look at the evening reminder once a minute, also while another card is showing
  fps = 2,
}

config = {
  offset = { label = "Hours ahead of UTC (for example 1, -5 or 5.5)", default = "0", order = 1 },
  remind = {
    label = "Light the LED from this hour when a habit is not done yet",
    default = "Off",
    options = { "Off", "17", "18", "19", "20", "21", "22", "23" },
    order = 2,
  },
}

local W, H = gfx.width, gfx.height
local KEEP = 168          -- days of history kept: 24 weeks
local WEEKS = 12          -- columns in the grid
local SIZE, PITCH = 9, 11 -- a day's square, and the step from one to the next
local UNDO_MS = 5000      -- how long a second press undoes the first
local COLOURS = {
  Green = "#3fb950", Blue = "#4c9aff", Orange = "#f0883e", Purple = "#a371f7", Pink = "#f778ba", Yellow = "#e3b341",
}
local MISSED, UNTRACKED = "#2d2d2d", "#161616"

local habits = {} -- card id -> { day, start, bits, best, tail }; bits is "1"/"0" per day, newest first
local lit = false -- whether the reminder has the LED

-- ------------------------------------------------------------------ the day

-- Today as days since 1970 in local time, or nil until the clock is set.
local function today()
  local utc = time.now()
  if utc == 0 then return nil end
  return (utc + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)) // 86400
end

local function localHour()
  local utc = time.now()
  if utc == 0 then return nil end
  return (utc + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)) % 86400 // 3600
end

-- ---------------------------------------------------------------- the store

local function decode(hex)
  local bits = {}
  for i = 1, #hex do
    local v = tonumber(hex:sub(i, i), 16) or 0
    for b = 0, 3 do bits[#bits + 1] = (v >> b) & 1 == 1 and "1" or "0" end
  end
  return table.concat(bits)
end

local function encode(bits)
  local hex = {}
  for i = 1, #bits, 4 do
    local v = 0
    for b = 0, 3 do
      if bits:sub(i + b, i + b) == "1" then v = v | (1 << b) end
    end
    hex[#hex + 1] = string.format("%x", v)
  end
  return table.concat(hex)
end

local function open(card)
  local saved = store.get("h" .. card.id, nil)
  local h = { bits = "", best = 0, tail = 0 }
  if type(saved) == "table" then
    h.bits = type(saved.days) == "string" and decode(saved.days):sub(1, KEEP) or ""
    h.day = tonumber(saved.day)
    h.start = tonumber(saved.start)
    h.best = tonumber(saved.best) or 0
    h.tail = tonumber(saved.tail) or 0
  end
  return h
end

local function save(card, h)
  store.set("h" .. card.id, { day = h.day, start = h.start, days = encode(h.bits), best = h.best, tail = h.tail })
end

-- --------------------------------------------------------------- the sums

-- Whether day d was done. Days outside the kept history are not.
local function done(h, d)
  if not h.day then return false end
  local i = h.day - d + 1
  return i >= 1 and h.bits:sub(i, i) == "1"
end

-- The streak as of day t: counting back from today, or from yesterday when
-- today is not ticked off yet (the streak is not broken until the day is over).
local function streak(h, t)
  local d = done(h, t) and t or t - 1
  local n = 0
  while done(h, d) do
    n = n + 1
    d = d - 1
  end
  if h.day and n > 0 and h.day - d == #h.bits then n = n + h.tail end -- it runs on past what is kept
  return n
end

local function longest(h)
  local best, run = h.best, 0
  for i = 1, #h.bits do
    if h.bits:sub(i, i) == "1" then
      run = run + 1
      if i == #h.bits then run = run + h.tail end
      best = math.max(best, run)
    else
      run = 0
    end
  end
  return best
end

-- Move the history on to day t, so that its first day is t. Only done just
-- before a press is saved.
local function anchor(h, t)
  if h.day then return end
  h.day = t
  if not h.start and #h.bits > 0 then h.start = t - #h.bits + 1 end
end

local function moveTo(h, t)
  anchor(h, t)
  if t > h.day then
    local bits = string.rep("0", math.min(t - h.day, KEEP)) .. h.bits
    if #bits > KEEP then
      h.best = longest(h) -- before the oldest days scroll out
      local gone = bits:sub(KEEP + 1)
      local run = #(gone:match("^1*"))
      h.tail = run == #gone and run + h.tail or run
      bits = bits:sub(1, KEEP)
    end
    h.bits, h.day = bits, t
  end
  h.start = math.min(h.start or t, t)
end

local function set(h, t, value)
  moveTo(h, t)
  local i = h.day - t + 1 -- not 1 when the clock went back a little
  if i > KEEP then return end
  local bits = h.bits .. string.rep("0", math.max(0, i - #h.bits))
  h.bits = bits:sub(1, i - 1) .. (value and "1" or "0") .. bits:sub(i + 1)
end

-- ----------------------------------------------------------------- the LED

local function remind()
  local from = tonumber(config.remind)
  local t, hour = today(), localHour()
  local want = false
  if from and t and hour >= from then
    for _, h in pairs(habits) do
      if not done(h, t) then want = true end
    end
  end
  if want and not lit then
    led.set("#603000") -- a dim orange glow
  elseif lit and not want then
    led.off()
  end
  lit = want
end

function load()
  remind()
end

-- ------------------------------------------------------------------- words

local function fit(text, width, font)
  if gfx.textWidth(text, font) <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "..", font) > width do
    text = text:sub(1, utf8.offset(text, -1) - 1)
  end
  return text .. ".."
end

local function tick(x, y, colour)
  gfx.line(x, y + 4, x + 3, y + 7, colour, 2)
  gfx.line(x + 3, y + 7, x + 9, y, colour, 2)
end

-- --------------------------------------------------------------- the card

local function drawGrid(card, h, t, colour)
  local x0, y0 = 8, 30
  local monday = t - (t + 3) % 7 -- 1 January 1970 was a Thursday
  local first = h.start or (h.day and h.day - #h.bits + 1) or t
  for c = 0, WEEKS - 1 do
    for r = 0, 6 do
      local d = monday - (WEEKS - 1 - c) * 7 + r
      if d <= t then
        local x, y = x0 + c * PITCH, y0 + r * PITCH
        local fill = done(h, d) and colour or (d < first and UNTRACKED or MISSED)
        gfx.rect(x, y, SIZE, SIZE, fill, 2)
        if d == t then gfx.outline(x - 2, y - 2, SIZE + 4, SIZE + 4, "white", 1) end
      end
    end
  end
end

local function draw(card)
  local h = habits[card.id]
  local name = card.config.name ~= "" and card.config.name or "Habit"
  local colour = COLOURS[card.config.colour] or COLOURS.Green
  local t = today()

  gfx.text(8, 5, fit(name, W - 92, "medium"), "white", "medium")
  if not t then
    gfx.text(8, 40, "Waiting for the time.", "gray", "small")
    gfx.text(8, 58, "Habits can be ticked off", "gray", "small")
    gfx.text(8, 76, "once the clock is set.", "gray", "small")
    return
  end

  anchor(h, t)
  local isDone = done(h, t)
  if isDone then
    tick(W - 14 - gfx.textWidth("Done", "small") - 10, 9, colour)
    gfx.text(W - 8, 5, "Done", colour, "small", "right")
  else
    gfx.text(W - 8, 5, "Not yet", "gray", "small", "right")
  end

  drawGrid(card, h, t, colour)

  local x = 150
  local now = streak(h, t)
  gfx.text(x, 28, "Streak", "gray", "small")
  gfx.text(x, 42, tostring(now), now > 0 and colour or "gray", "large")
  local after = x + gfx.textWidth(tostring(now), "large") + 4
  if after + gfx.textWidth("days", "small") <= W - 4 then -- from 100 days on, the number says enough
    gfx.text(after, 62, now == 1 and "day" or "days", "gray", "small")
  end
  local best = tostring(math.max(longest(h), now))
  gfx.text(x, 86, "Best", "gray", "small")
  local bx = x + gfx.textWidth("Best ", "small")
  gfx.text(bx, 86, best, "white", "small")
  if bx + gfx.textWidth(best .. " days", "small") <= W - 4 then
    gfx.text(bx + gfx.textWidth(best .. " ", "small"), 86, best == "1" and "day" or "days", "gray", "small")
  end

  local hint
  if card.message and time.ms() < card.message.untilMs then
    hint = card.message.text
  elseif isDone then
    hint = "Well done. See you tomorrow"
  else
    hint = "Press centre when it's done"
  end
  gfx.text(8, H - 20, fit(hint, W - 16, "small"), "gray", "small")
end

local function say(card, text)
  card.message = { text = text, untilMs = time.ms() + UNDO_MS }
end

local function press(card, name)
  if name ~= "center" then return false end
  local h = habits[card.id]
  local t = today()
  if not t then
    say(card, "The clock is not set yet")
    return true
  end
  local again = card.undo and time.ms() < card.undo
  if done(h, t) then
    if again then
      set(h, t, false)
      save(card, h)
      card.undo = nil
      say(card, "Undone")
    else
      card.undo = time.ms() + UNDO_MS
      say(card, "Done. Press again to undo")
    end
  else
    set(h, t, true)
    save(card, h)
    card.undo = time.ms() + UNDO_MS
    say(card, "Nice one. Press again to undo")
    card.flash = true
  end
  remind()
  if card.flash and not lit then led.set(COLOURS[card.config.colour] or COLOURS.Green, 1) end
  card.flash = nil
  return true
end

cards = {
  habit = {
    name = "Habit",
    config = {
      name = { label = "Habit (for example Stretch, Read or No sugar)", default = "Stretch", order = 1 },
      colour = {
        label = "Colour",
        default = "Green",
        options = { "Green", "Blue", "Orange", "Purple", "Pink", "Yellow" },
        order = 2,
      },
    },

    load = function(card)
      if not habits[card.id] then habits[card.id] = open(card) end
      card.title = card.config.name
      remind()
    end,

    update = function(card)
      local t = today() or -1
      local message = card.message and time.ms() < card.message.untilMs and card.message.text or ""
      local shown = t .. message
      if shown == card.shown then return false end
      card.shown = shown
    end,

    draw = draw,
    button = press,
  },
}
