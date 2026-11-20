-- How clean the air is where you are, and which pollen is about. The air quality
-- index big and coloured, the pollutant behind it, the strongest pollen, and a bar
-- per hour of today. From open-meteo.com's air quality API (free, no API key),
-- which takes its numbers from the Copernicus Atmosphere Monitoring Service.
-- Pollen is only forecast for Europe; elsewhere the card says so.

plugin = {
  name = "Air",
  description = "Air quality and pollen where you are, with a bar per hour of today",
  author = "DeskHog",
  version = "1.0",
  refresh = 1800,
}

config = {
  city = { label = "City", default = "Antwerp", order = 1 },
  scale = { label = "Index", default = "European", options = { "European", "US" }, order = 2 },
  light = { label = "Light the LED when the air is poor or pollen is high", default = true, order = 3 },
}

local W = gfx.width
local US, BANDS -- set in load(): the settings are not filled in yet up here
local place, problem, now, hours, hour

-- The bands of each index, from the European Environment Agency and the US EPA,
-- with the LED colour for the bad ones.
local US_BANDS = {
  { 50, "Good", "#2ecc71" },
  { 100, "Moderate", "#f1c40f" },
  { 150, "Unhealthy for some", "#e67e22", "orange" },
  { 200, "Unhealthy", "#e74c3c", "red" },
  { 300, "Very unhealthy", "#9b59b6", "red" },
  { math.huge, "Hazardous", "#a0204a", "red" },
}
local EU_BANDS = {
  { 20, "Good", "#50c8f0" },
  { 40, "Fair", "#50ccaa" },
  { 60, "Moderate", "#f0e641" },
  { 80, "Poor", "#ff5050", "orange" },
  { 100, "Very poor", "#c0304a", "red" },
  { math.huge, "Extremely poor", "#9b59b6", "red" },
}

local POLLUTANTS = {
  { key = "pm2_5", name = "PM2.5" },
  { key = "pm10", name = "PM10" },
  { key = "ozone", name = "ozone" },
  { key = "nitrogen_dioxide", name = "NO2" },
}

-- Grains per cubic metre where Low, Moderate and High start, from MeteoSwiss'
-- load classes for allergenic pollen ("Belastungsklassen der allergenen
-- Pollenarten"; their "very high" is shown as High here). MeteoSwiss has no class
-- for olive, so it borrows birch's, as Foreca's pollen ratings also group them.
local POLLEN = {
  { key = "alder_pollen", name = "Alder", low = 1, moderate = 11, high = 70 },
  { key = "birch_pollen", name = "Birch", low = 1, moderate = 11, high = 70 },
  { key = "grass_pollen", name = "Grass", low = 1, moderate = 20, high = 50 },
  { key = "mugwort_pollen", name = "Mugwort", low = 1, moderate = 6, high = 15 },
  { key = "olive_pollen", name = "Olive", low = 1, moderate = 11, high = 70 },
  { key = "ragweed_pollen", name = "Ragweed", low = 1, moderate = 6, high = 11 },
}
local LEVELS = { { "Low", "#2ecc71" }, { "Moderate", "#f1c40f" }, { "High", "#e74c3c" } }

local function band(aqi)
  for _, b in ipairs(BANDS) do
    if aqi <= b[1] then return b end
  end
end

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

-- The pollen with the highest level: its name and level (1 to 3), or 0 when there
-- is none, or nil when there is no pollen forecast for this place.
local function strongestPollen(current)
  local best, level, known = nil, 0, false
  for _, p in ipairs(POLLEN) do
    local count = tonumber(current[p.key])
    if count then
      known = true
      local l = count >= p.high and 3 or count >= p.moderate and 2 or count >= p.low and 1 or 0
      if l > level then best, level = p.name, l end
    end
  end
  if not known then return nil end
  return best, level
end

local function setLight()
  if not config.light or not now then return end
  local light = band(now.aqi)[4]
  if not light and now.pollenLevel == 3 then light = "orange" end
  if light then led.set(light) else led.off() end
end

local function fetch()
  local index = US and "us_aqi" or "european_aqi"
  local fields = { index }
  for _, p in ipairs(POLLUTANTS) do
    fields[#fields + 1] = p.key
    fields[#fields + 1] = index .. "_" .. p.key
  end
  for _, p in ipairs(POLLEN) do fields[#fields + 1] = p.key end
  local url = string.format(
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f"
      .. "&current=%s&hourly=%s&forecast_days=1&timezone=auto",
    place.latitude, place.longitude, table.concat(fields, ","), index)

  http.get(url, function(res)
    local current = res.json and res.json.current
    local aqi = current and tonumber(current[index])
    if not aqi then
      if not now then problem = res.ok and "No air data for this place" or (res.error or "No connection") end
      return
    end
    problem = nil
    now = { aqi = aqi }

    -- The pollutant with the highest index of its own is the one that sets the total.
    local worst = -1
    for _, p in ipairs(POLLUTANTS) do
      local sub = tonumber(current[index .. "_" .. p.key])
      if sub and sub > worst then
        worst, now.pollutant, now.amount = sub, p, tonumber(current[p.key])
      end
    end
    now.pollen, now.pollenLevel = strongestPollen(current)

    hours, hour = {}, nil
    local hourly = res.json.hourly or {}
    for i, value in ipairs(hourly[index] or {}) do
      hours[i] = tonumber(value) or false -- json.null for hours without a forecast
      if hourly.time and hourly.time[i] == current.time then hour = i end
    end
    setLight()
  end)
end

function load()
  US = config.scale == "US"
  BANDS = US and US_BANDS or EU_BANDS
  if place then return fetch() end
  http.get("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" .. urlencode(config.city), function(res)
    place = res.json and res.json.results and res.json.results[1]
    if not place then
      problem = res.ok and "City not found" or (res.error or "No connection")
      return
    end
    fetch()
  end)
end

local function drawHours(top, bottom)
  if not hours or #hours == 0 then return end
  -- The day's highest hour fills a bar, but a clean day stays low: at least the
  -- top of Fair (European) or Moderate (US) does.
  local full = US and 100 or 40
  for _, value in ipairs(hours) do
    if value and value > full then full = value end
  end
  local step = (W - 16) / #hours
  for i, value in ipairs(hours) do
    local x = 8 + (i - 1) * step
    if value then
      local b = band(value)
      local height = math.max(2, math.min(1, value / full) * (bottom - top))
      gfx.rect(x, bottom - height, math.max(1, step - 2), height, i == hour and "white" or b[3], 1)
    else
      gfx.rect(x, bottom - 1, math.max(1, step - 2), 1, "#444444")
    end
  end
  gfx.text(8, bottom + 2, "0h", "gray", "small")
  gfx.text(W / 2, bottom + 2, "today", "gray", "small", "center")
  gfx.text(W - 8, bottom + 2, "24h", "gray", "small", "right")
end

function draw()
  gfx.text(8, 4, "Air in " .. (place and place.name or config.city), "gray", "small")
  if problem or not now then
    ui.label(problem or "Loading...", problem and "orange" or "gray")
    return
  end

  -- The index, big, with its band beside it.
  local b = band(now.aqi)
  local number = tostring(math.floor(now.aqi + 0.5))
  gfx.text(8, 20, number, b[3], "large")
  local x = 8 + gfx.textWidth(number, "large") + 10
  local font = gfx.textWidth(b[2], "medium") <= W - x - 8 and "medium" or "small"
  gfx.text(x, 26, b[2], b[3], font)
  gfx.text(x, 44, US and "US AQI" or "European AQI", "gray", "small")

  -- What sets it, and the pollen.
  local p = now.pollutant
  if p then
    local line = "Mostly " .. p.name
    if now.amount then line = line .. string.format(", %d µg/m³", math.floor(now.amount + 0.5)) end
    gfx.text(8, 62, line, "white", "small")
  end
  if now.pollenLevel == nil then
    gfx.text(8, 79, "No pollen forecast here", "gray", "small")
  elseif now.pollenLevel == 0 then
    gfx.text(8, 79, "No pollen", "white", "small")
  else
    local level = LEVELS[now.pollenLevel]
    local label = now.pollen .. " pollen: "
    gfx.text(8, 79, label, "white", "small")
    gfx.text(8 + gfx.textWidth(label, "small"), 79, level[1], level[2], "small")
  end

  drawHours(98, 116)
end
