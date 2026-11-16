-- A hedgehog that lives on your desk. He gets hungry and bored as the day
-- goes by, sleeps at night, and remembers how you treated him.
--
-- Press centre to give him what he wants: food, a game, or just a pat.
--
-- Shows keeping state across restarts with store, and working out what
-- happened while the device was off from time.now().

plugin = {
  name = "Pet hedgehog",
  description = "A hedgehog that gets hungry and bored, sleeps at night and likes a pat",
  author = "DeskHog",
  version = "1.2",
  fps = 4,
  refresh = 60, -- he also gets hungry while another card is showing
}

config = {
  name = { label = "His name", default = "Max", order = 1 },
  offset = { label = "Hours ahead of UTC, so he sleeps at night", default = "0", order = 2 },
  light = { label = "Light the LED when he needs something", default = true, order = 3 },
}

local W, H = gfx.width, gfx.height
local HOURS_TO_STARVE, HOURS_TO_BORE = 8, 6 -- from full to empty, awake

local pet       -- { food, fun, born, seen }: food and fun run from 0 to 100
local reaction  -- what he just did, and until when: { text, untilMs }
local blink = 0
local shown

local function clamp(value) return math.max(0, math.min(100, value)) end

local function localHour()
  local now = time.now()
  if now == 0 then return 12 end -- clock not set yet: call it midday
  local seconds = now + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)
  return seconds % 86400 // 3600
end

local function asleep()
  local hour = localHour()
  return hour >= 22 or hour < 7
end

-- Let the time since we last looked take its toll.
local function age()
  local now = time.now()
  if now == 0 then return end
  local hours = math.max(0, now - (pet.seen or now)) / 3600
  pet.seen = now
  -- Being off or asleep for the night costs him at most a normal day.
  hours = math.min(hours, 16)
  if hours > 0 and not asleep() then
    pet.food = clamp(pet.food - hours * 100 / HOURS_TO_STARVE)
    pet.fun = clamp(pet.fun - hours * 100 / HOURS_TO_BORE)
  end
end

local function mood()
  if asleep() then return "asleep" end
  if pet.food < 30 then return "hungry" end
  if pet.fun < 30 then return "bored" end
  return "happy"
end

local function save()
  store.set("pet", pet)
end

function load()
  if not pet then
    pet = store.get("pet", nil)
    if type(pet) ~= "table" or type(pet.food) ~= "number" or type(pet.fun) ~= "number" then
      pet = { food = 80, fun = 80, born = time.now() }
    end
  end
  local before = mood()
  age()
  if mood() ~= before or not pet.saved or time.now() - pet.saved > 1800 then
    pet.saved = time.now()
    save() -- not on every look: writing wears the storage
  end
  if config.light then
    local feeling = mood()
    if feeling == "hungry" or feeling == "bored" then led.set("orange") else led.off() end
  end
end

function update(dt)
  blink = (blink + dt) % 4
  local feeling = pet and mood() or ""
  local now = feeling .. (reaction and reaction.text or "") .. tostring(blink > 3.8)
    .. (pet and (pet.food // 2) .. (pet.fun // 2) or "")
  if reaction and time.ms() > reaction.untilMs then reaction = nil end
  if now == shown then return false end
  shown = now
end

local function bar(x, y, label, value, color)
  gfx.text(x, y, label, "gray", "small")
  gfx.outline(x + 38, y + 4, 62, 9, "#555555")
  gfx.rect(x + 40, y + 6, 58 * value / 100, 5, value < 30 and "orange" or color)
end

function draw()
  if not pet then return end
  local feeling = mood()
  local cx, cy = 58, 66

  -- Spikes, then the face in front of them.
  for i = 0, 10 do
    local angle = math.pi * (1 + i / 10)
    gfx.line(cx, cy, cx + math.cos(angle) * 50, cy + math.sin(angle) * 46, "#8a5a36", 4)
  end
  gfx.circle(cx, cy, 36, "#6b4226")
  gfx.circle(cx, cy + 12, 25, "#f1c27d")
  gfx.circle(cx - 17, cy + 40, 6, "#6b4226")
  gfx.circle(cx + 17, cy + 40, 6, "#6b4226")

  local closed = feeling == "asleep" or blink > 3.8
  for _, side in ipairs({ -1, 1 }) do
    local x = cx + side * 10
    if closed then
      gfx.line(x - 4, cy + 7, x + 4, cy + 7, "black", 2)
    else
      gfx.circle(x, cy + 6, 3, "black")
    end
    if feeling == "happy" or reaction then gfx.circle(cx + side * 17, cy + 17, 3, "#f5a5a5") end
  end
  gfx.circle(cx, cy + 15, 3, "#4a2c17") -- nose
  if feeling == "hungry" then
    gfx.circle(cx, cy + 25, 4, "#4a2c17") -- mouth open
  elseif feeling == "bored" then
    gfx.line(cx - 5, cy + 25, cx + 5, cy + 25, "#4a2c17", 2)
  elseif feeling == "happy" then
    gfx.line(cx - 6, cy + 23, cx, cy + 27, "#4a2c17", 2)
    gfx.line(cx, cy + 27, cx + 6, cy + 23, "#4a2c17", 2)
  end

  local x = 122
  gfx.text(x, 8, config.name, "white", "medium")
  local says = reaction and reaction.text
    or feeling == "asleep" and "Zzz..."
    or feeling == "hungry" and "I'm hungry"
    or feeling == "bored" and "Play with me"
    or "Feeling fine"
  gfx.text(x, 32, says, feeling == "happy" and "green" or feeling == "asleep" and "gray" or "orange", "small")
  bar(x, 60, "Food", pet.food, "green")
  bar(x, 80, "Fun", pet.fun, "blue")
  local born = pet.born or 0
  if born > 0 and time.now() > 0 then
    local days = (time.now() - born) // 86400
    gfx.text(x, 108, days == 1 and "1 day old" or days .. " days old", "gray", "small")
  end
end

function button(name)
  if name ~= "center" or not pet then return false end
  local feeling = mood()
  local text
  if feeling == "asleep" then
    text = "Shh. He's asleep"
  elseif feeling == "hungry" then
    pet.food = clamp(pet.food + 45)
    text = "Nom nom nom"
  elseif feeling == "bored" then
    pet.fun = clamp(pet.fun + 45)
    text = "Wheee!"
  else
    pet.fun = clamp(pet.fun + 5)
    text = "He likes that"
  end
  reaction = { text = text, untilMs = time.ms() + 2500 }
  pet.saved = time.now()
  save()
  if config.light and mood() == "happy" then led.set("green", 1) end
  return true
end
