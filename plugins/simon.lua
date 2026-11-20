-- Simon says: watch the three buttons light up in order, then repeat it. One
-- more is added each round. The LED joins in.
--
-- All three buttons belong to the game while a round is on. To leave in the
-- middle, hold centre and press up.

plugin = {
  name = "Simon",
  description = "Watch the pattern, repeat the pattern. It gets longer every round",
  author = "DeskHog",
  version = "1.2",
  fps = 30,
}

local W, H = gfx.width, gfx.height
-- Top to bottom, like the buttons beside the screen.
local PADS = {
  { button = "up", color = "#3498db", dim = "#123a55" },
  { button = "center", color = "#2ecc71", dim = "#0f4a28" },
  { button = "down", color = "#e74c3c", dim = "#561b15" },
}
local SHOW, GAP = 0.45, 0.18 -- seconds a pad is lit, and dark in between

local state = "start" -- "start", "show", "input" or "over"
local pattern, position, lit, timer
local best = 0
local shown

local function light(pad, seconds)
  lit, timer = pad, seconds
  led.set(PADS[pad].color, seconds)
end

local function nextRound()
  pattern[#pattern + 1] = math.random(#PADS)
  position, lit, timer = 0, nil, 0.6 -- a breath before it starts
  state = "show"
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state == "show" then
    timer = timer - dt
    if timer <= 0 then
      if lit then
        lit, timer = nil, GAP
      elseif position < #pattern then
        position = position + 1
        light(pattern[position], SHOW)
      else
        state, position = "input", 0
      end
    end
  elseif lit then
    timer = timer - dt
    if timer <= 0 then lit = nil end
  end
  local now = state .. tostring(lit) .. (pattern and #pattern or 0)
  if now == shown then return false end
  shown = now
end

function draw()
  local height = (H - 16) / #PADS
  for i, pad in ipairs(PADS) do
    gfx.rect(8, 6 + (i - 1) * (height + 2), 70, height, lit == i and pad.color or pad.dim, 6)
  end
  local x = 92 + (W - 92) / 2
  if state == "start" then
    gfx.text(x, 30, "SIMON", "white", "loud", "center")
    gfx.text(x, 62, "Centre to start", "gray", "small", "center")
    if best > 0 then gfx.text(x, 84, "Best " .. best, "gray", "small", "center") end
  elseif state == "over" then
    gfx.text(x, 26, "Wrong!", "red", "loud", "center")
    gfx.text(x, 58, "You got to " .. (#pattern - 1), "white", "small", "center")
    gfx.text(x, 80, "Best " .. best, "gray", "small", "center")
    gfx.text(x, 102, "Centre to try again", "gray", "small", "center")
  else
    gfx.text(x, 26, "Round " .. #pattern, "white", "medium", "center")
    gfx.text(x, 58, state == "show" and "Watch..." or "Your turn", state == "show" and "gray" or "yellow", "medium", "center")
  end
end

function button(name)
  if state == "start" or state == "over" then
    if name ~= "center" then return false end
    pattern = {}
    nextRound()
    return true
  end
  if state == "show" then return true end -- no pressing while it shows
  for i, pad in ipairs(PADS) do
    if pad.button == name then
      position = position + 1
      if pattern[position] ~= i then
        state, lit = "over", nil
        if #pattern - 1 > best then
          best = #pattern - 1
          store.set("best", best)
        end
        led.set("red", 1.5)
      else
        light(i, 0.2)
        if position == #pattern then nextRound() end
      end
    end
  end
  return true
end
