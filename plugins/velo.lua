-- Bikes and free docks at a Velo Antwerpen station, live. Give a station's
-- number (as on the pole, e.g. 1 or 001) or part of its name. A card can watch
-- up to five stations, separated by commas: it then opens on a list of them all,
-- and the centre button goes to each station in turn. If you like, the LED turns
-- amber when a station runs low on bikes.
--
-- The numbers come from Velo's own GBFS feed (gbfs.smartbike.com/antwerp):
-- GBFS is the open format bike share systems publish their stations in, free
-- and without a key. Its station list (60 KB, with addresses and coordinates)
-- and the counts (50 KB) are both more than the device reads in one go, so the
-- `only` filter keeps just the numbers, names and counts. The list is read
-- once, to find your station; the counts every 90 seconds while a card is on
-- screen, and every 5 minutes in the background for a card that uses the LED.
-- citybik.es has both in one answer, but that is 85 KB too, and a copy of
-- this feed.
--
-- Other bike shares with a GBFS feed work the same way: Villo (Brussels),
-- Blue-bike (at Belgian stations) and Li Bia Velo (Namur) by name, any other by
-- the address of its gbfs.json (MobilityData lists them on GitHub, in
-- gbfs/systems.csv). It has to be GBFS version 1 or 2: version 3 writes every
-- name once per language, which no longer fits in the plugin's memory for a
-- city's worth of stations. Most systems publish both. E-bikes are shown
-- separately when a feed counts them in num_ebikes_available; Velo has none,
-- and counting them per kind of bike, as Villo does, takes too much memory too.

plugin = {
  name = "Velo",
  description = "Bikes and free docks at your Velo Antwerpen stations, or at another bike share's",
  author = "DeskHog",
  version = "1.1",
  refresh = 300, -- the background check for cards that use the LED
  fps = 1,
}

-- Shared by all of this connection's cards.
config = {
  system = { label = "Bike share: Velo, Villo, Blue-bike, Li Bia Velo, or the address of a gbfs.json", default = "Velo" },
}

local VELO = { "Velo", "https://gbfs.smartbike.com/antwerp/1.0/gbfs.json" }
local BLUE = { "Blue-bike", "https://api.delijn.be/gbfs/gbfs.json" }
local SYSTEMS = {
  ["velo"] = VELO,
  ["velo antwerpen"] = VELO,
  ["villo"] = { "Villo", "https://api.cyclocity.fr/contracts/bruxelles/gbfs/gbfs.json" },
  ["blue-bike"] = BLUE,
  ["bluebike"] = BLUE,
  ["blue bike"] = BLUE,
  ["li bia velo"] = { "Li Bia Velo", "https://api.cyclocity.fr/contracts/namur/gbfs/gbfs.json" },
}

local W = gfx.width
local MOST = 5       -- stations on one card
local EVERY = 90000 -- ms between questions while a card is on screen
local SOON = 20000  -- ms: never ask again sooner than this
local BIKE, DOCK, ELECTRIC = "#ff5a3c", "#3c8cff", "#f1c40f"

local system, feedUrl            -- the bike share's name, and the address of its gbfs.json
local feeds                      -- name -> address, from gbfs.json
local found = false              -- the station list has been read
local busy, askedAt = false, nil -- a question is out, and when the last one went
local all = {}                   -- card id -> card
local lit = false                -- this plugin turned the LED on

local function problemFrom(res)
  if res.error then return res.error == "no WiFi" and "No WiFi" or ("No answer from " .. system) end
  return system .. " answered " .. res.status
end

local function each(fn)
  for _, card in pairs(all) do fn(card) end
end

-- A card watches one or more stations. Each is a "spot": what was asked for
-- (spot.wanted), the station that was found for it (id, name, number) and its
-- counts (bikes, docks, ebikes, closed, at).
local function eachSpot(fn)
  each(function(card)
    for _, spot in ipairs(card.spots) do fn(spot, card) end
  end)
end

-- A station list takes a good part of the plugin's memory: let it go before the next one comes.
local function tidy(res)
  res.json = nil
  collectgarbage()
end

-- ------------------------------------------------------------ the counts

local function showLed()
  local low = false
  eachSpot(function(spot, card)
    local limit = card.config.alert
    if limit > 0 and spot.bikes and not spot.closed and spot.bikes <= limit then low = true end
  end)
  if low then
    led.set("#ff9900")
    lit = true
  elseif lit then
    led.off()
    lit = false
  end
end

-- Four fields per station (five with e-bikes): every one more makes the list noticeably larger.
local STATUS = { last_updated = true, data = { stations = { {
  station_id = true, num_bikes_available = true, num_docks_available = true, num_ebikes_available = true,
  is_renting = true,
} } } }

-- Station id -> the spots that show it: the same station may be on two cards.
local function spotsById()
  local byId, any = {}, false
  eachSpot(function(spot)
    if spot.id then
      byId[spot.id] = byId[spot.id] or {}
      table.insert(byId[spot.id], spot)
      any = true
    end
  end)
  return byId, any
end

local function counts()
  local _, wanted = spotsById()
  if busy or not found or not wanted then return end
  if askedAt and time.ms() - askedAt < SOON then return end
  busy, askedAt = true, time.ms()
  http.get(feeds.station_status, { only = STATUS, timeout = 15 }, function(res)
    busy = false
    local list = res.json and res.json.data and res.json.data.stations
    if not res.ok or type(list) ~= "table" then
      eachSpot(function(spot) if not spot.bikes then spot.problem = problemFrom(res) end end)
      return
    end
    local byId = spotsById()
    local at = tonumber(res.json.last_updated) or time.now()
    for _, item in ipairs(list) do
      local id = tostring(item.station_id)
      for _, spot in ipairs(byId[id] or {}) do
        spot.problem = nil
        spot.bikes = tonumber(item.num_bikes_available) or 0
        spot.docks = tonumber(item.num_docks_available)
        spot.ebikes = tonumber(item.num_ebikes_available)
        spot.closed = item.is_renting == false or item.is_renting == 0
        spot.at = at
      end
      byId[id] = nil
    end
    for _, spots in pairs(byId) do
      for _, spot in ipairs(spots) do spot.problem = system .. " no longer lists this station" end
    end
    list = nil
    tidy(res)
    showLed()
  end)
end

-- ------------------------------------------------------------ the stations

-- A number matches the station's number; text matches (part of) its name.
local function match(wanted, stations)
  local number = wanted:match("^%d+$") and tonumber(wanted)
  local lower, partial = wanted:lower(), nil
  for _, item in ipairs(stations) do
    local id = tostring(item.station_id)
    local name = tostring(item.name or id)
    if number then
      if tonumber(id) == number or tonumber(name:match("^(%d+)")) == number then return id, name end
    elseif name:lower() == lower then
      return id, name
    elseif not partial and lower ~= "" and name:lower():find(lower, 1, true) then
      partial = { id, name }
    end
  end
  if partial then return partial[1], partial[2] end
end

local function place(card, stations)
  local names = {}
  for _, spot in ipairs(card.spots) do
    local id, name = match(spot.wanted, stations)
    if id then
      -- Velo puts the number in front of the name: "001- Centraal Station - Astrid".
      local number, rest = name:match("^(%d+)%s*%-%s*(.+)$")
      spot.id, spot.name, spot.number, spot.problem = id, rest or name, number, nil
      names[#names + 1] = #card.spots == 1 and name or spot.name
    else
      spot.problem = system .. " has no station " .. spot.wanted
    end
  end
  card.problem = nil
  if #names > 0 then card.title = table.concat(names, ", ") end
end

local function stations()
  busy, askedAt = true, time.ms()
  local wanted = { data = { stations = { { station_id = true, name = true } } } }
  http.get(feeds.station_information, { only = wanted, timeout = 15 }, function(res)
    busy = false
    local list = res.json and res.json.data and res.json.data.stations
    if not res.ok or type(list) ~= "table" then
      each(function(card) card.problem = problemFrom(res) end)
      return
    end
    found = true
    each(function(card) place(card, list) end)
    list = nil
    tidy(res)
    askedAt = nil -- not a question about the counts: they may come straight away
    counts()
  end)
end

-- gbfs.json says where the other files are, per language.
local function discover()
  busy, askedAt = true, time.ms()
  http.get(feedUrl, function(res)
    busy = false
    local data = res.json and res.json.data
    if not res.ok or type(data) ~= "table" then
      each(function(card) card.problem = res.ok and "That address is not a GBFS feed" or problemFrom(res) end)
      return
    end
    if tostring(res.json.version):sub(1, 1) == "3" then
      each(function(card) card.problem = "This is a GBFS 3 feed, too large for DeskHog. Try the system's GBFS 2 feed" end)
      return
    end
    local language = data.en or data.nl or data.fr or data.de
    if not language then
      for _, value in pairs(data) do language = language or value end
    end
    local list = type(language) == "table" and language.feeds
    local urls = {}
    for _, feed in ipairs(type(list) == "table" and list or {}) do
      if type(feed) == "table" and feed.name and feed.url then urls[feed.name] = feed.url end
    end
    if not urls.station_information or not urls.station_status then
      each(function(card) card.problem = "This feed has no stations" end)
      return
    end
    feeds = urls
    stations()
  end)
end

-- Take the next step: find the files, then the stations, then the counts.
local function start()
  if not system then
    local choice = config.system:match("^%s*(.-)%s*$")
    local known = SYSTEMS[choice:lower()]
    system, feedUrl = known and known[1] or "The feed", known and known[2] or choice
  end
  if busy then return end
  -- A card that was added after the station list was read still has to find its stations.
  local unplaced = false
  eachSpot(function(spot) unplaced = unplaced or not (spot.id or spot.problem) end)
  if not feeds then
    discover()
  elseif not found or unplaced then
    stations()
  else
    counts()
  end
end

-- ------------------------------------------------------------ drawing

local function fit(text, font, width)
  if gfx.textWidth(text, font) <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "...", font) > width do text = text:sub(1, -2) end
  return text .. "..."
end

local function ago(at)
  local now = time.now()
  if not at or now == 0 then return nil end
  local seconds = math.max(0, now - at)
  if seconds < 60 then return "Updated just now" end
  if seconds < 3600 then return "Updated " .. seconds // 60 .. " min ago" end
  if seconds < 172800 then return "Updated " .. seconds // 3600 .. " h ago" end
  return "Updated " .. seconds // 86400 .. " days ago"
end

local function wheel(x, y, color)
  gfx.circle(x, y, 7, color)
  gfx.circle(x, y, 5, "black")
end

-- A bike, 30 by 18, with its top left at x, y.
local function bike(x, y, color)
  wheel(x + 7, y + 11, color)
  wheel(x + 23, y + 11, color)
  gfx.line(x + 7, y + 11, x + 13, y + 4, color, 2)  -- seat stay
  gfx.line(x + 13, y + 4, x + 21, y + 4, color, 2)  -- top tube
  gfx.line(x + 7, y + 11, x + 15, y + 11, color, 2) -- chain stay
  gfx.line(x + 15, y + 11, x + 13, y + 4, color, 2) -- seat tube
  gfx.line(x + 15, y + 11, x + 21, y + 4, color, 2) -- down tube
  gfx.line(x + 23, y + 11, x + 20, y + 1, color, 2) -- fork
  gfx.line(x + 18, y + 1, x + 23, y + 1, color, 2)  -- handlebar
  gfx.line(x + 10, y + 2, x + 15, y + 2, color, 2)  -- saddle
end

-- A parking sign, 20 by 20.
local function dock(x, y, color)
  gfx.rect(x, y, 20, 20, color, 4)
  gfx.text(x + 10, y + 1, "P", "white", "medium", "center")
end

local function number(x, y, value, color)
  gfx.text(x, y, tostring(value), color, "large")
end

local function message(text, color)
  for i, line in ipairs(gfx.wrap(text, W - 16, "small")) do
    gfx.text(8, 34 + (i - 1) * 18, line, color, "small")
  end
end

-- One station, large: the page of a card with one station, and the pages after
-- the list on a card with several.
local function drawStation(card, spot, paged)
  local right = spot.number and gfx.textWidth(spot.number, "small") + 12 or 8
  gfx.text(8, 4, fit(spot.name or spot.wanted, "small", W - 8 - right), "gray", "small")
  if spot.number then gfx.text(W - 8, 4, spot.number, "gray", "small", "right") end

  local problem = spot.problem or card.problem
  if problem and not spot.bikes then return message(problem, "orange") end
  if not spot.bikes then
    return message(spot.id and "Counting the bikes..." or "Finding the station...", "gray")
  end

  local limit = card.config.alert
  local low = limit > 0 and spot.bikes <= limit
  local dim = spot.closed

  bike(10, 44, dim and "gray" or BIKE)
  number(46, 32, spot.bikes, dim and "gray" or (low and "orange" or "white"))
  gfx.text(46, 72, spot.bikes == 1 and "bike" or "bikes", "gray", "small")
  if spot.ebikes and spot.ebikes > 0 and not dim then
    gfx.text(46, 89, spot.ebikes .. " electric", ELECTRIC, "small")
  end

  if spot.docks then
    dock(130, 43, dim and "gray" or DOCK)
    number(158, 32, spot.docks, dim and "gray" or "white")
    gfx.text(158, 72, spot.docks == 1 and "free dock" or "free docks", "gray", "small")
  end

  -- With pages the bottom edge belongs to the dots.
  local lift = paged and 6 or 0
  if spot.closed then
    gfx.text(8, 94 - lift, "Out of service", "red", "medium")
  elseif problem then
    gfx.text(8, 96 - lift, fit(problem, "small", W - 16), "orange", "small")
  end
  local age = ago(spot.at)
  if age then gfx.text(8, 114 - lift, age, "gray", "small") end
end

-- All of a card's stations, a line each: bikes and free docks.
local function drawList(card)
  local docksAt, bikesAt = W - 8, W - 62
  gfx.text(8, 4, system or "", "gray", "small")
  gfx.text(bikesAt, 4, "bikes", BIKE, "small", "right")
  gfx.text(docksAt, 4, "docks", DOCK, "small", "right")

  local limit = card.config.alert
  for i, spot in ipairs(card.spots) do
    local y = 26 + (i - 1) * 20
    local problem = spot.problem or card.problem
    if spot.bikes then
      local low = limit > 0 and spot.bikes <= limit
      gfx.text(8, y, fit(spot.name, "small", bikesAt - 44), spot.closed and "gray" or "white", "small")
      if spot.closed then
        gfx.text(docksAt, y, "closed", "red", "small", "right")
      else
        gfx.text(bikesAt, y, tostring(spot.bikes), low and "orange" or "white", "small", "right")
        gfx.text(docksAt, y, spot.docks and tostring(spot.docks) or "-", "white", "small", "right")
      end
    elseif problem and not spot.id then
      gfx.text(8, y, fit(found and spot.wanted .. ": not found" or problem, "small", W - 16), "orange", "small")
    else
      gfx.text(8, y, fit(spot.name or spot.wanted, "small", bikesAt - 44), "gray", "small")
      gfx.text(docksAt, y, problem and "no answer" or "...", problem and "orange" or "gray", "small", "right")
    end
  end
end

cards = {
  station = {
    name = "Station",
    config = {
      station = {
        label = "Station number or (part of the) name. Several, up to five: separate them with commas",
        default = "1",
        order = 1,
      },
      alert = { label = "LED amber at this many bikes or fewer (0: never)", default = 0, order = 2 },
    },

    load = function(card)
      all[card.id] = card
      if not card.spots then
        card.spots = {}
        for wanted in (card.config.station .. ","):gmatch("%s*(.-)%s*,") do
          if wanted ~= "" and #card.spots < MOST then card.spots[#card.spots + 1] = { wanted = wanted } end
        end
        if #card.spots == 0 then card.spots[1] = { wanted = "" } end
      end
      local waiting = false
      for _, spot in ipairs(card.spots) do waiting = waiting or not spot.bikes end
      if waiting or card.config.alert > 0 then start() end
    end,

    -- Only while the card is on screen: ask again when the counts are old, and
    -- repaint when the minute of "Updated ... ago" changes.
    update = function(card)
      if not askedAt or time.ms() - askedAt >= EVERY then start() end
      local minute = time.now() // 60
      if minute == card.minute then return false end
      card.minute = minute
    end,

    -- A card with several stations has the list first, then a page per station.
    pages = function(card)
      local stations = card.spots and #card.spots or 1
      return stations > 1 and stations + 1 or 1
    end,

    draw = function(card, page)
      local spots = card.spots
      if not spots then return end
      if #spots == 1 then return drawStation(card, spots[1], false) end
      if page == 1 then return drawList(card) end
      drawStation(card, spots[page - 1], true)
    end,
  },
}
