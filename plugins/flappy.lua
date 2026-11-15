-- Flappy Hog: tap the centre button to stay in the air.
-- Shows the game-style API: update(dt) runs every frame, draw() paints with gfx.

plugin = {
  name = "Flappy Hog",
  description = "One button. Endless frustration. Infinite glory.",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local BIRD_X, BIRD_SIZE = 30, 10
local PIPE_WIDTH, GAP, SPACING = 20, 70, 125
local GRAVITY, FLAP, SPEED = 420, -150, 60 -- pixels per second (squared, for gravity)

local state -- "ready", "playing" or "over"
local bird, pipes, score
local best = 0

local function newPipe(x)
  return { x = x, gap = math.random(15, H - 15 - GAP), scored = false }
end

local function reset()
  state = "ready"
  bird = { y = H / 2, speed = 0 }
  pipes = { newPipe(W + 20), newPipe(W + 20 + SPACING) }
  score = 0
end

local function crash()
  state = "over"
  if score > best then
    best = score
    store.set("best", best)
  end
  led.set("red", 2)
end

function load()
  best = store.get("best", 0)
  reset()
end

function update(dt)
  if state ~= "playing" then return end

  bird.speed = bird.speed + GRAVITY * dt
  bird.y = bird.y + bird.speed * dt
  if bird.y < 0 or bird.y + BIRD_SIZE > H then return crash() end

  for i, pipe in ipairs(pipes) do
    pipe.x = pipe.x - SPEED * dt
    if pipe.x + PIPE_WIDTH < 0 then
      -- Off the left edge: send it back in behind the other pipe.
      local other = pipes[3 - i]
      pipes[i] = newPipe(other.x + SPACING)
      pipe = pipes[i]
    end

    local overlapsX = BIRD_X + BIRD_SIZE > pipe.x and BIRD_X < pipe.x + PIPE_WIDTH
    local inGap = bird.y >= pipe.gap and bird.y + BIRD_SIZE <= pipe.gap + GAP
    if overlapsX and not inGap then return crash() end

    if not pipe.scored and pipe.x + PIPE_WIDTH < BIRD_X then
      pipe.scored = true
      score = score + 1
    end
  end
end

function draw()
  for _, pipe in ipairs(pipes) do
    gfx.rect(pipe.x, 0, PIPE_WIDTH, pipe.gap, "green")
    gfx.rect(pipe.x, pipe.gap + GAP, PIPE_WIDTH, H - pipe.gap - GAP, "green")
  end
  gfx.rect(BIRD_X, bird.y, BIRD_SIZE, BIRD_SIZE, "yellow", 2)
  gfx.text(6, 2, tostring(score), "white", "loud")

  if state == "ready" then
    gfx.text(W / 2, H / 2 - 10, "Press center", "white", "medium", "center")
  elseif state == "over" then
    gfx.text(W / 2, H / 2 - 24, "Game over", "white", "loud", "center")
    gfx.text(W / 2, H / 2 + 6, "Best " .. best, "gray", "small", "center")
  end
end

function button(name)
  if name ~= "center" then return false end -- up and down still switch cards
  if state == "over" then
    reset()
  else
    state = "playing"
    bird.speed = FLAP
  end
  return true
end
