-- Reminders for every day: stand-up at half past ten, stretch at three, go
-- home at half past five. The card counts down to the next one. When it is
-- time, the card comes to the front with the reminder over it and the LED
-- flashes, whatever card you were looking at.
--
-- Write them in the settings as "10:30 Stand-up; 15:00 Stretch; 17:30 Go home",
-- in 24-hour time, in the device's time zone (pick that on its web page).
--
-- While a reminder is up: centre says it is done, up asks for it again in five
-- minutes (a setting). Down switches cards as usual, and the reminder stays up
-- until you answer it or the next one takes its place.
--
-- Shows the three things that make this possible. timer keeps looking at the
-- clock while another card is on screen, which update() does not. time.date()
-- gives the local time, summer time included, and time.make() turns "15:00
-- today" back into a moment to count down to. notify() asks for attention.

plugin = {
  name = "Reminders",
  description = "Daily reminders that come to the front when it is time, with a countdown to the next",
  author = "DeskHog",
  version = "1.1",
  fps = 1, -- for the countdown; it is only repainted when its text changes
}

config = {
  list = {
    label = "Reminders, as 10:30 Stand-up; 15:00 Stretch",
    default = "10:30 Stand-up; 15:00 Stretch; 17:30 Go home",
    order = 1,
  },
  snooze = { label = "Minutes until it asks again, after up", default = 5, order = 2 },
  color = {
    label = "LED colour",
    default = "orange",
    options = { "orange", "red", "yellow", "green", "blue", "purple", "white" },
    order = 3,
  },
}

local W, H = gfx.width, gfx.height

-- The setting, as a list of { at = minutes after midnight, text = ... }, earliest first.
local reminders = {}

-- Settings only hold their values once the file has run, so they are read
-- here and not at the top of the file.
function load()
  reminders = {}
  for entry in config.list:gmatch("[^;\n]+") do
    local hour, minute, text = entry:match("^%s*(%d%d?)[:.](%d%d)%s*(.-)%s*$")
    hour, minute = tonumber(hour), tonumber(minute)
    if hour and hour < 24 and minute < 60 then
      reminders[#reminders + 1] = { at = hour * 60 + minute, text = text ~= "" and text or "Reminder" }
    end
  end
  table.sort(reminders, function(a, b) return a.at < b.at end)
end

local due     -- the reminder that is up: { text, at }, or nil
local snoozed -- the text of one that asks again later, or nil
local looked  -- the minute of the day the clock was last looked at
local shown   -- what was last drawn

local function clock(minutes)
  return string.format("%d:%02d", minutes // 60, minutes % 60)
end

local function ring(text, at)
  due = { text = text, at = at }
  -- The device decides what happens: the card comes to the front with the text
  -- over it, or, when someone is mid-game or the card may not, only the LED flashes.
  notify(text, { led = config.color })
end

-- Once a second, on screen or not: has the minute of a reminder begun?
timer.every(1, function()
  local now = time.date()
  if not now then return false end -- the device has not set its clock yet
  local minute = now.hour * 60 + now.min
  if minute == looked then return false end -- returning false: nothing to repaint
  local first = looked == nil -- just started: do not ring for a minute that was already under way
  looked = minute
  if first then return false end

  local texts = {}
  for _, reminder in ipairs(reminders) do
    if reminder.at == minute then texts[#texts + 1] = reminder.text end
  end
  if #texts == 0 then return false end
  ring(table.concat(texts, ", "), minute)
end)

-- The next reminder, the seconds until it, and whether that is tomorrow.
-- time.make() gives the moment the clock will say so, which keeps the count
-- right on the two days a year the clocks change and a day is not 24 hours.
local function upcoming(t)
  local now = time.date(t)
  local nextOne, wait, tomorrow
  for _, reminder in ipairs(reminders) do
    local at = { year = now.year, month = now.month, day = now.day, hour = reminder.at // 60, min = reminder.at % 60 }
    local when, later = time.make(at), false
    if when <= t then -- today's has been, or is due right now: the next is tomorrow's
      at.day = at.day + 1 -- the 32nd is the 1st of the next month
      when, later = time.make(at), true
    end
    if not wait or when - t < wait then nextOne, wait, tomorrow = reminder, when - t, later end
  end
  return nextOne, wait, tomorrow
end

local function countdown(wait)
  if wait < 60 then return wait .. " s" end
  local minutes = (wait + 59) // 60 -- rounded up, so "1 min" lasts until the last minute begins
  if minutes < 60 then return minutes .. " min" end
  return string.format("%dh %02dm", minutes // 60, minutes % 60)
end

-- What the card says right now: title, big text, its colour and the line under it.
local function describe()
  if due then
    return "It is time", due.text, config.color, "Centre: done.  Up: +" .. config.snooze .. " min"
  end
  local t = time.now()
  if t == 0 then return "Reminders", "...", "gray", "Waiting for the time" end
  local nextOne, wait, tomorrow = upcoming(t)
  if not nextOne then return "Reminders", "None", "gray", "Add some on the web page, like 10:30 Stand-up" end
  local line = (tomorrow and "Tomorrow " or "") .. clock(nextOne.at) .. "  " .. nextOne.text
  return snoozed and "Snoozed: " .. snoozed or "Next reminder", countdown(wait), "white", line
end

function update()
  local title, big, _, line = describe()
  local now = title .. big .. line
  if now == shown then return false end
  shown = now
end

-- The day as a line along the bottom: a tick per reminder, and a dot for now.
local function drawDay(now)
  local left, right, y = 10, W - 10, H - 7
  local function x(minutes) return left + (right - left) * minutes / 1440 end
  local minute = now.hour * 60 + now.min
  gfx.line(left, y, right, y, "#333333")
  for _, reminder in ipairs(reminders) do
    gfx.rect(x(reminder.at) - 1, y - 4, 2, 9, reminder.at > minute and "white" or "#555555")
  end
  gfx.circle(x(minute), y, 3, config.color)
end

function draw()
  local title, big, color, line = describe()
  ui.title(title, due and color or nil)
  ui.big(big, color)
  ui.label(line)
  local now = time.date()
  if now and not due and #reminders > 0 then drawDay(now) end
end

function button(name)
  if not due then return false end -- nothing is up: up and down switch cards
  if name == "center" then
    due = nil
    return true
  end
  if name == "up" then
    local again = due
    due, snoozed = nil, again.text
    timer.after(math.max(1, config.snooze) * 60, function()
      snoozed = nil
      ring(again.text, again.at)
    end)
    return true
  end
  return false
end
