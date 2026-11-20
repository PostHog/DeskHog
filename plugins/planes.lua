-- The aircraft overhead right now: a small radar with you in the middle and,
-- next to it, the nearest plane's callsign, height, speed, heading and type.
-- The centre button steps through the others, nearest first.
--
-- Positions come from adsb.fi, a community network of ADS-B receivers. It is
-- free for personal use, needs no key, answers quickly, allows a request a
-- second, and its answers carry the aircraft type and registration. When it
-- does not answer, the card asks adsb.lol, which sends the same kind of answer
-- but often takes 10 seconds or more. OpenSky also works without a key, but
-- allows an anonymous caller about 400 requests a day (one every few minutes)
-- and has neither type nor registration; airplanes.live refuses callers it has
-- not been introduced to. 15 km around a busy airport is a few KB; the `only`
-- filter keeps even 100 km (about 45 KB) well inside the device's limits. Your
-- city's coordinates come from open-meteo.com, also free and keyless.
--
-- Planes are only asked for while the card is on screen, every 30 seconds, and
-- move along their heading in between.

plugin = {
  name = "Planes",
  description = "The aircraft overhead right now, on a small radar, with callsign, height, speed and type",
  author = "DeskHog",
  version = "1.0",
  fps = 1,
}

config = {
  city = { label = "City", default = "Antwerp", order = 1 },
  radius = { label = "How far around you to look, in km (up to 100)", default = 15, order = 2 },
  unit = { label = "Height and speed in", default = "m and km/h", options = { "m and km/h", "ft and knots" }, order = 3 },
}

local W = gfx.width
local EVERY = 30000      -- ms between questions while the card is on screen
local STALE = 120000     -- ms after which an old answer is no longer shown
local RANGE, FEET -- from the settings, which are only filled in once the file has run
local COMPASS = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" }

-- Radar on the left, details on the right.
local RX, RY, RR = 58, 74, 54
local DX = 122

local place, planes, problem
local selected = 1
local asking, askedAt, fetchedAt, movedAt = false, nil, nil, nil

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

local function thousands(value)
  local text = string.format("%d", math.floor(math.abs(value) + 0.5))
  return (value < 0 and "-" or "") .. text:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", "")
end

-- Distance in km along the ground, and the bearing in degrees, from you to a point.
local function towards(lat, lon)
  local rad = math.pi / 180
  local lat1, lat2, dLon = place.latitude * rad, lat * rad, (lon - place.longitude) * rad
  local a = math.sin((lat2 - lat1) / 2) ^ 2 + math.cos(lat1) * math.cos(lat2) * math.sin(dLon / 2) ^ 2
  local distance = 6371 * 2 * math.atan(math.sqrt(a), math.sqrt(1 - a))
  local bearing = math.atan(math.sin(dLon) * math.cos(lat2),
    math.cos(lat1) * math.sin(lat2) - math.sin(lat1) * math.cos(lat2) * math.cos(dLon)) / rad
  return distance, bearing % 360
end

local function compass(degrees)
  return COMPASS[math.floor((degrees % 360) / 45 + 0.5) % 8 + 1]
end

-- adsb.fi first; adsb.lol, which sends the same kind of answer, when it fails.
local SOURCES = {
  "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d",
  "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
}
local WANTED = { ac = { { hex = true, flight = true, r = true, t = true, alt_baro = true,
  gs = true, track = true, lat = true, lon = true } } }

local function take(list)
  problem = nil
  local keep = planes and planes[selected] and planes[selected].hex
  planes = {}
  for _, item in ipairs(list) do
    local altitude = tonumber(item.alt_baro) -- "ground" for anything taxiing or parked
    local lat, lon = tonumber(item.lat), tonumber(item.lon)
    if altitude and lat and lon then
      local plane = {
        hex = tostring(item.hex or ""),
        name = (type(item.flight) == "string" and item.flight:match("^%s*(.-)%s*$")) or "",
        reg = type(item.r) == "string" and item.r or nil,
        kind = type(item.t) == "string" and item.t or nil,
        altitude = altitude,          -- feet
        speed = tonumber(item.gs),    -- knots
        track = tonumber(item.track), -- degrees
        lat = lat, lon = lon,
      }
      if plane.name == "" then plane.name = plane.reg or plane.hex:upper() end
      plane.distance, plane.bearing = towards(lat, lon)
      if plane.distance <= RANGE then planes[#planes + 1] = plane end
    end
  end
  table.sort(planes, function(a, b) return a.distance < b.distance end)
  selected = 1
  for i, plane in ipairs(planes) do
    if plane.hex == keep then selected = i end
  end
  fetchedAt, movedAt = time.ms(), time.ms()
end

local function ask(source)
  local url = string.format(SOURCES[source], place.latitude, place.longitude, math.ceil(RANGE / 1.852))
  http.get(url, { only = WANTED, timeout = 15 }, function(res)
    local list = res.json and res.json.ac
    if res.ok and type(list) == "table" then
      asking = false
      return take(list)
    end
    if SOURCES[source + 1] then return ask(source + 1) end
    asking = false
    problem = res.error and "No connection" or ("No answer about planes (" .. res.status .. ")")
  end)
end

local function fetch()
  if asking or not place then return end
  asking, askedAt = true, time.ms()
  ask(1)
end

local function locate()
  asking, askedAt = true, time.ms()
  http.get("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" .. urlencode(config.city), function(res)
    asking = false
    place = res.json and res.json.results and res.json.results[1]
    if not place then
      problem = res.ok and "No city called " .. config.city or (res.error or "No connection")
      return
    end
    problem = nil
    fetch()
  end)
end

function load()
  RANGE = math.max(1, math.min(100, tonumber(config.radius) or 15))
  FEET = config.unit == "ft and knots"
  if asking then return end
  if place then fetch() else locate() end
end

-- Only runs while the card is on screen: ask again when the answer is old, and
-- move each plane along its heading in between.
function update()
  local now = time.ms()
  if not asking and (not askedAt or now - askedAt >= EVERY) then
    if place then fetch() else locate() end
  end
  if not planes or not movedAt then return false end
  local seconds = (now - movedAt) / 1000
  movedAt = now
  if now - fetchedAt > STALE then return end
  for _, plane in ipairs(planes) do
    if plane.speed and plane.track then
      local km = plane.speed * 1.852 / 3600 * seconds
      local rad = plane.track * math.pi / 180
      plane.lat = plane.lat + km * math.cos(rad) / 111.2
      plane.lon = plane.lon + km * math.sin(rad) / (111.2 * math.cos(plane.lat * math.pi / 180))
      plane.distance, plane.bearing = towards(plane.lat, plane.lon)
    end
  end
end

function button(name)
  if name == "center" and planes and #planes > 1 then
    selected = selected % #planes + 1
    return true
  end
end

-- An arrow pointing along a heading, centred on x, y.
local function arrow(x, y, size, degrees, color)
  local rad = degrees * math.pi / 180
  local sx, sy = math.sin(rad), -math.cos(rad)
  local tipX, tipY = x + sx * size, y + sy * size
  gfx.line(x - sx * size, y - sy * size, tipX, tipY, color, 2)
  for _, turn in ipairs({ 2.5, -2.5 }) do
    local back = rad + turn
    gfx.line(tipX, tipY, tipX + math.sin(back) * size * 0.8, tipY - math.cos(back) * size * 0.8, color, 2)
  end
end

local function spot(plane)
  local scale = RR / RANGE
  local rad = plane.bearing * math.pi / 180
  return RX + math.sin(rad) * plane.distance * scale, RY - math.cos(rad) * plane.distance * scale
end

local function radar()
  gfx.circle(RX, RY, RR, "#1e3a2a")
  gfx.circle(RX, RY, RR - 1, "#06120b")
  gfx.circle(RX, RY, RR / 2, "#1e3a2a")
  gfx.circle(RX, RY, RR / 2 - 1, "#06120b")
  gfx.line(RX - RR, RY, RX + RR, RY, "#163020")
  gfx.line(RX, RY - RR, RX, RY + RR, "#163020")
  gfx.text(RX, RY - RR - 1, "N", "#3c6e50", "small", "center")
  if planes then
    for i, plane in ipairs(planes) do
      if i ~= selected and plane.distance <= RANGE then
        local x, y = spot(plane)
        gfx.rect(x - 1, y - 1, 3, 3, "#9fd8b0")
      end
    end
    local plane = planes[selected]
    if plane and plane.distance <= RANGE * 1.1 then
      local x, y = spot(plane)
      if plane.track then
        local rad = plane.track * math.pi / 180
        gfx.line(x, y, x + math.sin(rad) * 10, y - math.cos(rad) * 10, "yellow", 1)
      end
      gfx.circle(x, y, 3, "yellow")
    end
  end
  gfx.circle(RX, RY, 2, "#2ecc71")
end

local function fit(text, font, width)
  if gfx.textWidth(text, font) <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "...", font) > width do text = text:sub(1, -2) end
  return text .. "..."
end

function draw()
  local fresh = planes and fetchedAt and time.ms() - fetchedAt <= STALE
  radar()
  local right = W - DX - 4
  gfx.text(DX, 4, fit("Over " .. (place and place.name or config.city), "small", right), "gray", "small")

  if not fresh then
    local text = problem or (place and "Looking at the sky..." or "Finding " .. config.city .. "...")
    for i, line in ipairs(gfx.wrap(text, right, "small")) do
      gfx.text(DX, 34 + (i - 1) * 17, line, problem and "orange" or "gray", "small")
    end
    return
  end

  if #planes == 0 then
    for i, line in ipairs(gfx.wrap("No planes within " .. thousands(RANGE) .. " km", right, "medium")) do
      gfx.text(DX, 34 + (i - 1) * 20, line, "white", "medium")
    end
    return
  end

  local plane = planes[selected]
  gfx.text(DX, 22, fit(plane.name, "loud", right), "yellow", "loud")
  local reg = plane.reg ~= plane.name and plane.reg or nil
  local about = plane.kind and reg and plane.kind .. "  " .. reg or plane.kind or reg
  if about then gfx.text(DX, 44, fit(about, "small", right), "gray", "small") end

  local height = FEET and thousands(plane.altitude) .. " ft" or thousands(plane.altitude * 0.3048) .. " m"
  gfx.text(DX, 62, height, "white", "medium")

  if plane.speed then
    local speed = FEET and thousands(plane.speed) .. " kt" or thousands(plane.speed * 1.852) .. " km/h"
    local x = DX
    if plane.track then
      arrow(DX + 6, 90, 5, plane.track, "white")
      x = DX + 18
    end
    gfx.text(x, 81, fit(speed, "medium", W - x - 4), "white", "medium")
  end

  local distance = plane.distance < 10 and string.format("%.1f km", plane.distance) or thousands(plane.distance) .. " km"
  gfx.text(DX, 102, distance .. " " .. compass(plane.bearing), "gray", "small")
  gfx.text(DX, 118, selected .. " of " .. #planes, "gray", "small")
end
