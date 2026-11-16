-- Blocks. Pairs of coloured blocks fall into a well. Three or more of one
-- colour that touch disappear, and what was resting on them falls, which can
-- set off the next group. The game ends when the well is full.
--
-- Made for three buttons: up moves the pair one column along (and back to the
-- first after the last), centre swaps its two colours, down drops it.
--
-- Shows a grid game, and working through chain reactions a step at a time so
-- each one is seen. To leave in the middle, hold centre and press up.

plugin = {
  name = "Blocks",
  description = "Falling pairs of colours: bring three together and they are gone",
  author = "Tim Broddin",
  version = "1.1",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local COLS, ROWS, CELL = 6, 9, 14
local LEFT, TOP = 8, (H - ROWS * CELL) // 2
local COLORS = { "#e74c3c", "#f1c40f", "#2ecc71", "#3498db", "#9b59b6" }
local FLASH = 0.2 -- seconds a group lights up before it goes

local state = "start" -- "start", "falling", "clearing" or "over"
local grid            -- grid[column][row], row 1 at the top; a colour number or nil
local pair, coming    -- pair: {column, row of the lower block, top, bottom}
local group, chain    -- the blocks that are lighting up, and how many groups in a row this is
local wait, score, cleared = 0, 0, 0
local best = 0

local function colours() return score >= 400 and 5 or 4 end -- a fifth colour joins later

local function newPair() return { top = math.random(colours()), bottom = math.random(colours()) } end

local function free(column, row)
  return row >= 1 and row <= ROWS and not grid[column][row]
end

local function spawn()
  pair, coming = coming, newPair()
  pair.column, pair.row = COLS // 2, 2
  if not (free(pair.column, 1) and free(pair.column, 2)) then
    pair, state = nil, "over"
    if score > best then
      best = score
      store.set("best", best)
    end
    led.set("red", 1.5)
    return
  end
  state, wait = "falling", 0
end

-- Every block that belongs to a group of three or more touching blocks of its colour.
local function findGroups()
  local seen, found = {}, {}
  for column = 1, COLS do
    for row = 1, ROWS do
      local colour = grid[column][row]
      if colour and not seen[column * 16 + row] then
        local members, todo = {}, { { column, row } }
        seen[column * 16 + row] = true
        while #todo > 0 do
          local at = table.remove(todo)
          members[#members + 1] = at
          for _, step in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
            local c, r = at[1] + step[1], at[2] + step[2]
            if c >= 1 and c <= COLS and r >= 1 and r <= ROWS and grid[c][r] == colour and not seen[c * 16 + r] then
              seen[c * 16 + r] = true
              todo[#todo + 1] = { c, r }
            end
          end
        end
        if #members >= 3 then
          for _, member in ipairs(members) do found[#found + 1] = member end
        end
      end
    end
  end
  return found
end

local function settle() -- let every block fall as far as it can
  for column = 1, COLS do
    local to = ROWS
    for row = ROWS, 1, -1 do
      local colour = grid[column][row]
      if colour then
        grid[column][row] = nil
        grid[column][to] = colour
        to = to - 1
      end
    end
  end
end

local function check()
  group = findGroups()
  if #group > 0 then
    chain = chain + 1
    state, wait = "clearing", FLASH
    led.set(chain > 1 and "yellow" or "green", FLASH)
  else
    spawn()
  end
end

local function land()
  grid[pair.column][pair.row] = pair.bottom
  grid[pair.column][pair.row - 1] = pair.top
  pair, chain = nil, 0
  check()
end

local function newGame()
  grid = {}
  for column = 1, COLS do grid[column] = {} end
  score, cleared, chain = 0, 0, 0
  coming = newPair()
  spawn()
end

function load()
  best = store.get("best", 0)
end

function update(dt)
  if state == "falling" then
    wait = wait + dt
    local pace = math.max(0.14, 0.62 - cleared * 0.006) -- seconds a row; quicker as the game goes on
    if wait < pace then return false end
    wait = 0
    if free(pair.column, pair.row + 1) then pair.row = pair.row + 1 else land() end
  elseif state == "clearing" then
    wait = wait - dt
    if wait > 0 then return false end
    for _, at in ipairs(group) do grid[at[1]][at[2]] = nil end
    score = score + #group * 10 * chain -- a chain reaction counts double, triple and so on
    cleared = cleared + #group
    settle()
    check()
  else
    return false
  end
end

local function block(column, row, colour)
  gfx.rect(LEFT + (column - 1) * CELL + 1, TOP + (row - 1) * CELL + 1, CELL - 2, CELL - 2, colour, 3)
end

function draw()
  gfx.outline(LEFT - 2, TOP - 2, COLS * CELL + 4, ROWS * CELL + 4, "#444444")
  if grid then
    for column = 1, COLS do
      for row = 1, ROWS do
        if grid[column][row] then block(column, row, COLORS[grid[column][row]]) end
      end
    end
    if state == "clearing" then
      for _, at in ipairs(group) do block(at[1], at[2], "white") end
    end
    if pair then
      block(pair.column, pair.row - 1, COLORS[pair.top])
      block(pair.column, pair.row, COLORS[pair.bottom])
    end
  end

  local x = LEFT + COLS * CELL + 10
  local middle = x + (W - x) / 2
  if state == "start" then
    gfx.text(middle, 8, "BLOCKS", "white", "loud", "center")
    gfx.text(x, 40, "Up: next column", "gray", "small")
    gfx.text(x, 58, "Centre: swap", "gray", "small")
    gfx.text(x, 76, "Down: drop", "gray", "small")
    gfx.text(x, 104, best > 0 and "Best " .. best .. ". Centre" or "Centre to start", "white", "small")
    return
  end
  gfx.text(x, 4, "Score", "gray", "small")
  gfx.text(x, 20, tostring(score), "white", "large")
  if state == "over" then
    gfx.text(x, 66, "The well is full", "red", "small")
    gfx.text(x, 86, "Best " .. best, "gray", "small")
    gfx.text(x, 106, "Centre: again", "gray", "small")
    return
  end
  if chain > 1 and state == "clearing" then gfx.text(x, 66, "Chain x" .. chain, "yellow", "medium") end
  gfx.text(x, 96, "Next", "gray", "small")
  gfx.rect(x + 40, 88, CELL - 2, CELL - 2, COLORS[coming.top], 3)
  gfx.rect(x + 40, 88 + CELL, CELL - 2, CELL - 2, COLORS[coming.bottom], 3)
end

function button(name)
  if state == "start" or state == "over" then
    if name ~= "center" then return false end -- up and down switch cards when no game is on
    newGame()
    return true
  end
  if state ~= "falling" then return true end -- a group is going: wait for it
  if name == "center" then
    pair.top, pair.bottom = pair.bottom, pair.top
  elseif name == "up" then
    -- The next column along that has room at this height, going round at the end.
    for step = 1, COLS - 1 do
      local column = (pair.column - 1 + step) % COLS + 1
      if free(column, pair.row) and free(column, pair.row - 1) then
        pair.column = column
        break
      end
    end
  else
    while free(pair.column, pair.row + 1) do pair.row = pair.row + 1 end
    land()
  end
  return true
end
