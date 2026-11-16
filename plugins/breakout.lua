-- Breakout. Hold up to move the bat left and down to move it right; centre
-- starts and pauses. Clear the wall to get a faster one.

plugin = {
  name = "Breakout",
  description = "A bat, a ball and a wall of bricks. Three lives",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local COLS, ROWS = 9, 4
local BRICK_W, BRICK_H = 25, 8
local WALL_LEFT, WALL_TOP = (W - COLS * BRICK_W) // 2, 18
local BAT_W, BAT_H, BAT_Y = 38, 4, H - 8
local BALL = 4
local COLORS = { "red", "orange", "yellow", "green" }

local state = "start" -- "start", "serve", "playing", "paused" or "over"
local bricks, left, bat, ball, score, lives, level
local best = 0

local function buildWall()
  bricks, left = {}, COLS * ROWS
  for row = 1, ROWS do
    bricks[row] = {}
    for col = 1, COLS do bricks[row][col] = true end
  end
end

local function serve()
  ball = { x = bat + BAT_W / 2 - BALL / 2, y = BAT_Y - BALL - 1, vx = 0, vy = 0 }
  state = "serve"
end

local function newGame()
  score, lives, level = 0, 3, 1
  bat = (W - BAT_W) / 2
  buildWall()
  serve()
end

local function launch()
  local speed = 95 * (1 + (level - 1) * 0.15)
  ball.vx = (math.random() < 0.5 and -1 or 1) * speed * 0.6
  ball.vy = -speed
  state = "playing"
end

local function finish()
  state = "over"
  if score > best then
    best = score
    store.set("best", best)
  end
  led.set("red", 2)
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state ~= "playing" and state ~= "serve" then return false end

  local moved = false
  if input.held("up") then bat, moved = bat - 170 * dt, true end
  if input.held("down") then bat, moved = bat + 170 * dt, true end
  bat = math.max(0, math.min(W - BAT_W, bat))
  if state == "serve" then
    ball.x = bat + BAT_W / 2 - BALL / 2
    return moved -- nothing else moves until the ball is served
  end

  local beforeY = ball.y
  ball.x = ball.x + ball.vx * dt
  ball.y = ball.y + ball.vy * dt

  if ball.x <= 0 then ball.x, ball.vx = 0, math.abs(ball.vx) end
  if ball.x >= W - BALL then ball.x, ball.vx = W - BALL, -math.abs(ball.vx) end
  if ball.y <= 0 then ball.y, ball.vy = 0, math.abs(ball.vy) end

  -- The bat sends the ball off at an angle that depends on where it was hit.
  if ball.vy > 0 and ball.y + BALL >= BAT_Y and beforeY + BALL <= BAT_Y + BAT_H
    and ball.x + BALL >= bat and ball.x <= bat + BAT_W then
    local where = (ball.x + BALL / 2 - (bat + BAT_W / 2)) / (BAT_W / 2) -- -1 at the left end, 1 at the right
    local speed = math.sqrt(ball.vx * ball.vx + ball.vy * ball.vy)
    ball.vx = where * speed * 0.8
    ball.vy = -math.sqrt(math.max(speed * speed - ball.vx * ball.vx, speed * speed * 0.2))
    ball.y = BAT_Y - BALL
  end

  -- A brick under the middle of the ball breaks.
  local col = math.floor((ball.x + BALL / 2 - WALL_LEFT) / BRICK_W) + 1
  local row = math.floor((ball.y + BALL / 2 - WALL_TOP) / BRICK_H) + 1
  if bricks[row] and bricks[row][col] then
    bricks[row][col] = false
    left = left - 1
    score = score + (ROWS - row + 1)
    -- Coming from above or below it bounces back up or down; otherwise sideways.
    local top = WALL_TOP + (row - 1) * BRICK_H
    if beforeY + BALL / 2 < top or beforeY + BALL / 2 > top + BRICK_H then ball.vy = -ball.vy else ball.vx = -ball.vx end
    if left == 0 then
      level = level + 1
      led.set("green", 1)
      buildWall()
      serve()
    end
  end

  if ball.y > H then
    lives = lives - 1
    if lives == 0 then finish() else serve() end
  end
end

function draw()
  if bricks then
    -- A run of bricks side by side is one rectangle; thin lines cut it into bricks.
    for row = 1, ROWS do
      local from
      for col = 1, COLS + 1 do
        if bricks[row][col] and not from then from = col end
        if not bricks[row][col] and from then
          gfx.rect(WALL_LEFT + (from - 1) * BRICK_W, WALL_TOP + (row - 1) * BRICK_H, (col - from) * BRICK_W, BRICK_H - 1, COLORS[row])
          from = nil
        end
      end
    end
    for col = 0, COLS do
      gfx.rect(WALL_LEFT + col * BRICK_W - 1, WALL_TOP, 1, ROWS * BRICK_H, "black")
    end
    gfx.rect(bat, BAT_Y, BAT_W, BAT_H, "white", 2)
    gfx.rect(ball.x, ball.y, BALL, BALL, "white")
    gfx.text(4, 1, tostring(score), "white", "small")
    gfx.text(W - 4, 1, string.rep("o ", lives - 1), "gray", "small", "right") -- balls in reserve
  end

  local big, small
  if state == "start" then big, small = "BREAKOUT", "Hold up and down to move"
  elseif state == "serve" then small = "Centre to serve"
  elseif state == "paused" then big, small = "Paused", "Centre to carry on"
  elseif state == "over" then big, small = "Game over", "Score " .. score .. ", best " .. best end
  if big then gfx.text(W / 2, 58, big, "white", "loud", "center") end
  if small then gfx.text(W / 2, big and 86 or 76, small, "gray", "small", "center") end
end

function button(name)
  if name == "center" then
    if state == "start" or state == "over" then newGame()
    elseif state == "serve" then launch()
    elseif state == "playing" then state = "paused"
    elseif state == "paused" then state = "playing" end
    return true
  end
  -- Up and down move the bat while a ball is in play. Otherwise they switch cards.
  return state == "playing" or state == "serve"
end
