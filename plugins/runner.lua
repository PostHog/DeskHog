-- Hog run. The hedgehog runs by itself; you get it past what is in the way.
-- Centre (or up) jumps over stumps and logs, down curls it into a ball that
-- rolls under the bees. It gets faster the further you get.
--
-- Shows a game where the world moves and the player stays put, and a button
-- that works both as a press and held down.

plugin = {
  name = "Hog run",
  description = "An endless run: jump the logs, roll under the bees",
  author = "Tim Broddin",
  version = "1.1",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local GROUND = 112                 -- y of the ground
local HOG_X, HOG_W, HOG_H = 30, 24, 18
local BALL = 14                    -- its height when rolled up
local JUMP, GRAVITY = 250, 720     -- pixels a second, and a second squared
local ROLL = 0.45                  -- a press of down rolls at least this long

local state = "start" -- "start", "playing" or "over"
local distance, speed, height, fall, rolling = 0, 115, 0, 0, 0
local things, gap
local best = 0

local KINDS = {
  stump = { w = 12, h = 14, lift = 0 },
  log = { w = 26, h = 10, lift = 0 },
  bee = { w = 12, h = 8, lift = 13 },  -- too low to run under, too high to trip over
}

local function points() return math.floor(distance / 20) end

local function spawn()
  local kind = "stump"
  local roll = math.random()
  if points() >= 15 and roll < 0.3 then kind = "bee"
  elseif roll < 0.6 then kind = "log" end
  things[#things + 1] = { kind = kind, x = W + 10 }
  gap = speed * (0.85 + math.random() * 0.8)
end

local function newGame()
  distance, speed, height, fall, rolling = 0, 115, 0, 0, 0
  things, gap = {}, 120
  state = "playing"
end

local function crashed()
  local tall = rolling > 0 and height == 0 and BALL or HOG_H
  -- A little forgiving: the hedgehog's box is 3 pixels smaller all round.
  local left, right = HOG_X + 3, HOG_X + HOG_W - 3
  local bottom, top = height + 2, height + tall - 3
  for _, thing in ipairs(things) do
    local kind = KINDS[thing.kind]
    if thing.x < right and thing.x + kind.w > left and kind.lift < top and kind.lift + kind.h > bottom then
      return true
    end
  end
  return false
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state ~= "playing" then return false end
  dt = math.min(dt, 0.1)
  speed = math.min(240, 115 + distance / 60)
  distance = distance + speed * dt

  if height > 0 or fall > 0 then
    height = height + fall * dt
    fall = fall - GRAVITY * dt
    if height <= 0 then height, fall = 0, 0 end
  end
  rolling = math.max(0, rolling - dt)
  if input.held("down") then rolling = math.max(rolling, 0.05) end

  for i = #things, 1, -1 do
    things[i].x = things[i].x - speed * dt
    if things[i].x < -40 then table.remove(things, i) end
  end
  local last = things[#things]
  if not last or last.x < W - gap then spawn() end

  if crashed() then
    state = "over"
    if points() > best then
      best = points()
      store.set("best", best)
    end
    led.set("red", 1.5)
  end
end

local function hog(x, y)
  if rolling > 0 and height == 0 then
    local angle = distance / 7
    gfx.circle(x + 12, y - 7, 7, "#6b4a2f")
    gfx.circle(x + 12 + math.cos(angle) * 4, y - 7 + math.sin(angle) * 4, 1.5, "#e8c9a0")
    return
  end
  gfx.rect(x, y - 17, 20, 14, "#6b4a2f", 6)             -- the spiky back
  for i = 0, 2 do gfx.line(x + 4 + i * 5, y - 16, x + 2 + i * 5, y - 20, "#6b4a2f", 2) end
  gfx.circle(x + 19, y - 9, 5, "#e8c9a0")               -- face
  gfx.rect(x + 20, y - 12, 2, 2, "black")
  gfx.circle(x + 24, y - 8, 1.5, "black")
  local stride = height == 0 and (distance // 9) % 2 == 0
  gfx.rect(x + (stride and 3 or 6), y - 3, 3, 3, "#e8c9a0")
  gfx.rect(x + (stride and 14 or 11), y - 3, 3, 3, "#e8c9a0")
end

function draw()
  gfx.line(0, GROUND, W, GROUND, "#666666", 1)
  for i = 0, 5 do
    gfx.rect((i * 47 - distance) % (W + 20) - 10, GROUND + 5 + (i % 3) * 5, 7, 1, "#3a3a3a")
  end

  if things then
    for _, thing in ipairs(things) do
      local kind = KINDS[thing.kind]
      local x, y = thing.x, GROUND - kind.lift - kind.h
      if thing.kind == "stump" then
        gfx.rect(x, y, kind.w, kind.h, "#8e5a2b", 2)
        gfx.rect(x, y, kind.w, 3, "#c98f55", 1)
      elseif thing.kind == "log" then
        gfx.rect(x, y, kind.w, kind.h, "#7a4a22", 5)
        gfx.circle(x + 5, y + 5, 3, "#c98f55")
      else
        gfx.rect(x + 3, y - 4, 6, 5, "#dfe6e9", 2)      -- wings
        gfx.rect(x, y, kind.w, kind.h, "yellow", 3)
        gfx.rect(x + 4, y, 2, kind.h, "black")
        gfx.rect(x + 8, y, 2, kind.h, "black")
      end
    end
    gfx.text(W - 4, 2, tostring(points()), "white", "small", "right")
  end
  hog(HOG_X, GROUND - height)

  local big, small
  if state == "start" then big, small = "HOG RUN", "Centre jumps, down rolls"
  elseif state == "over" then big, small = "Ouch", "Score " .. points() .. ", best " .. best end
  if big then
    gfx.rect(20, 20, W - 40, 62, "black", 6)
    gfx.outline(20, 20, W - 40, 62, "#444444")
    gfx.text(W / 2, 27, big, "white", "loud", "center")
    gfx.text(W / 2, 56, small, "gray", "small", "center")
  end
end

function button(name)
  if state ~= "playing" then
    if name ~= "center" then return false end -- up and down switch cards when no run is on
    newGame()
    return true
  end
  if name == "down" then
    rolling = ROLL
  elseif height == 0 then
    fall, height, rolling = JUMP, 0.01, 0
  end
  return true
end
