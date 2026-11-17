-- The time somewhere else: a card per city, with the date there, how far
-- ahead or behind it is, and a sky that says whether they are awake.
--
-- Add a card per city and pick its time zone from the list on the web page.
-- Summer time is part of the zone, there and here, so the difference is right
-- all year.
--
-- Shows a setting with type = "timezone": the web page offers its list of
-- places and the card gets a zone to hand to time.date() and time.format().

plugin = {
  name = "World clock",
  description = "The time in other cities, a card each, with a sky that shows day or night there",
  author = "DeskHog",
  version = "1.0",
  fps = 1, -- look at the time once a second; it is only repainted when the minute changes
}

-- Shared by all of this plugin's cards.
config = {
  hours12 = { label = "12-hour clock", default = false },
}

local W, H = gfx.width, gfx.height

-- The sky by the hour there: background, the colour of the small text, and what is up.
local function sky(hour)
  if hour >= 8 and hour < 18 then return "#1c5a94", "#cfe6fa", "sun" end
  if hour >= 6 and hour < 21 then return "#553a6e", "#e6d3ee", hour < 12 and "sun" or "moon" end -- dawn and dusk
  return "#0a1034", "#9aa6d6", "moon"
end

local function drawSun(x, y)
  for ray = 0, 7 do
    local angle = ray * math.pi / 4
    gfx.line(x + math.cos(angle) * 9, y + math.sin(angle) * 9, x + math.cos(angle) * 12, y + math.sin(angle) * 12,
      "#ffd23f", 2)
  end
  gfx.circle(x, y, 6, "#ffd23f")
end

-- A crescent: a disc with a bite out of it, in the colour of the sky.
local function drawMoon(x, y, background)
  gfx.circle(x, y, 8, "#f1f0e2")
  gfx.circle(x + 4, y - 3, 7, background)
end

-- "+6h", "-3h30", and whether it is another day there.
local function difference(now, zone)
  local here, there = time.date(now), time.date(now, zone)
  local minutes = (there.offset - here.offset) // 60
  if minutes == 0 then return "Same time as here" end
  local text = string.format("%s%dh", minutes < 0 and "-" or "+", math.abs(minutes) // 60)
  if minutes % 60 ~= 0 then text = text .. string.format("%02d", math.abs(minutes) % 60) end
  -- A zone is never more than a day away, so comparing the weekday is enough.
  if there.wday ~= here.wday then text = text .. (minutes > 0 and ", tomorrow" or ", yesterday") end
  return text
end

cards = {
  city = {
    name = "City",

    -- Set per card.
    config = {
      city = { label = "City", default = "Tokyo", order = 1 },
      -- The web page saves the place picked as a POSIX string like this one.
      -- Left empty it is the device's own zone.
      zone = { label = "Time zone", type = "timezone", default = "JST-9", order = 2 },
    },

    load = function(card)
      card.title = card.config.city -- what the web page calls this card
    end,

    update = function(card)
      local minute = time.now() // 60 -- minutes change at the same moment everywhere
      if minute == card.shown then return false end
      card.shown = minute
    end,

    draw = function(card)
      local now = time.now()
      if now == 0 then -- the device has not set its clock yet
        ui.title(card.config.city)
        ui.label("Waiting for the time. It comes from the internet once WiFi is connected.")
        return
      end
      local zone = card.config.zone
      local background, soft, light = sky(time.date(now, zone).hour)
      gfx.clear(background)

      gfx.text(10, 8, card.config.city, "white", "medium")
      if light == "sun" then drawSun(W - 22, 20) else drawMoon(W - 22, 20, background) end

      local clock = time.format("%H:%M", now, zone)
      if config.hours12 then clock = time.format("%-I:%M ", now, zone) .. time.format("%p", now, zone):lower() end
      gfx.text(W / 2, 38, clock, "white", "large", "center")

      gfx.text(10, H - 40, time.format("%A %-d %B", now, zone), soft)
      gfx.text(10, H - 22, difference(now, zone), soft)
      gfx.text(W - 10, H - 22, time.format("%Z", now, zone), soft, "small", "right")
    end,
  },
}
