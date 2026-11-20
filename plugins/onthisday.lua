-- What happened on today's date in years gone by, one event at a time: the
-- year, how long ago that was, and what happened. A new one every 30 seconds;
-- centre for the next one right away.
--
-- The highlights come from Wikipedia's "On this day" feed, in the language you
-- pick. That feed sends the full summary of every article it links to, so even
-- the short list of highlights runs to 100-280 KB, and the complete lists of
-- events, births and deaths to half a megabyte or more: too much for the
-- device. Those three come from byabbe.se instead, a copy of the English
-- Wikipedia's date pages (free, no key), which is 15-80 KB. On the few days the
-- English highlights are too large as well, the card shows that day's events.
--
-- The list is shuffled: the lists run oldest first, and the long ones (births
-- can be 400 names) would otherwise stay in the Middle Ages all morning.
-- Part of today's list is saved, so a restart shows something straight away.

plugin = {
  name = "On this day",
  description = "Events, births and deaths from Wikipedia that happened on today's date",
  author = "DeskHog",
  version = "1.0",
  refresh = 3600, -- look once an hour whether the date has changed, also while another card is on screen
  fps = 1,
}

config = {
  kind = { label = "What to show: Wikipedia's selected highlights, or all events, births or deaths (those three are in English only)",
    default = "selected", options = { "selected", "events", "births", "deaths" }, order = 1 },
  language = { label = "Language of the highlights (French works; there is no Dutch feed)", default = "en",
    options = { "en", "fr", "de", "es", "it", "pt", "sv" }, order = 2 },
  advance = { label = "Seconds before the next one (0 to stay)", default = 30, order = 3 },
  offset = { label = "Hours ahead of UTC, for when the day changes (for example 1, -5 or 5.5)", default = "0", order = 4 },
}

local ACCENT = "#f2b84b"
local W, H = gfx.width, gfx.height
local PAD = 8
local TOP, BOTTOM = 42, H - 1    -- the text goes between the year and the bottom edge
local KEEP = 60                  -- items kept in memory; long lists are sampled
local CACHE_CHUNKS, CHUNK_BYTES = 3, 1900 -- store values are at most 2 KB, 8 KB in all
local HEADERS = {
  -- Wikimedia asks for a User-Agent that says who is asking. The device sends its
  -- own, so the same goes in Api-User-Agent, which Wikimedia reads too.
  ["User-Agent"] = "DeskHog/1.0 (https://github.com/TimBroddin/DeskHog)",
  ["Api-User-Agent"] = "DeskHog/1.0 (https://github.com/TimBroddin/DeskHog)",
}
local MONTHS = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
  "November", "December" }
local HEADINGS = { selected = "", events = "", births = "Born ", deaths = "Died " }

local items                      -- { { year = 1152, text = "..." }, ... } or nil
local current = 1
local shownAt = 0                -- time.ms() when the item came on screen
local lines, font = {}, "medium"
local today                      -- { key, year, month, day } of the list in items
local fetching                   -- the key being fetched, while a request is out
local problem, failedAt
local partial = false            -- the list came from the saved part, not the whole of it

-- Days since 1970 to year, month and day.
local function date(days)
  local year = 1970
  while true do
    local leap = year % 4 == 0 and (year % 100 ~= 0 or year % 400 == 0)
    local length = leap and 366 or 365
    if days < length then
      local lengths = { 31, leap and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
      for month, monthLength in ipairs(lengths) do
        if days < monthLength then return year, month, days + 1 end
        days = days - monthLength
      end
    end
    days = days - length
    year = year + 1
  end
end

-- Today in local time, and what a list for it is saved under. Nil until the clock is set.
local function localDay()
  local utc = time.now()
  if utc == 0 then return nil end
  local seconds = utc + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)
  local year, month, day = date(seconds // 86400)
  local key = string.format("%d-%02d-%02d %s %s", year, month, day, config.kind,
    config.kind == "selected" and config.language or "en")
  return { key = key, year = year, month = month, day = day }
end

-- The fonts have Western European letters only. Punctuation and the letters
-- that turn up most in names get a stand-in; anything else stays as it is.
local STAND_IN = {
  ["\u{2010}"] = "-", ["\u{2011}"] = "-", ["\u{2013}"] = "-", ["\u{2014}"] = "-", ["\u{2212}"] = "-",
  ["\u{2018}"] = "'", ["\u{2019}"] = "'", ["\u{201C}"] = '"', ["\u{201D}"] = '"', ["\u{201E}"] = '"',
  ["\u{2026}"] = "...", ["\u{2009}"] = " ", ["\u{202F}"] = " ", ["\u{200B}"] = "", ["\u{00AD}"] = "",
  ["ł"] = "l", ["Ł"] = "L", ["ő"] = "ö", ["ű"] = "ü", ["č"] = "c", ["Č"] = "C", ["ć"] = "c", ["š"] = "s",
  ["Š"] = "S", ["ž"] = "z", ["Ž"] = "Z", ["ř"] = "r", ["ě"] = "e", ["ş"] = "s", ["Ş"] = "S", ["ğ"] = "g",
  ["ı"] = "i", ["İ"] = "I", ["ą"] = "a", ["ę"] = "e", ["ń"] = "n", ["ś"] = "s", ["Ś"] = "S", ["ź"] = "z",
  ["ż"] = "z", ["Ż"] = "Z", ["ă"] = "a", ["ț"] = "t", ["ș"] = "s", ["ā"] = "a", ["ē"] = "e", ["ī"] = "i",
  ["ō"] = "o", ["ū"] = "u", ["đ"] = "d", ["Đ"] = "D",
}

local PICTURED = {}
for _, aside in ipairs({ "pictured", "pictured left", "pictured right", "im Bild", "photo", "illustration",
  "en la imagen", "en la foto", "nella foto", "na imagem", "na foto", "på bilden", "bilden" }) do
  PICTURED["(" .. aside .. ")"] = true
end

-- Plain text for the screen: letters the fonts have, no "(pictured)" for a
-- picture that is not there, and a capital to start with.
local function tidy(text)
  text = tostring(text):gsub("[\xC2-\xF4][\x80-\xBF]*", STAND_IN)
  text = text:gsub("%s*(%b())", function(aside) if PICTURED[aside] then return "" end end)
  text = text:gsub("%s+", " "):match("^%s*(.-)%s*$")
  text = text:gsub("%((%a)%. (%d)", "(%1.\194\160%2") -- keep "(d. 1942)" on one line
  return (text:gsub("^%l", string.upper))
end

-- "AD 51" is 51, "44 BC" is -44.
local function parseYear(value)
  if type(value) == "number" then return math.floor(value) end
  local digits = tostring(value or ""):match("%d+")
  if not digits then return nil end
  local year = tonumber(digits)
  if tostring(value):find("BC") then year = -year end
  return year
end

-- The year in large digits, with "BC" smaller after it. Returns how wide it was.
local function drawYear(year, x, y)
  local digits = tostring(math.abs(year))
  gfx.text(x, y, digits, ACCENT, "large")
  local width = gfx.textWidth(digits, "large")
  if year < 0 then
    gfx.text(x + width + 4, y + 18, "BC", ACCENT, "medium")
    width = width + 4 + gfx.textWidth("BC", "medium")
  end
  return width
end

local function ago(year)
  if not today then return "" end
  local years = today.year - year
  if year < 0 then years = years - 1 end -- there was no year 0
  if years <= 0 then return "This year" end
  if years == 1 then return "A year ago" end
  return years .. " years ago"
end

-- Cut text to fit a width, ending in "...", without splitting a character.
local function cut(text, width, which)
  while #text > 0 and gfx.textWidth(text .. "...", which) > width do
    text = text:sub(1, (utf8.offset(text, -1) or #text) - 1)
  end
  return text .. "..."
end

-- Medium if the text fits, otherwise small, and cut off what still does not fit.
local function layout()
  local item = items and items[current]
  if not item then lines = {} return end
  local room = BOTTOM - TOP
  font = "medium"
  lines = gfx.wrap(item.text, W - 2 * PAD, font)
  if #lines * gfx.textHeight(font) > room then
    font = "small"
    lines = gfx.wrap(item.text, W - 2 * PAD, font)
  end
  local fits = room // gfx.textHeight(font)
  if #lines > fits then
    for i = #lines, fits + 1, -1 do lines[i] = nil end
    lines[fits] = cut(lines[fits], W - 2 * PAD, font)
  end
end

local function show(index)
  current = index
  shownAt = time.ms()
  layout()
end

local function shuffle(list)
  for i = #list, 2, -1 do
    local j = math.random(i)
    list[i], list[j] = list[j], list[i]
  end
end

-- Save the first items of the shuffled list, as much as the store holds.
local function save(key, list)
  local chunks, chunk, size, saved = {}, {}, 2, 0
  for _, item in ipairs(list) do
    local entry = item.year .. "|" .. item.text
    local bytes = #json.encode(entry) + 1
    if size + bytes > CHUNK_BYTES then
      chunks[#chunks + 1] = chunk
      if #chunks == CACHE_CHUNKS then break end
      chunk, size = {}, 2
    end
    chunk[#chunk + 1] = entry
    size = size + bytes
    saved = saved + 1
  end
  if #chunks < CACHE_CHUNKS and #chunk > 0 then chunks[#chunks + 1] = chunk end
  local ok = pcall(function()
    for i = 1, CACHE_CHUNKS do store.set("list" .. i, chunks[i] or {}) end
    store.set("day", key)
    store.set("partial", saved < #list)
  end)
  if not ok then print("Could not save today's list") end
end

local function restore()
  local key = store.get("day", "")
  if key == "" then return nil end
  local list = {}
  for i = 1, CACHE_CHUNKS do
    for _, entry in ipairs(store.get("list" .. i, {})) do
      local year, text = tostring(entry):match("^(%-?%d+)|(.+)$")
      if year then list[#list + 1] = { year = tonumber(year), text = text } end
    end
  end
  if #list == 0 then return nil end
  return key, list, store.get("partial", false)
end

-- A fresh list. If one was on screen, the item showing stays first.
local function accept(day, raw)
  -- Shuffle what came, then tidy only the ones kept: births can be 400 entries.
  shuffle(raw)
  local list = {}
  for _, entry in ipairs(raw) do
    local year = parseYear(entry.year)
    local text = entry.text or entry.description
    if year and type(text) == "string" and text ~= "" then list[#list + 1] = { year = year, text = tidy(text) } end
    if #list == KEEP then break end
  end
  if #list == 0 then
    if not items then problem = "Nothing found for today" end
    return
  end
  local showing = items and items[current]
  if showing and today and today.key == day.key then
    for i, item in ipairs(list) do
      if item.text == showing.text then table.remove(list, i) break end
    end
    table.insert(list, 1, showing)
  end
  items, today, problem, partial = list, day, nil, false
  save(day.key, list)
  show(1)
end

local function failed(res)
  failedAt = time.ms()
  if items then return end -- keep showing what there is
  if res.error then problem = "No connection"
  elseif res.status == 404 then problem = "Nothing for this language"
  else problem = "Wikipedia answered " .. tostring(res.status) end
end

-- All events, births or deaths, from byabbe.se (English Wikipedia).
local function fetchAll(day, kind)
  local url = string.format("https://byabbe.se/on-this-day/%d/%d/%s.json", day.month, day.day, kind)
  http.get(url, { only = { [kind] = { { year = true, description = true } } }, timeout = 20 }, function(res)
    fetching = nil
    local list = res.ok and res.json and res.json[kind]
    if type(list) ~= "table" then return failed(res) end
    accept(day, list)
  end)
end

local function fetch(day)
  fetching = day.key
  if config.kind ~= "selected" then return fetchAll(day, config.kind) end
  local url = string.format("https://%s.wikipedia.org/api/rest_v1/feed/onthisday/selected/%02d/%02d",
    config.language, day.month, day.day)
  http.get(url, { headers = HEADERS, only = { selected = { { year = true, text = true } } }, timeout = 25 },
    function(res)
      if res.error and res.error:find("too large") then
        print("The highlights are too large today; showing all events instead")
        return fetchAll(day, "events")
      end
      fetching = nil
      local list = res.ok and res.json and res.json.selected
      if type(list) ~= "table" then return failed(res) end
      accept(day, list)
    end)
end

-- Fetch when there is no list for today, or only the saved part of it.
local function check()
  local day = localDay()
  if not day then return end
  if fetching == day.key then return end
  if today and today.key == day.key and not partial then return end
  fetch(day) -- yesterday's list stays on screen until today's comes
end

function load()
  math.randomseed(time.ms() + time.now())
  if not items then
    local key, list, wasPartial = restore()
    local day = localDay()
    if key and (not day or day.key == key) then
      local year, month, dayOfMonth = key:match("^(%d+)-(%d+)-(%d+)")
      shuffle(list)
      items, partial = list, wasPartial
      today = { key = key, year = tonumber(year), month = tonumber(month), day = tonumber(dayOfMonth) }
      show(1)
    end
  end
  check()
end

function update()
  -- The date changes, or the clock was set after load(), or a failed request is due again.
  if not fetching and (not failedAt or time.ms() - failedAt > 300000) then
    local day = localDay()
    if day and (not today or today.key ~= day.key) then check() end
  end
  if not items or #items < 2 or config.advance <= 0 then return false end
  if time.ms() - shownAt < config.advance * 1000 then return false end
  show(current % #items + 1)
end

function draw()
  local item = items and items[current]
  if not item then
    gfx.text(PAD, 6, "On this day", ACCENT, "medium")
    local message = problem or (time.now() == 0 and "Waiting for the date..." or "Loading...")
    gfx.text(PAD, 34, message, problem and "orange" or "gray", "small")
    return
  end

  -- The year, large; on the right the date and how long ago.
  local room = W - 2 * PAD - drawYear(item.year, PAD - 1, 2) - 8
  if today then
    local heading = (HEADINGS[config.kind] or "") .. today.day .. " "
    local month = MONTHS[today.month]
    if gfx.textWidth(heading .. month, "small") > room then month = month:sub(1, 3) end
    gfx.text(W - PAD, 7, heading .. month, "gray", "small", "right")
  end
  gfx.text(W - PAD, 24, ago(item.year), "white", "small", "right")

  -- The text, centred in the space it has.
  local lineHeight = gfx.textHeight(font)
  local y = TOP + (BOTTOM - TOP - #lines * lineHeight) // 2
  for _, line in ipairs(lines) do
    gfx.text(PAD, y, line, "white", font)
    y = y + lineHeight
  end
end

-- Centre goes to the next one; up and down move between cards as usual.
function button(name)
  if name ~= "center" or not items or #items == 0 then return false end
  show(current % #items + 1)
  return true
end
