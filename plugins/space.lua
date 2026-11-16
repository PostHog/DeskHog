-- Two cards about what is up there: how far away the International Space
-- Station is right now and in which direction, and a countdown to the next
-- rocket launch anywhere in the world.
--
-- The station's position comes from wheretheiss.at, the launches from
-- RocketLaunch.Live, and your city's coordinates from open-meteo.com. All
-- three are free and need no key.

plugin = {
  name = "Space",
  description = "How far away the space station is right now, and a countdown to the next rocket launch",
  author = "DeskHog",
  version = "1.3",
  refresh = 30,
  fps = 1,
}

local DIRECTIONS = { "north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west" }

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

-- Distance in km along the ground and compass direction from one place to another.
local function towards(fromLat, fromLon, toLat, toLon)
  local rad = math.pi / 180
  local lat1, lat2, dLon = fromLat * rad, toLat * rad, (toLon - fromLon) * rad
  local a = math.sin((lat2 - lat1) / 2) ^ 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dLon / 2) ^ 2
  local distance = 6371 * 2 * math.atan(math.sqrt(a), math.sqrt(1 - a))
  local bearing = math.atan(math.sin(dLon) * math.cos(lat2),
    math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(dLon)) / rad
  return distance, DIRECTIONS[math.floor((bearing % 360) / 45 + 0.5) % 8 + 1]
end

-- Cut a line that is too wide for the card.
local function fit(text)
  if gfx.textWidth(text, "small") <= gfx.width - 16 then return text end
  while #text > 4 and gfx.textWidth(text .. "...", "small") > gfx.width - 16 do text = text:sub(1, -2) end
  return text .. "..."
end

local function thousands(value)
  local text = string.format("%d", math.floor(value + 0.5))
  return (text:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", ""))
end

local function whereIsIt(card)
  http.get("https://api.wheretheiss.at/v1/satellites/25544", function(res)
    local at = res.json
    if not at or not at.latitude then
      if not card.distance then card.problem = res.error or "No word from the station" end
      return
    end
    card.problem = nil
    card.distance, card.direction = towards(card.place.latitude, card.place.longitude, at.latitude, at.longitude)
    card.over = string.format("Over %.0f°%s %.0f°%s, in %s", math.abs(at.latitude), at.latitude >= 0 and "N" or "S",
      math.abs(at.longitude), at.longitude >= 0 and "E" or "W", at.visibility == "daylight" and "daylight" or "the dark")
    -- Within about 1,000 km it is above your horizon.
    local near = card.distance < 1000
    if near and not card.near then led.set("blue", 20) end
    card.near = near
  end)
end

-- "3 days", "5h 12m" or "12:05" until a moment given as Unix time.
local function countdown(at)
  local seconds = at - time.now()
  if seconds <= 0 then return "Lift-off" end
  if seconds >= 172800 then return seconds // 86400 .. " days" end
  if seconds >= 3600 then return string.format("%dh %02dm", seconds // 3600, seconds % 3600 // 60) end
  return string.format("%d:%02d", seconds // 60, seconds % 60)
end

cards = {
  station = {
    name = "Space station",
    config = {
      city = { label = "Where you are (a city)", default = "Antwerp" },
    },
    load = function(card)
      if card.place then return whereIsIt(card) end
      http.get("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" .. urlencode(card.config.city), function(res)
        card.place = res.json and res.json.results and res.json.results[1]
        if not card.place then
          card.problem = res.ok and "No city called " .. card.config.city or (res.error or "No connection")
          return
        end
        whereIsIt(card)
      end)
    end,
    update = function(card)
      if card.distance == card.shown then return false end
      card.shown = card.distance
    end,
    draw = function(card)
      ui.title("Space station")
      if card.problem or not card.distance then
        ui.label(card.problem or "Looking up...", card.problem and "orange" or "gray")
        return
      end
      ui.big(thousands(card.distance) .. " km", card.near and "yellow" or "white")
      ui.label(card.near and "It is above your horizon right now" or "to the " .. card.direction .. " of " .. card.place.name,
        card.near and "yellow" or "gray")
      ui.label(card.over)
    end,
  },

  launch = {
    name = "Next rocket launch",
    load = function(card)
      -- Launches change slowly: ask every half hour, and right after one has gone.
      local stale = not card.asked or time.ms() - card.asked > 1800000
      local gone = card.at and time.now() > card.at + 600
      if not stale and not gone then return end
      card.asked = time.ms()
      local wanted = { result = { { name = true, sort_date = true, provider = { name = true }, vehicle = { name = true },
        pad = { location = { name = true } } } } }
      http.get("https://fdo.rocketlaunch.live/json/launches/next/1", { only = wanted }, function(res)
        local launch = res.json and res.json.result and res.json.result[1]
        if not launch then
          if not card.at then card.problem = res.error or "No launches listed" end
          return
        end
        card.problem = nil
        card.at = tonumber(launch.sort_date) or 0
        card.rocket = fit((launch.vehicle and launch.vehicle.name or "A rocket") .. ", " .. tostring(launch.name))
        card.from = fit((launch.provider and launch.provider.name .. ", " or "")
          .. (launch.pad and launch.pad.location and launch.pad.location.name or ""))
      end)
    end,
    update = function(card)
      local text = card.at and time.now() > 0 and countdown(card.at) or card.problem or ""
      if text == card.shown then return false end
      card.shown = text
    end,
    draw = function(card)
      ui.title("Launch, by RocketLaunch.Live")
      if card.problem or not card.at then
        ui.label(card.problem or "Looking up...", card.problem and "orange" or "gray")
        return
      end
      ui.big(time.now() > 0 and countdown(card.at) or "...")
      ui.label(card.rocket)
      ui.label(card.from)
    end,
  },
}
