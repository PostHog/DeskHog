-- Rock, paper, scissors against the hedgehog. The three buttons are the three
-- hands, top to bottom. First to three wins the match.
--
-- All three buttons belong to the game while a match is on. To leave in the
-- middle, hold centre and press up.

plugin = {
  name = "Rock paper scissors",
  description = "One button per hand. First to three beats the hedgehog",
  author = "DeskHog",
  version = "1.0",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local HANDS = { "Rock", "Paper", "Scissors" } -- each beats the one before it
local BUTTONS = { up = 1, center = 2, down = 3 }
local BEAT, REVEAL = 0.3, 1.7 -- seconds per word of the count, and to look at the result
local TARGET = 3

local state = "start" -- "start", "choose", "count", "reveal" or "over"
local mine, theirs, outcome
local you, hog = 0, 0
local timer = 0
local won, lost = 0, 0 -- matches, kept
local shown

-- A hand drawn around cx, cy; s is about half its size.
local function hand(kind, cx, cy, s)
  if kind == 1 then
    gfx.circle(cx, cy, s, "#8d8d8d")
    gfx.circle(cx - s * 0.3, cy - s * 0.3, s * 0.3, "#b5b5b5")
  elseif kind == 2 then
    gfx.rect(cx - s * 0.75, cy - s, s * 1.5, s * 2, "white", 2)
    for i = -1, 1 do
      gfx.line(cx - s * 0.45, cy + i * s * 0.45, cx + s * 0.45, cy + i * s * 0.45, "#9aa5ad", 1)
    end
  else
    local width = math.max(2, s // 5)
    gfx.line(cx - s * 0.4, cy + s * 0.45, cx + s, cy - s * 0.7, "#cfd8dc", width)
    gfx.line(cx - s * 0.4, cy - s * 0.45, cx + s, cy + s * 0.7, "#cfd8dc", width)
    for side = -1, 1, 2 do
      gfx.circle(cx - s * 0.65, cy + side * s * 0.55, s * 0.38, "red")
      gfx.circle(cx - s * 0.65, cy + side * s * 0.55, s * 0.18, "black")
    end
  end
end

local function newMatch()
  you, hog = 0, 0
  state = "choose"
end

function load()
  won, lost = store.get("won", 0), store.get("lost", 0)
end

function update(dt)
  if state == "count" or state == "reveal" then
    timer = timer - dt
    if timer <= 0 then
      if state == "count" then
        state, timer = "reveal", REVEAL
        if outcome == 1 then you = you + 1 elseif outcome == 2 then hog = hog + 1 end
        led.set(outcome == 1 and "green" or outcome == 2 and "red" or "yellow", 0.6)
      elseif you >= TARGET or hog >= TARGET then
        state = "over"
        if you > hog then won = won + 1 else lost = lost + 1 end
        store.set("won", won)
        store.set("lost", lost)
      else
        state = "choose"
      end
    end
  end
  local now = state .. (state == "count" and math.ceil(timer / BEAT) or "")
  if now == shown then return false end
  shown = now
end

local function score(y)
  gfx.text(W / 2, y, "You " .. you .. " - " .. hog .. " Max", "gray", "small", "center")
end

function draw()
  if state == "count" then
    local word = HANDS[math.max(1, math.min(3, 4 - math.ceil(timer / BEAT)))]
    score(8)
    gfx.text(W / 2, 54, word .. "...", "white", "loud", "center")
    return
  end
  if state == "reveal" then
    score(6)
    gfx.text(58, 26, "You", "gray", "small", "center")
    gfx.text(W - 58, 26, "Max", "gray", "small", "center")
    hand(mine, 58, 72, 22)
    hand(theirs, W - 58, 72, 22)
    local text = outcome == 1 and "You win" or outcome == 2 and "Max wins" or "Draw"
    local color = outcome == 1 and "green" or outcome == 2 and "red" or "yellow"
    gfx.text(W / 2, 108, text, color, "medium", "center")
    return
  end

  -- The three hands beside their buttons, and what is going on to the right.
  local height = (H - 16) / 3
  for i, name in ipairs(HANDS) do
    local y = 6 + (i - 1) * (height + 2)
    gfx.rect(6, y, 102, height, "#1c2330", 6)
    hand(i, 25, y + height / 2, 10)
    gfx.text(44, y + height / 2 - 8, name, "white", "small")
  end
  local x = 112 + (W - 112) / 2
  if state == "choose" then
    gfx.text(x, 30, you .. " - " .. hog, "white", "large", "center")
    gfx.text(x, 76, "You against Max", "gray", "small", "center")
    gfx.text(x, 96, "Pick a hand", "white", "small", "center")
  elseif state == "over" then
    local youWon = you > hog
    gfx.text(x, 20, youWon and "You won!" or "Max won", youWon and "green" or "red", "medium", "center")
    gfx.text(x, 44, you .. " - " .. hog, "white", "small", "center")
    gfx.text(x, 70, "Matches " .. won .. " - " .. lost, "gray", "small", "center")
    gfx.text(x, 96, "Centre: again", "gray", "small", "center")
  else
    gfx.text(x, 22, "First to", "white", "medium", "center")
    gfx.text(x, 42, "three", "white", "medium", "center")
    gfx.text(x, 74, "Centre to start", "gray", "small", "center")
    if won + lost > 0 then gfx.text(x, 96, "Matches " .. won .. " - " .. lost, "gray", "small", "center") end
  end
end

function button(name)
  if state == "choose" then
    mine, theirs = BUTTONS[name], math.random(3)
    local difference = (mine - theirs) % 3
    outcome = difference == 1 and 1 or difference == 2 and 2 or 0 -- 1: you, 2: the hedgehog, 0: a draw
    state, timer = "count", 3 * BEAT
    return true
  end
  if state == "count" or state == "reveal" then return true end -- too late to change your mind
  if name ~= "center" then return false end
  newMatch()
  return true
end
