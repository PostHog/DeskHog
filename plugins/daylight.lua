-- Daylight: sunrise, sunset and the length of the day, with how much longer or
-- shorter it is than yesterday, and the phase of the moon.
--
-- The sums are done here, on the device: the sun with NOAA's solar equations
-- (sunrise and sunset are when the top of the sun touches the horizon, 0.833°
-- below it once the air bends the light), the moon by counting synodic months
-- from the new moon of 6 January 2000. The internet is only asked where the city
-- is and how far its clocks are from UTC (open-meteo.com, free, no API key). The
-- place is saved, so after the first lookup it works without WiFi too.
--
-- The centre button switches to dawn, noon, golden hour and dusk.

plugin = {
  name = "Daylight",
  description = "Sunrise, sunset and the length of the day, how it changed since yesterday, and the moon",
  author = "DeskHog",
  version = "1.0",
  refresh = 10800, -- ask for the UTC offset again every three hours, to follow summer time
  fps = 1,         -- look at the time once a second; it is only repainted when the minute changes
}

config = {
  city = { label = "City", default = "Antwerp" },
}

local W, H = gfx.width, gfx.height
local RAD, DEG = math.pi / 180, 180 / math.pi
local SUN, SUN_LOW, ARC, NIGHT = "#ffc83d", "#8a6a2a", "#6b5a2a", "#4a4a60"

local place -- { city, name, latitude, longitude, offset }, saved once looked up
local problem, view, shown, today

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

-- ------------------------------------------------------------------ the sun

-- Where the sun is, t days after noon UTC on 1 January 2000: its declination (in
-- radians) and the equation of time (minutes the sundial is ahead of the clock).
-- Numbers are 32-bit floats on the device, so this works from days, not Julian dates.
local function sunAt(t)
  local T = t / 36525
  local L = (280.46646 + (0.98564736 * t) % 360) * RAD       -- mean longitude
  local M = (357.52911 + (0.98560028 * t) % 360) * RAD       -- mean anomaly
  local e = 0.016708634 - 0.000042037 * T                    -- orbit eccentricity
  local C = math.sin(M) * (1.914602 - 0.004817 * T) + math.sin(2 * M) * (0.019993 - 0.000101 * T)
    + math.sin(3 * M) * 0.000289
  local omega = (125.04 - (0.05295377 * t) % 360) * RAD
  local lambda = L + (C - 0.00569 - 0.00478 * math.sin(omega)) * RAD
  local eps = (23 + (26 + (21.448 - 46.815 * T) / 60) / 60 + 0.00256 * math.cos(omega)) * RAD
  local decl = math.asin(math.sin(eps) * math.sin(lambda))
  local y = math.tan(eps / 2) ^ 2
  local eq = y * math.sin(2 * L) - 2 * e * math.sin(M) + 4 * e * y * math.sin(M) * math.cos(2 * L)
    - 0.5 * y * y * math.sin(4 * L) - 1.25 * e * e * math.sin(2 * M)
  return decl, 4 * eq * DEG
end

-- When the sun crosses `altitude` degrees on local day `day` (days since 1970),
-- rising or setting, in local minutes after midnight. Also says "up" or "down"
-- when it stays above or below all day, and gives the minute of solar noon.
local function crossing(day, altitude, rising)
  local lat, lon, offset = place.latitude * RAD, place.longitude, place.offset / 60
  local start = (day - 10957) - 0.5 -- midnight UTC of that day, in days since J2000
  local minute, noon = 720 - 4 * lon, nil
  for pass = 1, 3 do -- first at noon, then twice at the crossing itself
    local decl, eq = sunAt(start + minute / 1440)
    noon = 720 - 4 * lon - eq
    local cosH = (math.sin(altitude * RAD) - math.sin(lat) * math.sin(decl)) / (math.cos(lat) * math.cos(decl))
    if cosH > 1 or cosH < -1 then
      if pass == 1 then return nil, cosH > 1 and "down" or "up", noon + offset end
      break -- keep the last answer rather than give up on a day that has one
    end
    local hours = math.acos(cosH) * DEG * 4
    minute = rising and noon - hours or noon + hours
  end
  return minute + offset, nil, noon + offset
end

-- Sunrise, sunset and the day's length in minutes for one local day.
local function daylight(day)
  local rise, state, noon = crossing(day, -0.833, true)
  local set = crossing(day, -0.833, false)
  if state or not set then
    return { state = state or "down", noon = noon, length = state == "up" and 1440 or 0 }
  end
  return { rise = rise, set = set, noon = noon, length = set - rise }
end

-- ----------------------------------------------------------------- the moon

local NEW_MOON = 947182440 -- 6 January 2000, 18:14 UTC
local SYNODIC = 29.530588853
local PHASES = { "New moon", "Waxing crescent", "First quarter", "Waxing gibbous", "Full moon", "Waning gibbous",
  "Last quarter", "Waning crescent" }

-- How far through its cycle the moon is, from 0 (new) through 0.5 (full) to 1.
local function moonPhase(utc)
  return ((utc - NEW_MOON) / 86400 % SYNODIC) / SYNODIC
end

-- The moon as a disc, a row at a time: lit from the right while it grows (seen from
-- the north; the other way round south of the equator).
local function drawMoon(x, y, r, phase, south)
  gfx.circle(x, y, r, "#2e2e38")
  local k = math.cos(phase * 2 * math.pi)
  for dy = -r + 0.5, r - 0.5 do
    local w = math.sqrt(r * r - dy * dy)
    local from, to = k * w, w
    if phase > 0.5 then from, to = -w, -k * w end
    if south then from, to = -to, -from end
    if to - from >= 0.5 then gfx.rect(x + from, y + dy - 0.5, to - from, 1, "#e8e4d0") end
  end
end

-- ------------------------------------------------------------------- words

-- Text cut short with ".." so it fits `width` pixels.
local function fit(text, width)
  if gfx.textWidth(text, "small") <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "..", "small") > width do
    text = text:sub(1, utf8.offset(text, -1) - 1)
  end
  return text .. ".."
end

local function clock(minutes)
  local m = math.floor(minutes + 0.5) % 1440
  return string.format("%02d:%02d", m // 60, m % 60)
end

local function length(minutes)
  local m = math.floor(minutes + 0.5)
  if m >= 1440 then return "24 hours" end
  if m <= 0 then return "No daylight" end
  return string.format("%d h %02d min", m // 60, m % 60)
end

local function change(seconds)
  local s = math.floor(math.abs(seconds) + 0.5)
  local sign = seconds < 0 and "-" or "+"
  if s == 0 then return "Same as yesterday" end
  if s >= 3600 then return string.format("%s%d h %d min", sign, s // 3600, s % 3600 // 60) end
  if s >= 60 then return string.format("%s%d min %d s", sign, s // 60, s % 60) end
  return string.format("%s%d s", sign, s)
end

-- ------------------------------------------------------------------ the data

local function fetchOffset()
  local url = string.format("https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    .. "&timezone=auto&forecast_days=1", place.latitude, place.longitude)
  http.get(url, { only = { utc_offset_seconds = true } }, function(res)
    local offset = res.json and tonumber(res.json.utc_offset_seconds)
    if not offset then
      if not place.offset then problem = res.error or "No time zone for this place" end
      return
    end
    problem = nil
    if offset ~= place.offset then
      place.offset = offset
      store.set("place", place)
      shown = nil
    end
  end)
end

function load()
  if not place then
    place = store.get("place", nil)
    if place and place.city ~= config.city then place = nil end -- the setting changed
  end
  if place then return fetchOffset() end
  http.get("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" .. urlencode(config.city),
    { only = { results = { { name = true, latitude = true, longitude = true } } } }, function(res)
      local found = res.json and res.json.results and res.json.results[1]
      if not found then
        problem = res.ok and "No city called " .. config.city or (res.error or "No connection")
        return
      end
      place = { city = config.city, name = found.name, latitude = found.latitude, longitude = found.longitude }
      fetchOffset()
    end)
end

-- Everything the card shows, worked out once a minute.
local function work()
  local utc = time.now()
  if utc == 0 or not place or not place.offset then return nil end
  local here = utc + place.offset
  local day, minute = here // 86400, here % 86400 / 60
  local d = daylight(day)
  d.minute = minute
  d.change = (d.length - daylight(day - 1).length) * 60
  d.moon = moonPhase(utc)
  d.dawn = crossing(day, -6, true)
  d.dusk = crossing(day, -6, false)
  d.golden = crossing(day, 6, false)
  return d
end

function update()
  local utc = time.now()
  local key = (utc // 60) .. (view or "") .. tostring(place and place.offset)
  if key == shown then return false end
  shown = key
  today = work()
end

function button(name)
  if name ~= "center" then return end
  view = view == nil and "times" or nil
  shown = nil
  return true
end

-- ----------------------------------------------------------------- drawing

local CX, HORIZON, RX, RY, RY_NIGHT = W / 2, 84, 86, 52, 12

local function onArc(f, below)
  local a = f * math.pi
  if below then return CX + RX * math.cos(a), HORIZON + RY_NIGHT * math.sin(a) end
  return CX - RX * math.cos(a), HORIZON - RY * math.sin(a)
end

-- Where the sun goes on the picture: how far along the day (or night) it is.
local function sunSpot(d)
  local now = d.minute
  if d.state then
    local f = ((now - d.noon + 720) % 1440) / 1440
    return f, d.state == "down"
  end
  if now >= d.rise and now <= d.set then return (now - d.rise) / (d.set - d.rise), false end
  local set, rise = d.set, d.rise + 1440
  if now < d.rise then set, rise = d.set - 1440, d.rise end
  return (now - set) / (rise - set), true
end

local function drawArc(d)
  for i = 0, 30 do
    local x, y = onArc(i / 30, false)
    gfx.rect(x - 1, y - 1, 2, 2, ARC)
  end
  for i = 1, 11 do
    local x, y = onArc(i / 12, true)
    gfx.rect(x - 1, y - 1, 2, 2, NIGHT)
  end
  gfx.line(8, HORIZON, W - 8, HORIZON, "#555555")

  local f, below = sunSpot(d)
  local x, y = onArc(math.max(0, math.min(1, f)), below)
  if below then
    gfx.circle(x, y, 5, SUN_LOW)
  else
    gfx.circle(x, y, 8, "#4a3a12")
    gfx.circle(x, y, 6, SUN)
  end

  local mid = d.state == "up" and "Sun up all day" or d.state == "down" and "No sunrise today" or length(d.length)
  gfx.text(CX, 46, mid, "white", "medium", "center")
  local delta = change(d.change)
  if d.state and d.change == 0 then delta = d.state == "up" and "Midnight sun" or "Polar night" end
  gfx.text(CX, 63, delta, d.change < 0 and "#9aa6c8" or "#f0c060", "small", "center")

  gfx.text(CX - RX, 98, d.rise and clock(d.rise) or "--:--", "white", "medium", "center")
  gfx.text(CX - RX, 115, "sunrise", "gray", "small", "center")
  gfx.text(CX + RX, 98, d.set and clock(d.set) or "--:--", "white", "medium", "center")
  gfx.text(CX + RX, 115, "sunset", "gray", "small", "center")
end

local function drawTimes(d)
  local rows = {
    { "Dawn", d.dawn },
    { "Sunrise", d.rise },
    { "Noon", d.noon },
    { "Golden hour", d.golden },
    { "Sunset", d.set },
    { "Dusk", d.dusk },
  }
  for i, row in ipairs(rows) do
    local y = 24 + (i - 1) * 17
    gfx.text(10, y, row[1], "gray", "small")
    gfx.text(W - 10, y, row[2] and clock(row[2]) or "--:--", i == 4 and "#f0a040" or "white", "small", "right")
  end
end

function draw()
  local name = place and place.name or config.city
  if problem or not today then
    gfx.text(8, 4, name, "gray", "small")
    local waiting = time.now() == 0 and "Waiting for the time..." or "Finding " .. config.city .. "..."
    ui.label(problem or waiting, problem and "orange" or "gray")
    return
  end

  local moon = today.moon
  local phase = PHASES[math.floor(moon * 8 + 0.5) % 8 + 1]
  drawMoon(W - 14, 12, 9, moon, place.latitude < 0)
  if not view then
    gfx.text(W - 29, 4, phase, "gray", "small", "right")
    gfx.text(8, 4, fit(name, W - 45 - gfx.textWidth(phase, "small")), "gray", "small")
    drawArc(today)
  else
    gfx.text(8, 4, fit(name .. ", today", W - 40), "gray", "small")
    drawTimes(today)
  end
end
