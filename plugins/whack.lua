-- Whack-a-hog. Three burrows, three buttons: press the one beside the hedgehog
-- that pops up, before it ducks again. Three misses and the game is over. A
-- press on an empty burrow counts as a miss too.
--
-- All three buttons belong to the game while it is on. To leave in the middle,
-- hold centre and press up.

plugin = {
  name = "Whack-a-hog",
  description = "Three burrows, three buttons. Bonk the hedgehog before it ducks",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local BUTTONS = { up = 1, center = 2, down = 3 }
local FIELD = 132          -- the grass is this wide; the score is to its right
local HOLE_X, HOLE_W = 66, 64
local LIVES = 3

local state = "start" -- "start", "playing" or "over"
local score, lives, best = 0, LIVES, 0
local hole, lastHole     -- where the hedgehog is, if it is up
local up, wait           -- seconds it stays up, and seconds until the next one
local bonk, bonkHole = 0, nil
local miss = 0           -- seconds the "missed" flash still shows

local function middle(i) return 20 + (i - 1) * 42 end

local function stay() return math.max(0.42, 1.15 - score * 0.025) end

local function loseLife()
  lives = lives - 1
  miss = 0.3
  led.set("red", 0.3)
  if lives <= 0 then
    state, hole = "over", nil
    if score > best then
      best = score
      store.set("best", best)
    end
  end
end

local function newGame()
  score, lives, hole, lastHole = 0, LIVES, nil, nil
  wait, bonk, miss = 0.7, 0, 0
  state = "playing"
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state ~= "playing" then
    if miss <= 0 then return false end
    miss = miss - dt
    return
  end
  bonk = math.max(0, bonk - dt)
  miss = math.max(0, miss - dt)
  if hole then
    up = up - dt
    if up <= 0 then
      hole = nil
      wait = 0.25 + math.random() * 0.5
      loseLife() -- it got away
    end
  else
    wait = wait - dt
    if wait <= 0 then
      repeat hole = math.random(3) until hole ~= lastHole
      lastHole, up = hole, stay()
    end
  end
end

local function hog(x, y, dizzy)
  gfx.circle(x, y, 15, "#5a3d2b")            -- spikes
  gfx.circle(x, y + 4, 11, "#e8c9a0")        -- face
  if dizzy then
    for side = -1, 1, 2 do
      gfx.line(x + side * 4 - 2, y, x + side * 4 + 2, y + 4, "black", 1)
      gfx.line(x + side * 4 - 2, y + 4, x + side * 4 + 2, y, "black", 1)
    end
  else
    gfx.circle(x - 4, y + 2, 1.5, "black")
    gfx.circle(x + 4, y + 2, 1.5, "black")
  end
  gfx.circle(x, y + 8, 2, "#3a2a20")         -- nose
end

function draw()
  gfx.rect(0, 0, FIELD, H, miss > 0 and "#5a2424" or "#1e4d2b")
  for i = 1, 3 do
    local y = middle(i)
    gfx.rect(HOLE_X - HOLE_W / 2, y + 8, HOLE_W, 14, "#1a120b", 7)
    local showing = (hole == i) or (bonk > 0 and bonkHole == i)
    if showing then
      -- How far out it is: quickly up, quickly down again at the end.
      local out = 1
      if hole == i then out = math.min(1, (stay() - up) / 0.08, up / 0.08) end
      gfx.clip(0, y - 22, FIELD, 37) -- nothing shows below the middle of the burrow
      hog(HOLE_X, y + 22 - out * 22, bonkHole == i and bonk > 0)
      gfx.clip()
      if bonk > 0 and bonkHole == i then gfx.text(HOLE_X + 22, y - 16, "Bonk!", "yellow", "small") end
    end
  end

  local x = FIELD + (W - FIELD) / 2
  if state == "start" then
    gfx.text(x, 16, "WHACK", "white", "loud", "center")
    gfx.text(x, 40, "A HOG", "white", "loud", "center")
    gfx.text(x, 74, "Centre: start", "gray", "small", "center")
    if best > 0 then gfx.text(x, 96, "Best " .. best, "gray", "small", "center") end
  elseif state == "over" then
    gfx.text(x, 14, "Game over", "red", "medium", "center")
    gfx.text(x, 36, tostring(score), "white", "large", "center")
    gfx.text(x, 82, "Best " .. best, "gray", "small", "center")
    gfx.text(x, 104, "Centre: again", "gray", "small", "center")
  else
    gfx.text(x, 22, tostring(score), "white", "large", "center")
    for i = 1, LIVES do
      gfx.circle(x - 18 + (i - 1) * 18, 88, 5, i <= lives and "red" or "#3a3a3a")
    end
  end
end

function button(name)
  if state ~= "playing" then
    if name ~= "center" then return false end
    newGame()
    return true
  end
  if hole and BUTTONS[name] == hole then
    score = score + 1
    bonk, bonkHole, hole = 0.25, hole, nil
    wait = 0.3 + math.random() * 0.5
    led.set("green", 0.15)
  elseif not (bonk > 0 and BUTTONS[name] == bonkHole) then -- a second tap on a bonked hog is forgiven
    loseLife()
  end
  return true
end
