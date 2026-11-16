-- Snake. Up turns left and down turns right, as the snake sees it. Centre
-- starts and pauses. The edges wrap around; only your own tail ends the game.
--
-- Shows drawing a lot with few calls: the snake is drawn as one rectangle per
-- straight stretch, not one per segment, which keeps the game quick.

plugin = {
  name = "Snake",
  description = "Two buttons to steer, one to pause. Eat, grow, don't bite yourself",
  author = "DeskHog",
  version = "1.2",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local CELL = 9
local COLS, ROWS = W // CELL, H // CELL
local LEFT, TOP = (W - COLS * CELL) // 2, (H - ROWS * CELL) // 2
-- Right, down, left, up: turning right is the next one, left the one before.
local DIRECTIONS = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } }

local state = "start" -- "start", "playing", "paused" or "over"
local snake, heading, turn, apple, score, wait
local best = 0

local function placeApple()
  repeat
    apple = { x = math.random(0, COLS - 1), y = math.random(0, ROWS - 1) }
    local free = true
    for _, part in ipairs(snake) do
      if part.x == apple.x and part.y == apple.y then free = false end
    end
  until free
end

local function newGame()
  local y = ROWS // 2
  snake = { { x = 8, y = y }, { x = 7, y = y }, { x = 6, y = y }, { x = 5, y = y } } -- head first
  heading, turn, score, wait = 1, 0, 0, 0
  placeApple()
  state = "playing"
end

local function step()
  heading = (heading - 1 + turn) % 4 + 1
  turn = 0
  local direction = DIRECTIONS[heading]
  local head = { x = (snake[1].x + direction[1]) % COLS, y = (snake[1].y + direction[2]) % ROWS }
  local ate = head.x == apple.x and head.y == apple.y
  if not ate then table.remove(snake) end -- the tail moves on, so its square is free
  for _, part in ipairs(snake) do
    if part.x == head.x and part.y == head.y then
      state = "over"
      if score > best then
        best = score
        store.set("best", best)
      end
      led.set("red", 2)
      return
    end
  end
  table.insert(snake, 1, head)
  if ate then
    score = score + 1
    led.set("green", 0.2)
    placeApple()
  end
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state ~= "playing" then return false end
  wait = wait + dt
  local pace = 1 / math.min(12, 5 + score * 0.25) -- seconds a square; quicker as it grows
  if wait < pace then return false end
  wait = wait - pace
  step()
end

function draw()
  if snake then
    gfx.rect(LEFT + apple.x * CELL + 1, TOP + apple.y * CELL + 1, CELL - 2, CELL - 2, "red", 3)
    -- One rectangle per straight stretch of snake.
    local from = 1
    for i = 2, #snake + 1 do
      local a, b, first = snake[i - 1], snake[i], snake[from]
      local straight = b and ((b.y == first.y and a.y == first.y and math.abs(b.x - a.x) == 1)
        or (b.x == first.x and a.x == first.x and math.abs(b.y - a.y) == 1))
      if not straight then
        local x, y = math.min(first.x, a.x), math.min(first.y, a.y)
        gfx.rect(LEFT + x * CELL + 1, TOP + y * CELL + 1,
          (math.abs(first.x - a.x) + 1) * CELL - 2, (math.abs(first.y - a.y) + 1) * CELL - 2,
          from == 1 and "#7bed9f" or "green", 2)
        from = i - 1 -- corners belong to both stretches
        if from == #snake then break end
      end
    end
    gfx.text(W - 4, 2, tostring(score), "white", "small", "right")
  end

  local big, small
  if state == "start" then big, small = "SNAKE", "Up turns left, down turns right"
  elseif state == "paused" then big, small = "Paused", "Centre to carry on"
  elseif state == "over" then big, small = "Ouch", "Score " .. score .. ", best " .. best end
  if big then
    gfx.rect(20, 34, W - 40, 66, "black", 6)
    gfx.outline(20, 34, W - 40, 66, "#444444")
    gfx.text(W / 2, 42, big, "white", "loud", "center")
    gfx.text(W / 2, 72, small, "gray", "small", "center")
  end
end

function button(name)
  if name == "center" then
    if state == "playing" then state = "paused"
    elseif state == "paused" then state = "playing"
    else newGame() end
    return true
  end
  if state ~= "playing" then return false end -- up and down switch cards when no game is on
  turn = name == "up" and -1 or 1
  return true
end
