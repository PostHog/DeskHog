-- A Pomodoro timer: 25 minutes of focus, 5 minutes of break, and a longer
-- break after four rounds. The LED is red while you focus (leave me alone)
-- and green during a break.
--
-- Centre starts, pauses and carries on. While paused, up starts over.
--
-- Shows keeping time while the card is not on screen: update() only runs for
-- the card you are looking at, so the clock is read from time.ms() rather than
-- counted, and load() (which runs every few seconds for every card) moves the
-- timer on to its next part.

plugin = {
  name = "Pomodoro",
  description = "Focus for 25 minutes, take a break, repeat. The LED shows which it is",
  author = "Tim Broddin",
  version = "1.1",
  fps = 2,
  refresh = 5, -- load() every five seconds, also while another card is showing
}

config = {
  focus = { label = "Minutes of focus", default = 25, order = 1 },
  rest = { label = "Minutes of break", default = 5, order = 2 },
  long = { label = "Minutes of long break, after four rounds", default = 15, order = 3 },
}

local phase = "idle" -- "idle", "focus", "break" or "long"
local round = 0      -- rounds of focus finished
local endsAt = 0     -- time.ms() at which this part ends
local length = 0     -- how long this part is, in ms
local pausedLeft     -- ms left, while paused
local note = "Centre to start"
local shown          -- what was last drawn

local function left()
  if pausedLeft then return pausedLeft end
  return math.max(0, endsAt - time.ms())
end

local function begin(next)
  phase = next
  pausedLeft = nil
  local minutes = next == "focus" and config.focus or next == "break" and config.rest or config.long
  length = math.max(1, minutes) * 60000
  endsAt = time.ms() + length
  if next == "focus" then
    led.set("red")
    note = "Centre to pause"
  else
    led.set("green")
    note = "Stretch your legs"
  end
end

local function stop(message)
  phase, pausedLeft = "idle", nil
  note = message
  led.off()
end

-- Move on when a part has run out. Called from load() and update().
local function step()
  if phase == "idle" or pausedLeft or left() > 0 then return end
  if phase == "focus" then
    round = round + 1
    begin(round % 4 == 0 and "long" or "break")
  else
    stop("Break is over. Centre for round " .. (round % 4 + 1))
  end
end

function load()
  step()
end

function update()
  step()
  local now = phase .. (left() // 1000) .. tostring(pausedLeft ~= nil) .. note
  if now == shown then return false end
  shown = now
end

function draw()
  local title = "Pomodoro"
  if phase == "focus" then title = "Focus, round " .. (round % 4 + 1) .. " of 4"
  elseif phase == "break" then title = "Break"
  elseif phase == "long" then title = "Long break" end
  ui.title(title)

  local seconds = phase == "idle" and config.focus * 60 or (left() + 999) // 1000
  local color = phase == "focus" and "red" or phase == "idle" and "white" or "green"
  ui.big(string.format("%d:%02d", seconds // 60, seconds % 60), pausedLeft and "gray" or color)
  ui.label(pausedLeft and "Paused. Centre carries on, up starts over." or note)

  -- How far along this part is, as a bar along the bottom.
  if phase ~= "idle" then
    gfx.rect(0, gfx.height - 4, gfx.width * (1 - left() / length), 4, color)
  end
end

function button(name)
  if name == "center" then
    if phase == "idle" then
      begin("focus")
    elseif pausedLeft then
      endsAt = time.ms() + pausedLeft
      pausedLeft = nil
      led.set(phase == "focus" and "red" or "green")
    else
      pausedLeft = left()
      led.off()
    end
    return true
  end
  if name == "up" and pausedLeft then
    round = 0
    stop("Centre to start")
    return true
  end
  return false -- up and down switch cards as usual; the timer keeps running
end
