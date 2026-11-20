-- Current temperature and today's curve for any number of cities, from
-- open-meteo.com (free, no API key).
--
-- The centre button steps through three pages: now, today in numbers, and the
-- coming days. The plugin only says how many pages there are (`pages`) and draws
-- the one it is asked for; the device does the rest.
--
-- This plugin shows how one plugin provides several cards. It has one kind of
-- card, "city". Add a card per city; they all share the unit, which is set once.

plugin = {
  name = "Weather",
  description = "The temperature now, today in numbers and the coming days, for any number of cities",
  author = "DeskHog",
  version = "1.4",
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

local W = gfx.width
local PAD, PAGES = 8, 3
local WINDS = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" }

local function round(n) return math.floor(n + 0.5) end
local function degrees(n) return n and round(n) .. "°" or "–" end

-- "2026-11-21" -> "Sat"
local function dayName(date)
  local y, m, d = tostring(date):match("^(%d+)-(%d+)-(%d+)")
  if not y then return "" end
  return time.format("%a", time.make{ year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = 12 })
end

-- "2026-11-20T08:02" -> "08:02"
local function clock(stamp) return stamp and tostring(stamp):match("T(%d+:%d+)") end

-- `card` is this card's own table. Besides card.config it holds whatever the
-- plugin puts there: here the place, the temperature, the day's curve and the
-- lines of the other two pages.
local function fetchWeather(card)
  local url = string.format(
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
      .. "&current=temperature_2m,weather_code,apparent_temperature,wind_speed_10m,wind_direction_10m"
      .. "&hourly=temperature_2m"
      .. "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
      .. "precipitation_probability_max,sunrise,sunset"
      .. "&forecast_days=6&timezone=auto",
    card.place.latitude, card.place.longitude)
  local imperial = config.unit == "F"
  if imperial then url = url .. "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch" end

  http.get(url, function(res)
    local now = res.json and res.json.current
    if not now then
      card.status = "No weather data"
      return
    end
    card.temp = now.temperature_2m
    card.summary = describe(now.weather_code)

    -- The answer covers six days; the curve is today's 24 hours.
    local hourly = res.json.hourly and res.json.hourly.temperature_2m or {}
    card.hours = {}
    for i = 1, math.min(24, #hourly) do card.hours[i] = hourly[i] end

    card.today, card.days = nil, nil
    local daily = res.json.daily
    if not (daily and daily.time and daily.temperature_2m_max and daily.temperature_2m_min) then return end
    local function of(list, i) return daily[list] and daily[list][i] end

    local rain = "–"
    if of("precipitation_probability_max", 1) then rain = round(of("precipitation_probability_max", 1)) .. "%" end
    if of("precipitation_sum", 1) then
      rain = rain .. string.format(imperial and "  %.2f in" or "  %.1f mm", of("precipitation_sum", 1))
    end
    local wind = "–"
    if now.wind_speed_10m then
      wind = round(now.wind_speed_10m) .. (imperial and " mph" or " km/h")
      if now.wind_direction_10m then
        wind = wind .. " " .. WINDS[round(now.wind_direction_10m / 45) % 8 + 1]
      end
    end
    local rise, set = clock(of("sunrise", 1)), clock(of("sunset", 1))
    card.today = {
      { "High / low", degrees(of("temperature_2m_max", 1)) .. " / " .. degrees(of("temperature_2m_min", 1)) },
      { "Feels like", degrees(now.apparent_temperature) },
      { "Rain", rain },
      { "Wind", wind },
      { "Sun", rise and set and (rise .. " - " .. set) or "–" },
    }

    card.days = {}
    for i = 2, math.min(6, #daily.time) do
      local code = of("weather_code", i)
      card.days[#card.days + 1] = {
        dayName(daily.time[i]),
        code and describe(code) or "",
        degrees(of("temperature_2m_max", i)) .. " / " .. degrees(of("temperature_2m_min", i)),
      }
    end
  end)
end

local function drawRows(card, heading, rows)
  gfx.text(PAD, 6, (card.place and card.place.name or card.config.city) .. heading, "white", "small")
  if not rows or #rows == 0 then
    gfx.text(PAD, 34, "No forecast", "gray", "small")
    return
  end
  for i, row in ipairs(rows) do
    local y = 27 + (i - 1) * 20
    gfx.text(PAD, y, row[1], "gray", "small")
    if row[3] then gfx.text(PAD + 44, y, row[2], "white", "small") end
    gfx.text(W - PAD, y, row[3] or row[2], "white", "small", "right")
  end
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

    -- The centre button steps through the pages, and the device shows which one
    -- is up. There is one page until there is weather to show.
    pages = function(card) return card.temp and PAGES or 1 end,

    draw = function(card, page)
      if page == 2 then return drawRows(card, "  today", card.today) end
      if page == 3 then return drawRows(card, "", card.days) end

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
