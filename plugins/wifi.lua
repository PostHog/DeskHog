-- WiFi signal meter. Carry the DeskHog around the house and watch how well it
-- hears your network: a dial with the strength in dBm, a word for how good
-- that is, and how it went over the last half minute.
--
-- The centre button starts over: it forgets the best reading and the history,
-- for when you walk into the next room.
--
-- Shows net.rssi() and its friends, a dial made of arcs, and a timer that
-- takes a sample every second, also while another card is on screen.

plugin = {
  name = "WiFi",
  description = "A signal meter to carry around: strength as a dial, with the last half minute as a chart",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local WEAKEST, STRONGEST = -90, -40 -- dBm at the two ends of the dial
local DIAL_X, DIAL_Y, RADIUS, THICK = 64, 66, 52, 9
local FROM, SWEEP = 225, 270        -- the dial starts at 7 o'clock and runs to 5 o'clock
local SAMPLES = 33                  -- one a second
local CHART_X, CHART_Y, CHART_H = 131, 64, 62

-- From strong to weak: at least this many dBm earns the word.
local LEVELS = {
  { from = -55, word = "Excellent", color = "#2ecc71" },
  { from = -67, word = "Good", color = "#a3d94a" },
  { from = -75, word = "Fair", color = "orange" },
  { from = -999, word = "Poor", color = "#e74c3c" },
}

local history = {} -- the last readings, oldest first; false for a moment without WiFi
local best         -- the strongest reading since the last start over
local shown        -- where the dial's hand is: it glides towards the reading

local function levelOf(rssi)
  for _, level in ipairs(LEVELS) do
    if rssi >= level.from then return level end
  end
end

-- 0 at the weak end of the dial, 1 at the strong end.
local function share(rssi)
  return math.max(0, math.min(1, (rssi - WEAKEST) / (STRONGEST - WEAKEST)))
end

local function sample()
  local rssi = net.rssi() -- nil while offline
  history[#history + 1] = rssi or false
  if #history > SAMPLES then table.remove(history, 1) end
  if rssi and (not best or rssi > best) then best = rssi end
end

-- The network's name, cut short if it is wider than the space it gets.
local function fitted(text, width)
  if gfx.textWidth(text) <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "...") > width do
    text = text:sub(1, utf8.offset(text, -1) - 1) -- drop a whole character, not half of an é
  end
  return text .. "..."
end

-- A timer rather than update(dt): the history keeps filling while you look at
-- another card. load() runs again when WiFi comes back, so make it only once.
local sampler
function load()
  sampler = sampler or timer.every(1, sample)
  if #history == 0 then sample() end
end

-- update(dt) only moves the hand. Once it has arrived there is nothing to
-- repaint until the next sample, and returning false says so.
function update(dt)
  local rssi = net.rssi()
  if not rssi then
    shown = nil
    return false
  end
  if not shown then shown = WEAKEST end
  local gap = rssi - shown
  if math.abs(gap) < 0.05 then return false end
  shown = shown + gap * math.min(1, dt * 6)
end

local function drawDial(rssi)
  gfx.arc(DIAL_X, DIAL_Y, RADIUS, FROM, FROM + SWEEP, "#2a2a2a", THICK) -- the empty dial
  if not rssi then
    gfx.text(DIAL_X, DIAL_Y - 22, "--", "#666666", "large", "center")
    gfx.text(DIAL_X, DIAL_Y + 18, "dBm", "#666666", "tiny", "center")
    gfx.text(DIAL_X, H - 23, "Offline", "#e74c3c", "medium", "center")
    return
  end

  local level = levelOf(rssi)
  local hand = share(shown or rssi)
  if hand > 0 then gfx.arc(DIAL_X, DIAL_Y, RADIUS, FROM, FROM + SWEEP * hand, level.color, THICK) end

  -- A white notch where the best reading was, so you can find that spot again.
  if best then
    local at = FROM + SWEEP * share(best)
    gfx.arc(DIAL_X, DIAL_Y, RADIUS + 3, at - 1.5, at + 1.5, "white", THICK + 6)
  end

  gfx.text(DIAL_X, DIAL_Y - 22, tostring(rssi), "white", "large", "center")
  gfx.text(DIAL_X, DIAL_Y + 18, "dBm", "gray", "tiny", "center")
  gfx.text(DIAL_X, H - 23, level.word, level.color, "medium", "center")
end

local function drawChart()
  local width = W - 4 - CHART_X
  local floor = CHART_Y + CHART_H
  gfx.line(CHART_X, floor, CHART_X + width, floor, "#444444", 1)
  -- The newest reading is the bar on the right; older ones move off to the left.
  for i, rssi in ipairs(history) do
    local x = CHART_X + width - (#history - i + 1) * 3
    if rssi then
      local tall = math.max(2, (CHART_H - 2) * share(rssi))
      gfx.rect(x, floor - tall, 2, tall, levelOf(rssi).color)
    else
      gfx.rect(x, floor - 2, 2, 2, "#e74c3c") -- no WiFi at that moment
    end
  end
end

function draw()
  local rssi = net.online() and net.rssi() or nil
  drawDial(rssi)

  local x, width = CHART_X, W - 4 - CHART_X
  if rssi then
    gfx.text(x, 4, fitted(net.ssid() or "", width), "white", "small")
    gfx.text(x, 23, net.ip() or "", "gray", "tiny")
  else
    gfx.text(x, 4, "No WiFi", "white", "small")
    gfx.text(x, 23, "Not connected", "gray", "tiny")
  end
  gfx.text(x, 36, best and ("Best " .. best .. " dBm") or "Best: none yet", "gray", "tiny")
  drawChart()
end

function button(name)
  if name ~= "center" then return false end -- up and down still switch cards
  history, best = {}, nil
  sample()
  return true
end
