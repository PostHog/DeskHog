-- Lander. Set the craft down on the pad, gently, before the fuel runs out.
-- Hold centre for the main engine, up to push left, down to push right.
-- Every landing brings a new landscape with a smaller pad and more gravity.
--
-- Shows held buttons, a landscape made up as you go, and simple physics.
--
-- All three buttons belong to the game during a flight. To leave in the
-- middle, hold centre and press up.

plugin = {
  name = "Lander",
  description = "Three thrusters, a little fuel and a landing pad that keeps shrinking",
  author = "DeskHog",
  version = "1.2",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local MAIN, SIDE = 62, 30          -- what the engines add, in pixels a second squared
local SAFE_FALL, SAFE_DRIFT = 26, 14 -- the fastest you may touch the pad, down and sideways
local FOOT = 6                     -- from the middle of the craft to its feet

local state = "start" -- "start", "flying", "landed" or "over"
local level, score, best = 1, 0, 0
local ground, pad, stars -- ground: points {x, y} from left to right; pad: {from, to, y}
local x, y, vx, vy, fuel, gravity
local burning, pushing = false, 0
local reason

local function groundAt(at)
  for i = 2, #ground do
    local a, b = ground[i - 1], ground[i]
    if at <= b.x then return a.y + (b.y - a.y) * (at - a.x) / (b.x - a.x) end
  end
  return ground[#ground].y
end

local function newLevel()
  local width = math.max(22, 46 - (level - 1) * 4)
  local from = math.random(12, W - 12 - width)
  local height = math.random(H - 46, H - 10)
  pad = { from = from, to = from + width, y = height }
  ground = {}
  local at = 0
  while at < from - 4 do -- hills to the left of the pad
    ground[#ground + 1] = { x = at, y = math.random(H - 62, H - 6) }
    at = at + math.random(10, 22)
  end
  ground[#ground + 1] = { x = from, y = height }
  ground[#ground + 1] = { x = from + width, y = height }
  at = from + width + math.random(8, 18)
  while at < W do -- and to the right
    ground[#ground + 1] = { x = at, y = math.random(H - 62, H - 6) }
    at = at + math.random(10, 22)
  end
  ground[#ground + 1] = { x = W, y = math.random(H - 62, H - 6) }
  if ground[1].x > 0 then table.insert(ground, 1, { x = 0, y = math.random(H - 62, H - 6) }) end

  stars = {}
  for i = 1, 12 do stars[i] = { x = math.random(0, W - 1), y = math.random(0, H - 70) } end

  -- Start well away from the pad, so there is some flying to do.
  local middle = (pad.from + pad.to) / 2
  x = middle < W / 2 and math.random(W // 2 + 20, W - 16) or math.random(16, W // 2 - 20)
  y, vx, vy = 14, (middle - x) / 14, 0
  fuel = math.max(55, 100 - (level - 1) * 6)
  gravity = 20 + (level - 1) * 2
  state = "flying"
end

local function newGame()
  level, score = 1, 0
  newLevel()
end

local function finish(landed, why)
  burning, pushing = false, 0
  if landed then
    score = score + 50 + math.floor(fuel)
    state = "landed"
    led.set("green", 1)
  else
    reason = why
    state = "over"
    led.set("red", 1.5)
  end
  if score > best then
    best = score
    store.set("best", best)
  end
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state ~= "flying" then return false end
  dt = math.min(dt, 0.1)
  burning = fuel > 0 and input.held("center")
  pushing = 0
  if fuel > 0 then
    if input.held("up") then pushing = pushing - 1 end
    if input.held("down") then pushing = pushing + 1 end
  end
  if burning then fuel = fuel - 11 * dt end
  if pushing ~= 0 then fuel = fuel - 5 * dt end
  fuel = math.max(0, fuel)

  vy = vy + (gravity - (burning and MAIN or 0)) * dt
  vx = vx + pushing * SIDE * dt
  x, y = x + vx * dt, y + vy * dt
  if x < 6 then x, vx = 6, 0 elseif x > W - 6 then x, vx = W - 6, 0 end
  if y < -30 then y, vy = -30, 0 end

  local floor = math.min(groundAt(x - 5), groundAt(x), groundAt(x + 5))
  if y + FOOT >= floor then
    y = floor - FOOT
    if x - 5 < pad.from or x + 5 > pad.to then finish(false, "Missed the pad")
    elseif vy > SAFE_FALL then finish(false, "Too fast")
    elseif math.abs(vx) > SAFE_DRIFT then finish(false, "Too much sideways")
    else finish(true) end
  end
end

function draw()
  if not ground then
    gfx.text(W / 2, 22, "LANDER", "white", "loud", "center")
    gfx.text(W / 2, 54, "Hold centre: engine", "gray", "small", "center")
    gfx.text(W / 2, 72, "Up: left   Down: right", "gray", "small", "center")
    gfx.text(W / 2, 100, best > 0 and "Best " .. best .. ". Centre to start" or "Centre to start", "white", "small",
      "center")
    return
  end

  for _, star in ipairs(stars) do gfx.pixel(star.x, star.y, "#777777") end
  for i = 2, #ground do
    gfx.line(ground[i - 1].x, ground[i - 1].y, ground[i].x, ground[i].y, "#8a8a8a", 1)
  end
  gfx.rect(pad.from, pad.y - 1, pad.to - pad.from, 3, "green")

  -- The craft: a cabin on two legs, and whatever is firing.
  if state == "over" then
    for i = 0, 5 do -- what is left of it
      gfx.line(x, y + 3, x + math.cos(i * 1.1) * 9, y + 3 - math.abs(math.sin(i * 1.1)) * 9, "orange", 1)
    end
  else
    gfx.rect(x - 4, y - 5, 8, 7, "white", 2)
    gfx.rect(x - 2, y - 3, 4, 2, "#3498db")
    gfx.line(x - 4, y + 2, x - 6, y + FOOT - 1, "white", 1)
    gfx.line(x + 4, y + 2, x + 6, y + FOOT - 1, "white", 1)
    if burning then gfx.line(x, y + 3, x, y + 8 + math.random(0, 4), "orange", 2) end
    if pushing < 0 then gfx.line(x + 5, y - 1, x + 9 + math.random(0, 2), y - 1, "orange", 1) end
    if pushing > 0 then gfx.line(x - 5, y - 1, x - 9 - math.random(0, 2), y - 1, "orange", 1) end
  end

  -- Fuel on the left, how hard you would hit on the right.
  gfx.outline(4, 4, 52, 7, "#666666")
  gfx.rect(5, 5, 50 * fuel / 100, 5, fuel > 20 and "white" or "red")
  local safe = vy <= SAFE_FALL and math.abs(vx) <= SAFE_DRIFT
  gfx.text(W - 4, 1, "Speed " .. math.floor(math.max(0, vy) + 0.5), safe and "green" or "orange", "small", "right")
  gfx.text(W / 2, 1, "Level " .. level, "gray", "small", "center")

  local big, first, second
  if state == "landed" then big, first, second = "Landed!", "Score " .. score, "Centre for the next one"
  elseif state == "over" then big, first, second = reason, "Score " .. score .. ", best " .. best, "Centre to fly again" end
  if big then
    gfx.rect(24, 24, W - 48, 66, "black", 6)
    gfx.outline(24, 24, W - 48, 66, "#444444")
    gfx.text(W / 2, 29, big, state == "landed" and "green" or "red", "medium", "center")
    gfx.text(W / 2, 50, first, "white", "small", "center")
    gfx.text(W / 2, 68, second, "gray", "small", "center")
  end
end

function button(name)
  if state == "flying" then return true end -- the buttons are read while held, in update()
  if name ~= "center" then return false end
  if state == "landed" then
    level = level + 1
    newLevel()
  else
    newGame()
  end
  return true
end
