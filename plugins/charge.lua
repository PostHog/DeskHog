-- Charge. Fling five acorns at a flag. Hold centre to charge the launcher and
-- let go to fire: the longer you hold, the further it flies. Up and down tilt
-- the barrel. The closer to the flag an acorn lands, the more it is worth, and
-- the flag moves after every shot. From the third shot a wall stands in the
-- way, so a flat shot no longer does it.
--
-- Shows the three button events. "press" starts charging, "release" fires,
-- and "long" (held for 0.6 seconds) shifts the charge into a faster gear: a
-- short hold stays gentle enough to aim at a flag that is close by.
--
-- All three buttons belong to the game during a round. To leave in the
-- middle, hold centre and press up.

plugin = {
  name = "Charge",
  description = "Hold to charge, let go to fire: five acorns, a flag that moves and a wall in the way",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local GROUND = 106                     -- y of the ground; the power meter sits under it
local GUN_X, GUN_Y, BARREL = 15, GROUND - 5, 13
local GRAVITY = 300                    -- pixels a second squared
local SLOWEST, FASTEST = 95, 255       -- how fast an acorn leaves at no charge and at full charge
local GENTLE, QUICK = 0.35, 0.9        -- how much of the meter fills in a second, before and after "long"
local STEEPEST, FLATTEST, TILT = 70, 30, 5 -- the barrel's angle, in degrees up from the ground
local SHOTS = 5
local RINGS = { { 18, "#555555" }, { 9, "orange" }, { 3, "#e74c3c" } } -- half widths of the target's rings

local state = "start" -- "start", "aim", "charging", "flying", "landed" or "over"
local shot, score, best = 0, 0, 0
local angle = 45
local power, rising, quick = 0, true, false
local lastPower                        -- where the meter stood at the previous shot
local flagX, wall                      -- wall: { x, height }, or nil
local acorn                            -- { x, y, vx, vy }
local trail, oldTrail = {}, {}         -- where this acorn has been, and the one before it
local verdict, gained, wait

-- The end of the barrel: where an acorn starts.
local function muzzle()
  local tilt = math.rad(angle)
  return GUN_X + math.cos(tilt) * BARREL, GUN_Y - math.sin(tilt) * BARREL
end

local function newRound()
  shot = shot + 1
  flagX = math.random(84, W - 22)
  wall = nil
  if shot >= 3 then
    -- Somewhere around the middle of the way, where a lobbed acorn is at its highest.
    local way = flagX - GUN_X
    wall = { x = math.floor(GUN_X + way * (0.42 + math.random() * 0.18)), height = math.random(18, 34) }
  end
  power, rising, quick = 0, true, false
  state = "aim"
end

local function newGame()
  shot, score = 0, 0
  lastPower, trail, oldTrail = nil, {}, {}
  newRound()
end

local function fire()
  local speed = SLOWEST + (FASTEST - SLOWEST) * power
  local tilt = math.rad(angle)
  local x, y = muzzle()
  acorn = { x = x, y = y, vx = math.cos(tilt) * speed, vy = -math.sin(tilt) * speed }
  lastPower = power
  oldTrail, trail = trail, {}
  state = "flying"
end

-- The acorn has stopped: say how good that was.
local function landed(what)
  gained = 0
  if what then
    verdict = what
  else
    local off = acorn.x - flagX
    local far = math.abs(off)
    if far <= RINGS[3][1] then
      gained, verdict = 100, "Bullseye!"
      led.set("green", 1)
    elseif far <= RINGS[1][1] + 2 then
      gained = math.max(5, math.floor(90 - far * 4.5))
      verdict = far <= RINGS[2][1] and "Close!" or "On the edge"
    else
      verdict = off < 0 and "Too short" or "Too far"
    end
  end
  score = score + gained
  wait = 1.6
  state = "landed"
end

local function finish()
  state = "over"
  if score > best then
    best = score
    store.set("best", best)
    verdict = "New best!"
    led.set("green", 2)
  else
    verdict = "Game over"
  end
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  dt = math.min(dt, 0.1)
  if state == "charging" then
    -- The meter runs up and then back down, so holding on for too long costs you.
    local step = (quick and QUICK or GENTLE) * dt
    power = power + (rising and step or -step)
    if power >= 1 then power, rising = 1, false
    elseif power <= 0 then power, rising = 0, true end
  elseif state == "flying" then
    acorn.vy = acorn.vy + GRAVITY * dt
    acorn.x, acorn.y = acorn.x + acorn.vx * dt, acorn.y + acorn.vy * dt
    trail[#trail + 1] = { acorn.x, acorn.y }
    if wall and acorn.x >= wall.x - 2 and acorn.x <= wall.x + 7 and acorn.y >= GROUND - wall.height then
      acorn.x = wall.x - 3
      landed("Into the wall")
    elseif acorn.y >= GROUND - 2 then
      acorn.y = GROUND - 2
      landed()
    elseif acorn.x > W + 6 then
      landed("Out of the garden")
    end
  elseif state == "landed" then
    wait = wait - dt
    if wait <= 0 then
      if shot < SHOTS then newRound() else finish() end
    end
  else
    return false -- nothing moves while you aim, or on the other screens
  end
end

local function dots(points, color)
  for i = 1, #points, 2 do gfx.pixel(points[i][1], points[i][2], color) end
end

local function drawMeter()
  local x, y, width = 6, H - 15, W - 12
  gfx.outline(x, y, width, 10, "#666666")
  local filled = (width - 2) * power
  if filled >= 1 then
    gfx.rect(x + 1, y + 1, filled, 8, power > 0.85 and "#e74c3c" or power > 0.5 and "orange" or "yellow")
  end
  if lastPower then -- a notch where you let go last time, to judge the next one by
    local at = x + 1 + (width - 2) * lastPower
    gfx.line(at, y - 3, at, y + 12, "white", 1)
  end
end

local function drawField()
  gfx.line(0, GROUND, W, GROUND, "#8a8a8a", 1)
  for _, ring in ipairs(RINGS) do
    gfx.rect(flagX - ring[1], GROUND, ring[1] * 2, 4, ring[2])
  end
  gfx.line(flagX, GROUND, flagX, GROUND - 18, "white", 1)
  gfx.triangle(flagX + 1, GROUND - 18, flagX + 10, GROUND - 14, flagX + 1, GROUND - 10, "#e74c3c")
  if wall then gfx.rect(wall.x, GROUND - wall.height, 5, wall.height, "#b0703c") end

  -- The launcher: a barrel on a wheel, and a faint arc that shows how far it tilts.
  local tipX, tipY = muzzle()
  gfx.arc(GUN_X, GUN_Y, 22, 90 - STEEPEST, 90 - FLATTEST, "#333333", 2)
  gfx.line(GUN_X, GUN_Y, tipX, tipY, state == "charging" and "yellow" or "white", 5)
  gfx.circle(GUN_X, GUN_Y, 6, "#8a8a8a")
  gfx.circle(GUN_X, GUN_Y, 2, "black")
end

function draw()
  if state == "start" then
    gfx.text(W / 2, 14, "CHARGE", "white", "loud", "center")
    gfx.text(W / 2, 46, "Hold centre to charge", "gray", "small", "center")
    gfx.text(W / 2, 63, "Let go to fire", "gray", "small", "center")
    gfx.text(W / 2, 80, "Up and down tilt the barrel", "gray", "small", "center")
    gfx.text(W / 2, 108, best > 0 and "Best " .. best .. ". Centre to start" or "Centre to start", "white", "small",
      "center")
    return
  end

  dots(oldTrail, "#444444")
  dots(trail, "#999999")
  drawField()
  if state == "flying" or state == "landed" then gfx.circle(acorn.x, acorn.y, 3, "orange") end
  drawMeter()

  gfx.text(4, 2, "Shot " .. shot .. " of " .. SHOTS, "gray", "tiny")
  gfx.text(W - 4, 0, tostring(score), "white", "medium", "right")
  if state == "aim" or state == "charging" then
    gfx.text(4, GUN_Y - 38, angle .. "°", "gray", "tiny")
  end
  if state == "aim" and shot == 1 then
    gfx.text(W / 2, 22, "Hold centre, let go to fire", "gray", "small", "center")
  elseif state == "landed" then
    gfx.text(W / 2, 14, gained > 0 and "+" .. gained or verdict, gained > 0 and "green" or "orange", "loud", "center")
    if gained > 0 then gfx.text(W / 2, 38, verdict, "white", "small", "center") end
  elseif state == "over" then
    gfx.rect(24, 20, W - 48, 66, "black", 6)
    gfx.outline(24, 20, W - 48, 66, "#444444")
    gfx.text(W / 2, 25, verdict, verdict == "New best!" and "green" or "white", "medium", "center")
    gfx.text(W / 2, 46, "Score " .. score .. ", best " .. best, "white", "small", "center")
    gfx.text(W / 2, 64, "Centre to play again", "gray", "small", "center")
  end
end

-- With a second parameter, button() also hears "long" and "release". Only
-- what it returns for a "press" decides whether the cards switch.
function button(name, event)
  if name == "center" then
    if event == "press" then
      if state == "start" or state == "over" then
        newGame() -- the release of this press finds nothing charging, and does nothing
        return true
      end
      -- No need to wait for the score to fade: a press goes straight on to the next shot.
      if state == "landed" then
        if shot < SHOTS then newRound() else finish() end
      end
      if state == "aim" then
        power, rising, quick = 0, true, false
        state = "charging"
      end
    elseif state == "charging" then
      if event == "long" then quick = true end
      if event == "release" then fire() end
    end
    return true
  end

  if event ~= "press" then return end
  if state == "aim" or state == "charging" then
    angle = math.max(FLATTEST, math.min(STEEPEST, angle + (name == "up" and TILT or -TILT)))
    return true
  end
  -- Up and down switch cards on the title and the score, not in the middle of a round.
  return state == "flying" or state == "landed"
end
