-- Paddle: first to five points beats the hedgehog on the other side.
-- Up and down move your paddle, the centre button starts and pauses.
--
-- Shows reading held buttons, and giving the buttons back: up and down only
-- belong to the game while a point is being played, so you can always leave
-- by pausing first.

plugin = {
  name = "Paddle",
  description = "First to five points beats the hedgehog on the other side",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local PADDLE_W, PADDLE_H, BALL = 5, 30, 5
local PLAYER_X, RIVAL_X = 5, W - 10
local PLAYER_SPEED, RIVAL_SPEED = 150, 62 -- pixels a second; the rival is beatable
local SERVE_SPEED, SPEED_UP = 85, 1.06    -- the ball gets faster with every hit
local WIN = 5
local CHEERS = { "Hog-tastic!", "Spiked 'em!", "Prickly perfect!", "Hawkins would be proud" }

local state = "start" -- "start", "serve", "playing", "paused" or "over"
local player, rival, ball, score, serveIn, message

local function centerBall()
  ball = { x = (W - BALL) / 2, y = (H - BALL) / 2, vx = 0, vy = 0 }
  serveIn = 1.2
  state = "serve"
end

local function newGame()
  player, rival = (H - PADDLE_H) / 2, (H - PADDLE_H) / 2
  score = { player = 0, rival = 0 }
  centerBall()
end

local function point(winner)
  score[winner] = score[winner] + 1
  if score[winner] < WIN then return centerBall() end
  state = "over"
  if winner == "player" then
    message = CHEERS[math.random(#CHEERS)]
    led.set("green", 2)
  else
    message = "Game over"
    led.set("red", 2)
  end
end

local function clamp(value, low, high)
  return math.max(low, math.min(high, value))
end

-- The ball bounces off a paddle at an angle that depends on where it hit.
local function bounce(paddleY, direction)
  local where = ((ball.y + BALL / 2) - (paddleY + PADDLE_H / 2)) / (PADDLE_H / 2) -- -1 at the top, 1 at the bottom
  local speed = math.abs(ball.vx) * SPEED_UP
  ball.vx = direction * speed
  ball.vy = where * speed * 0.9
end

function load()
  newGame()
  state = "start"
end

function update(dt)
  if state == "serve" then
    serveIn = serveIn - dt
    if serveIn <= 0 then
      ball.vx = -SERVE_SPEED -- towards you
      ball.vy = (math.random() < 0.5 and -1 or 1) * SERVE_SPEED * 0.6
      state = "playing"
    end
    return
  end
  if state ~= "playing" then return false end -- nothing moves on the other screens

  if input.held("up") then player = player - PLAYER_SPEED * dt end
  if input.held("down") then player = player + PLAYER_SPEED * dt end
  player = clamp(player, 0, H - PADDLE_H)

  -- The rival only chases the ball once it is coming its way and past the middle.
  if ball.vx > 0 and ball.x > W * 0.45 then
    local target = ball.y + BALL / 2 - PADDLE_H / 2
    rival = rival + clamp(target - rival, -RIVAL_SPEED * dt, RIVAL_SPEED * dt)
    rival = clamp(rival, 0, H - PADDLE_H)
  end

  ball.x = ball.x + ball.vx * dt
  ball.y = ball.y + ball.vy * dt
  if ball.y <= 0 then
    ball.y, ball.vy = 0, math.abs(ball.vy)
  elseif ball.y >= H - BALL then
    ball.y, ball.vy = H - BALL, -math.abs(ball.vy)
  end

  local level = function(paddleY) return ball.y + BALL >= paddleY and ball.y <= paddleY + PADDLE_H end
  if ball.vx < 0 and ball.x <= PLAYER_X + PADDLE_W and ball.x >= PLAYER_X - BALL and level(player) then
    ball.x = PLAYER_X + PADDLE_W
    bounce(player, 1)
  elseif ball.vx > 0 and ball.x + BALL >= RIVAL_X and ball.x <= RIVAL_X + PADDLE_W and level(rival) then
    ball.x = RIVAL_X - BALL
    bounce(rival, -1)
  elseif ball.x < -BALL then
    point("rival")
  elseif ball.x > W then
    point("player")
  end
end

function draw()
  for y = 4, H - 8, 12 do gfx.rect(W / 2 - 1, y, 2, 6, "#444444") end -- the net
  gfx.text(W / 2 - 14, 4, tostring(score.player), "white", "medium", "right")
  gfx.text(W / 2 + 14, 4, tostring(score.rival), "white", "medium")
  gfx.rect(PLAYER_X, player, PADDLE_W, PADDLE_H, "white")
  gfx.rect(RIVAL_X, rival, PADDLE_W, PADDLE_H, "white")
  if state == "playing" or state == "paused" then gfx.rect(ball.x, ball.y, BALL, BALL, "white", 3) end

  local big, small
  if state == "start" then big, small = "PADDLE", "Press center to start"
  elseif state == "serve" then big = "Ready?"
  elseif state == "paused" then big, small = "Paused", "Center to carry on"
  elseif state == "over" then big, small = message, "Press center to play again" end
  if big then gfx.text(W / 2, H / 2 - (small and 22 or 12), big, "white", "loud", "center") end
  if small then gfx.text(W / 2, H / 2 + 6, small, "gray", "small", "center") end
end

function button(name)
  if name == "center" then
    if state == "start" or state == "over" then newGame()
    elseif state == "playing" then state = "paused"
    elseif state == "paused" then state = "playing" end
    return true
  end
  -- Up and down move the paddle during a point. Otherwise they switch cards as usual.
  return state == "playing" or state == "serve"
end
