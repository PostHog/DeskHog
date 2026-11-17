-- A round dial for any number that a web address answers with as JSON: the
-- temperature outside, a Home Assistant sensor, the load of a server, the
-- price of something. Add a card per number.
--
-- Each card needs the address, and the way to the number inside the answer:
-- the names to follow, with dots between them. For this answer
--
--   { "current": { "temperature_2m": 11.4 }, "hourly": { "rain": [0, 0.2] } }
--
-- the path "current.temperature_2m" finds 11.4, and "hourly.rain.1" finds 0.2:
-- a number in a path picks from a list, counting from 0 as JSON people do.
--
-- As it comes, the card shows the temperature in Antwerp from open-meteo.com,
-- which needs no account. For a Home Assistant sensor: the address
-- http://192.168.1.10:8123/api/states/sensor.office_temperature, the path
-- "state", and a long-lived access token.

plugin = {
  name = "Gauge",
  description = "A round dial for any number from a JSON address, such as a temperature or a sensor",
  author = "DeskHog",
  version = "1.0",
  refresh = 60,
}

local W, H = gfx.width, gfx.height
local CX, CY = W // 2, 69            -- the middle of the dial
local R = 60                         -- its radius
local TRACK = 7                      -- how thick the ring is
local FROM, SWEEP = 225, 270         -- it starts bottom left and goes three quarters round, over the top
local TRACK_COLOR, GRAY = "#2a2e37", "#8a8f98"

-- The path as a list of steps: "hourly.rain.1" becomes { "hourly", "rain", 1 }.
local function steps(path)
  local list = {}
  for step in path:gmatch("[^.]+") do
    list[#list + 1] = math.tointeger(tonumber(step)) or step
  end
  return list
end

-- Follow the steps into the decoded answer. Nil when the way stops somewhere.
local function resolve(value, path)
  for _, step in ipairs(path) do
    if type(value) ~= "table" then return nil end
    if type(step) == "number" and value[step + 1] ~= nil then
      value = value[step + 1]        -- Lua lists count from 1
    else
      value = value[tostring(step)]  -- a name, or an object whose keys happen to be numbers
    end
  end
  return value
end

-- The same steps as a filter for `only`, so that of a large answer just the
-- one number is kept: { "current", "temperature_2m" } becomes
-- { current = { temperature_2m = true } }. In a filter, a first entry stands
-- for every item of a list, so a number in the path becomes one of those. It
-- is also kept as a name, for an object whose keys happen to be numbers.
local function filter(path)
  local wanted = true
  for i = #path, 1, -1 do
    local step = path[i]
    if type(step) == "number" then
      wanted = { wanted, [tostring(step)] = wanted }
    else
      wanted = { [step] = wanted }
    end
  end
  return wanted
end

-- 21.456 becomes "21.5", 1500.2 becomes "1500", 3 stays "3".
local function tidy(number)
  if number == math.floor(number) or math.abs(number) >= 1000 then return string.format("%.0f", number) end
  if math.abs(number) < 1 then return (string.format("%.2f", number):gsub("0$", "")) end
  return string.format("%.1f", number)
end

-- Where on the card a point of the dial is. Angles as gfx.arc has them:
-- 0 is straight up and they grow clockwise.
local function point(angle, radius)
  local a = math.rad(angle)
  return CX + radius * math.sin(a), CY - radius * math.cos(a)
end

-- From blue at the low end through green and yellow to red at the high end.
local STOPS = { { 0x34, 0x98, 0xdb }, { 0x2e, 0xcc, 0x71 }, { 0xf1, 0xc4, 0x0f }, { 0xe7, 0x4c, 0x3c } }
local function heat(share)
  local at = share * (#STOPS - 1)
  local i = math.min(math.floor(at), #STOPS - 2)
  local a, b, t = STOPS[i + 1], STOPS[i + 2], at - i
  local function mix(k) return math.floor(a[k] + (b[k] - a[k]) * t + 0.5) end
  return string.format("#%02x%02x%02x", mix(1), mix(2), mix(3))
end

local function refresh(card)
  local settings = card.config
  if settings.url == "" or settings.path == "" then
    card.problem = "Give the address and the path to the number on the web page"
    return
  end
  local path = steps(settings.path)
  local options = { only = filter(path) }
  if settings.token ~= "" then options.headers = { Authorization = "Bearer " .. settings.token } end

  http.get(settings.url, options, function(res)
    if not res.ok then
      card.problem = res.status == 401 and "The token was refused"
        or res.error or ("The address answered " .. res.status)
      return
    end
    -- Home Assistant sends its numbers as text, "21.5": tonumber takes both.
    local found = resolve(res.json, path)
    local number = tonumber(found)
    if not number then
      card.problem = found == nil and ("Nothing at " .. settings.path .. " in the answer")
        or (settings.path .. " is not a number")
      return
    end
    card.problem = nil
    card.value = number
  end)
end

local function drawDial(card)
  local settings = card.config
  local low, high = settings.min, settings.max
  if high <= low then high = low + 1 end
  -- How far round the value is, from 0 to 1. A value off the scale stops at its end.
  local share = math.max(0, math.min(1, (card.value - low) / (high - low)))
  local color = settings.color == "by value" and heat(share) or settings.color

  -- The track, and over it the part up to the value. An arc may run past 360.
  gfx.arc(CX, CY, R, FROM, FROM + SWEEP, TRACK_COLOR, TRACK)
  if share > 0 then
    gfx.arc(CX, CY, R, FROM, FROM + SWEEP * share, color, TRACK)
  end

  -- A tick every tenth, longer at the ends and in the middle.
  for i = 0, 10 do
    local angle = FROM + SWEEP * i / 10
    local x1, y1 = point(angle, R - TRACK - 3)
    local x2, y2 = point(angle, R - TRACK - (i % 5 == 0 and 9 or 6))
    gfx.line(x1, y1, x2, y2, i % 5 == 0 and "#c8ccd4" or "#5b616d", 1)
  end

  -- The needle: a kite from its tip, round the hub, to a short tail.
  local angle = FROM + SWEEP * share
  local tipX, tipY = point(angle, R - TRACK - 12)
  local leftX, leftY = point(angle - 90, 4)
  local rightX, rightY = point(angle + 90, 4)
  local tailX, tailY = point(angle + 180, 9)
  gfx.poly({ tipX, tipY, rightX, rightY, tailX, tailY, leftX, leftY }, "white")
  gfx.circle(CX, CY, 5, "white")
  gfx.circle(CX, CY, 2, "black")

  -- The scale's ends, under the ends of the ring. A small triangle beside one
  -- of them says the value is off the scale at that end.
  local lowText, highText = tidy(low), tidy(high)
  local x, y = point(FROM, R - 3)
  gfx.text(x, y + 6, lowText, GRAY, "tiny", "center")
  if card.value < low then
    x = x - gfx.textWidth(lowText, "tiny") / 2 - 12
    gfx.triangle(x, y + 9, x + 8, y + 9, x + 4, y + 16, color)
  end
  x, y = point(FROM + SWEEP, R - 3)
  gfx.text(x, y + 6, highText, GRAY, "tiny", "center")
  if card.value > high then
    x = x + gfx.textWidth(highText, "tiny") / 2 + 4
    gfx.triangle(x, y + 16, x + 8, y + 16, x + 4, y + 9, color)
  end

  -- The value, in the gap at the bottom. The chunky "loud" font has digits but
  -- no ° or µ, so the unit follows in the small font.
  local number, unit = tidy(card.value), settings.unit
  local numberWidth = gfx.textWidth(number, "loud")
  local left = CX - (numberWidth + gfx.textWidth(unit, "small")) / 2
  gfx.text(left, 101, number, "white", "loud")
  gfx.text(left + numberWidth + 1, 103, unit, "#c8ccd4", "small")
end

cards = {
  dial = {
    name = "Dial",
    config = {
      label = { label = "Name", default = "Antwerp", order = 1 },
      url = { label = "Address that answers with JSON",
        default = "https://api.open-meteo.com/v1/forecast?latitude=51.22&longitude=4.40&current=temperature_2m", order = 2 },
      path = { label = "Path to the number, like current.temperature_2m", default = "current.temperature_2m", order = 3 },
      min = { label = "Lowest value on the dial", default = -10, order = 4 },
      max = { label = "Highest value on the dial", default = 40, order = 5 },
      unit = { label = "Unit, shown after the value", default = "°", order = 6 },
      color = { label = "Colour", default = "by value", options = { "by value", "green", "orange", "blue", "red", "white" }, order = 7 },
      token = { label = "Access token (optional), sent as Authorization: Bearer", default = "", secret = true, order = 8 },
    },

    load = function(card)
      card.title = card.config.label
      refresh(card)
    end,

    draw = function(card)
      gfx.text(8, 3, card.config.label, GRAY, "small")
      if card.value then
        drawDial(card)
        -- A failed answer after a good one: the dial keeps the last value, marked as old.
        if card.problem then gfx.text(W - 8, 3, "no answer", "orange", "tiny", "right") end
      elseif card.problem then
        for i, line in ipairs(gfx.wrap(card.problem, W - 16)) do
          gfx.text(8, 30 + (i - 1) * 17, line, "orange")
        end
      else
        gfx.text(CX, 60, "Loading...", GRAY, "small", "center")
      end
    end,

    -- Centre looks again right now.
    button = function(card, name)
      if name ~= "center" then return false end
      refresh(card)
      return true
    end,
  },
}
