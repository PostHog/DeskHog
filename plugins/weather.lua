-- Current temperature and today's curve for any number of cities, from
-- open-meteo.com (free, no API key).
--
-- This plugin shows how one plugin provides several cards. It has one kind of
-- card, "city". Add a card per city; they all share the unit, which is set once.

plugin = {
  name = "Weather",
  description = "The temperature now and today's curve, for any number of cities",
  author = "DeskHog",
  version = "1.3",
  refresh = 900, -- every card's load() runs again every 15 minutes
}

-- Shared by all of this plugin's cards.
config = {
  unit = { label = "Unit", default = "C", options = { "C", "F" } },
}

local function describe(code)
  if code == 0 then return "Clear" end
  if code <= 2 then return "Partly cloudy" end
  if code == 3 then return "Cloudy" end
  if code <= 48 then return "Fog" end
  if code <= 57 then return "Drizzle" end
  if code <= 67 then return "Rain" end
  if code <= 77 then return "Snow" end
  if code <= 82 then return "Showers" end
  if code <= 86 then return "Snow showers" end
  return "Thunderstorm"
end

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

-- `card` is this card's own table. Besides card.config it holds whatever the
-- plugin puts there: here the place, the temperature and the day's curve.
local function fetchWeather(card)
  local url = string.format(
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
      .. "&current=temperature_2m,weather_code&hourly=temperature_2m&forecast_days=1",
    card.place.latitude, card.place.longitude)
  if config.unit == "F" then url = url .. "&temperature_unit=fahrenheit" end

  http.get(url, function(res)
    local now = res.json and res.json.current
    if not now then
      card.status = "No weather data"
      return
    end
    card.temp = now.temperature_2m
    card.summary = describe(now.weather_code)
    card.hours = res.json.hourly and res.json.hourly.temperature_2m or {}
  end)
end

cards = {
  city = {
    name = "City",

    -- Set per card.
    config = {
      city = { label = "City", default = "Antwerp" },
    },

    load = function(card)
      if card.place then return fetchWeather(card) end -- the city was looked up before
      local url = "https://geocoding-api.open-meteo.com/v1/search?count=1&name=" .. urlencode(card.config.city)
      http.get(url, function(res)
        card.place = res.json and res.json.results and res.json.results[1]
        if not card.place then
          card.status = res.ok and "City not found" or "No connection"
          return
        end
        card.title = card.place.name -- what the web page calls this card
        fetchWeather(card)
      end)
    end,

    draw = function(card)
      ui.title(card.place and card.place.name or card.config.city)
      if not card.temp then
        ui.label(card.status or "Loading...")
        return
      end
      ui.big(math.floor(card.temp + 0.5) .. "°")
      ui.label(card.summary)
      ui.chart(card.hours)
    end,
  },
}
