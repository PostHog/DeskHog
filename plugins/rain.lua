-- Will it rain in the next two hours? One line that says it, and a bar per
-- quarter of an hour. From open-meteo.com (free, no API key).

plugin = {
  name = "Rain",
  description = "Whether it will rain in the next two hours, where you are, quarter by quarter",
  author = "DeskHog",
  version = "1.2",
  refresh = 600,
}

config = {
  city = { label = "City", default = "Ghent" },
}

local W, H = gfx.width, gfx.height
local WET = 0.1 -- millimetres in a quarter of an hour that count as rain
local place, amounts, problem, summary

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

local function minutes(quarters)
  if quarters >= 4 then
    local hours = quarters / 4
    return hours == 1 and "an hour" or (hours == math.floor(hours) and string.format("%d", hours) or string.format("%.1f", hours)) .. " hours"
  end
  return quarters * 15 .. " minutes"
end

local function describe()
  local firstWet, firstDry
  for i, amount in ipairs(amounts) do
    if amount >= WET then firstWet = firstWet or i else firstDry = firstDry or i end
  end
  if not firstWet then return "Dry for the next two hours" end
  if not firstDry then return "Rain for the next two hours" end
  if firstWet == 1 then return "Raining. It stops in about " .. minutes(firstDry - 1) end
  return "Rain in about " .. minutes(firstWet - 1)
end

local function fetch()
  local url = string.format(
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&minutely_15=precipitation&forecast_minutely_15=8",
    place.latitude, place.longitude)
  http.get(url, function(res)
    local list = res.json and res.json.minutely_15 and res.json.minutely_15.precipitation
    if not list then
      if not amounts then problem = res.error or "No forecast for this place" end
      return
    end
    problem, amounts = nil, {}
    for i, amount in ipairs(list) do amounts[i] = tonumber(amount) or 0 end
    summary = describe()
  end)
end

function load()
  if place then return fetch() end
  http.get("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" .. urlencode(config.city), function(res)
    place = res.json and res.json.results and res.json.results[1]
    if not place then
      problem = res.ok and "No city called " .. config.city or (res.error or "No connection")
      return
    end
    fetch()
  end)
end

function draw()
  gfx.text(8, 4, "Rain in " .. (place and place.name or config.city), "gray", "small")
  if problem or not amounts then
    ui.label(problem or "Looking at the sky...", problem and "orange" or "gray")
    return
  end
  local wet = summary:find("^Rain") or summary:find("^Raining")
  local lines = gfx.wrap(summary, W - 24, "medium")
  for i, line in ipairs(lines) do
    gfx.text(W / 2, 24 + (i - 1) * gfx.textHeight("medium"), line, wet and "#5dade2" or "white", "medium", "center")
  end

  -- A bar per quarter of an hour; 2 mm in a quarter is a downpour and fills the height.
  local base, tall = 112, 34
  local step = (W - 24) / #amounts
  for i, amount in ipairs(amounts) do
    local x = 12 + (i - 1) * step
    if amount >= WET then
      local height = math.max(3, math.min(1, amount / 2) * tall)
      gfx.rect(x, base - height, step - 3, height, "#3498db", 2)
    else
      gfx.rect(x, base - 1, step - 3, 1, "#444444")
    end
  end
  gfx.text(12, base + 3, "now", "gray", "small")
  gfx.text(W - 12, base + 3, "in 2 hours", "gray", "small", "right")
end
