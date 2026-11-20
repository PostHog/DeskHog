-- Tally: count things through the day. Coffees, interruptions, bugs fixed.
-- Add a card per thing you count.
--
-- Press centre to add one. Hold centre for a second to take one off again.
-- The count starts from zero every day, and the last 14 days are drawn along
-- the bottom. Give it a daily goal to work towards, or a limit to stay under.
--
-- Shows several cards sharing one store, a press that does something else
-- when held, and keeping daily totals across restarts and days off.

plugin = {
  name = "Tally",
  description = "Count things through the day, with a goal or a limit and the last two weeks",
  author = "DeskHog",
  version = "1.0",
  refresh = 60, -- notice midnight while another card is on screen
}

-- Shared by all counters.
config = {
  offset = { label = "Hours ahead of UTC, so the day starts at midnight", default = "0", order = 1 },
}

local W = gfx.width
local KEEP = 60        -- days of totals to keep
local HOLD_MS = 1000   -- hold centre this long to take one off
local BUMP_MS = 450    -- how long the number jumps after a press

local COLOURS = {
  orange = "#f7a531", blue = "#4d8bff", green = "#3fcf6a", purple = "#a879ff",
  pink = "#ff6fae", teal = "#2ec4b6", yellow = "#ffd23f",
}
local AMBER, RED = "#ffb000", "#ff4d4d"

-- The local day number (days since 1970), or nil while the clock is not set.
local function today()
  local now = time.now()
  if now == 0 then return nil end
  return (now + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)) // 86400
end

local function key(card) return "c" .. card.id end

-- card.data = { day = <local day>, n = { today, yesterday, ... } }. Days the
-- device was off are zeros. Counts made before the clock was set have no day
-- yet and are given the first day the clock is known.
local function save(card)
  local n = {}
  for i = 1, KEEP do n[i] = card.data.n[i] or 0 end
  while #n > 1 and n[#n] == 0 do n[#n] = nil end -- old zeros take no room
  card.data.n = n
  store.set(key(card), card.data)
end

local function count(card, i) return card.data.n[i] or 0 end

local function anything(card)
  for i = 1, KEEP do
    if count(card, i) > 0 then return true end
  end
  return false
end

-- Move on to a new day if one has started.
local function catchUp(card)
  if not card.data then
    local saved = store.get(key(card), nil)
    if type(saved) ~= "table" or type(saved.n) ~= "table" then saved = { n = {} } end
    card.data = { day = tonumber(saved.day), n = {} }
    for i = 1, KEEP do card.data.n[i] = math.max(0, math.floor(tonumber(saved.n[i]) or 0)) end
  end
  local day = today()
  if not day then return end
  local data = card.data
  if not data.day then
    data.day = day
    if anything(card) then save(card) end -- nothing counted yet: nothing to write
  elseif day > data.day then
    -- A clock that goes back (it should not) leaves the counts where they are.
    local gap = math.min(day - data.day, KEEP)
    for _ = 1, gap do table.insert(data.n, 1, 0) end
    data.day = day
    if anything(card) then save(card) end
  end
end

local function colour(card) return COLOURS[card.config.colour] or COLOURS.orange end

local function change(card, by)
  catchUp(card)
  local now = count(card, 1)
  if now + by < 0 then
    card.bump = { at = time.ms(), dir = 0 }
    return
  end
  card.data.n[1] = now + by
  save(card)
  card.bump = { at = time.ms(), text = by > 0 and "+1" or "-1", dir = by }
  if by > 0 then
    local limit = card.config.limit
    led.set(limit > 0 and now + by > limit and RED or colour(card), 0.3)
  end
end

-- A colour part of the way to black, for fading.
local function dim(hex, amount)
  local r, g, b = tonumber(hex:sub(2, 3), 16), tonumber(hex:sub(4, 5), 16), tonumber(hex:sub(6, 7), 16)
  local keep = 1 - amount
  return string.format("#%02x%02x%02x", math.floor(r * keep), math.floor(g * keep), math.floor(b * keep))
end

local function chart(card, top, bottom)
  local goal, limit = card.config.goal, card.config.limit
  local most = math.max(1, goal, limit)
  for i = 1, 14 do most = math.max(most, count(card, i)) end
  local height = bottom - top
  local stride = (W - 16) / 14
  local barWidth = math.floor(stride) - 4
  local main = colour(card)
  for i = 1, 14 do
    local value = count(card, 15 - i)  -- oldest on the left, today on the right
    local x = 8 + (i - 1) * stride + 2
    local h = math.max(2, math.floor(value / most * height + 0.5))
    local fill
    if i == 14 then fill = main
    elseif value == 0 then fill = "#333333"
    else fill = dim(main, 0.6) end
    if limit > 0 and value > limit then fill = i == 14 and RED or dim(RED, 0.45) end
    gfx.rect(x, bottom - h, barWidth, h, fill, 2)
  end
  for _, line in ipairs({ goal, limit }) do
    if line > 0 then
      local y = bottom - math.floor(line / most * height + 0.5)
      for x = 8, W - 8, 6 do gfx.line(x, y, x + 2, y, "#888888") end
    end
  end
end

cards = {
  counter = {
    name = "Counter",

    config = {
      name = { label = "What you count", default = "Coffees", order = 1 },
      goal = { label = "Daily goal to reach (0 for none)", default = 0, order = 2 },
      limit = { label = "Daily limit to stay under (0 for none)", default = 0, order = 3 },
      colour = { label = "Colour", default = "orange", options = { "orange", "blue", "green", "purple", "pink", "teal", "yellow" }, order = 4 },
    },

    load = function(card)
      card.title = card.config.name
      catchUp(card)
    end,

    update = function(card)
      local now = time.ms()
      local busy = false
      if card.pressAt then
        busy = true
        if now - (card.seenAt or now) > 300 then
          card.pressAt = nil -- we were off screen: centre was held to go back a card
        elseif input.held("up") or input.held("down") then
          card.pressAt = nil
        elseif input.held("center") then
          if now - card.pressAt >= HOLD_MS then
            card.pressAt = nil
            change(card, -1)
          end
        else
          card.pressAt = nil -- let go before the hold counted: a press
          change(card, 1)
        end
      end
      card.seenAt = now
      if card.bump and now - card.bump.at < BUMP_MS + 100 then busy = true end
      local day = today()
      if card.data and day and card.data.day and day > card.data.day then
        catchUp(card)
        busy = true
      end
      if not busy then return false end
    end,

    draw = function(card)
      catchUp(card)
      local main = colour(card)
      local goal, limit = card.config.goal, card.config.limit
      local n = count(card, 1)

      gfx.text(8, 5, card.config.name, "white", "medium")

      -- The number, jumping when it changes.
      local age = card.bump and time.ms() - card.bump.at or BUMP_MS
      local t = math.min(1, age / BUMP_MS)
      local lift, shake = 0, 0
      if card.bump and t < 1 then
        if card.bump.dir == 0 then -- nothing left to take off: shake the head
          shake = math.floor(math.sin(t * 25) * 4 * (1 - t) + 0.5)
        else
          lift = math.floor(-10 * (1 - t) * (1 - t) * card.bump.dir)
        end
      end
      local numberColour = "white"
      if limit > 0 and n > limit then numberColour = RED
      elseif limit > 0 and n == limit then numberColour = AMBER
      elseif goal > 0 and n >= goal then numberColour = main end
      local text = tostring(n)
      gfx.text(8 + shake, 28 + lift, text, numberColour, "large")
      local after = 8 + gfx.textWidth(text, "large") + 6
      if goal > 0 then
        gfx.text(after, 45, "/ " .. goal, n >= goal and main or "gray", "medium")
      elseif limit > 0 then
        gfx.text(after, 45, "max " .. limit, n >= limit and numberColour or "gray", "small")
      end
      if card.bump and t < 1 and card.bump.dir ~= 0 then
        local where = after + (goal > 0 and gfx.textWidth("/ " .. goal, "medium") + 6 or 0)
        local fade = card.bump.dir >= 0 and main or "#bbbbbb"
        gfx.text(math.min(where, 118), 30 - math.floor(t * 12), card.bump.text, dim(fade, t * 0.85), "medium")
      end

      -- Yesterday and the last seven days, or what to do.
      local x, right = 132, W - 8
      if card.pressAt and time.ms() - card.pressAt > 200 then
        local held = math.min(1, (time.ms() - card.pressAt) / HOLD_MS)
        gfx.text(x, 30, "Hold to take", "gray", "small")
        gfx.text(x, 46, "one off", "gray", "small")
        gfx.rect(x, 68, right - x, 4, "#333333", 2)
        gfx.rect(x, 68, (right - x) * held, 4, RED, 2)
      else
        local week = 0
        for i = 1, 7 do week = week + count(card, i) end
        if not anything(card) then
          gfx.text(x, 30, "Press centre", "gray", "small")
          gfx.text(x, 46, "to count one", "gray", "small")
        else
          gfx.text(x, 30, "yesterday", "gray", "small")
          gfx.text(right, 30, tostring(count(card, 2)), "white", "small", "right")
          gfx.text(x, 48, "7 days", "gray", "small")
          gfx.text(right, 48, tostring(week), "white", "small", "right")
        end
      end

      chart(card, 82, 129)
    end,

    button = function(card, name)
      if name ~= "center" then return false end
      card.pressAt = time.ms()
      card.seenAt = time.ms()
      return true
    end,
  },
}
