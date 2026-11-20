-- The top stories on Hacker News, one at a time, with points, comments, the
-- site and how old they are. A new story every 20 seconds; centre for the next
-- one right away. From the official Hacker News API on Firebase, which is free
-- and needs no key.
--
-- The list of stories asks Firebase for only the first few ids
-- (limitToFirst), and each story is read with a filter that leaves out its
-- list of comment ids, which can run to thousands for a busy story.

plugin = {
  name = "Hacker News",
  description = "The top stories on Hacker News, one at a time, with points and comments",
  author = "DeskHog",
  version = "1.0",
  refresh = 600,
  fps = 1,
}

config = {
  list = { label = "Stories", default = "top", options = { "top", "best", "new", "ask", "show" }, order = 1 },
  count = { label = "How many", default = 10, order = 2 },
  advance = { label = "Seconds before the next story (0 to stay)", default = 20, order = 3 },
  alert = { label = "Light up for a story with this many points (0 for never)", default = 0, order = 4 },
}

local API = "https://hacker-news.firebaseio.com/v0/"
local ORANGE = "#ff6600"
local W, H = gfx.width, gfx.height
local PAD = 8
local TOP, BOTTOM = 26, H - 20      -- the title goes between the header and the footer
local AT_ONCE = 4                   -- story requests in flight at the same time

local stories                       -- nil until the first answer
local current = 1
local problem
local shownAt = 0                   -- time.ms() when the story came on screen
local lines, font = {}, "medium"
local alerted = {}                  -- ids that already lit the LED
local firstLoad = true

-- "github.com" from "https://www.github.com/foo".
local function site(url)
  if not url then return nil end
  local host = url:match("^%a+://([^/:?#]+)")
  return host and host:gsub("^www%.", "")
end

-- "5m", "3h" or "2d" since a moment given as Unix time.
local function age(at)
  local now = time.now()
  if now == 0 or not at then return nil end
  local seconds = math.max(0, now - at)
  if seconds < 3600 then return math.max(1, seconds // 60) .. "m" end
  if seconds < 86400 then return seconds // 3600 .. "h" end
  return seconds // 86400 .. "d"
end

-- Medium if the title fits, otherwise small, and cut off what still does not fit.
local function layout()
  local story = stories and stories[current]
  if not story then lines = {} return end
  local room = BOTTOM - TOP
  font = "medium"
  lines = gfx.wrap(story.title, W - 2 * PAD, font)
  if #lines * gfx.textHeight(font) > room then
    font = "small"
    lines = gfx.wrap(story.title, W - 2 * PAD, font)
  end
  local fits = room // gfx.textHeight(font)
  if #lines > fits then
    for i = #lines, fits + 1, -1 do lines[i] = nil end
    local last = lines[fits]
    while #last > 1 and gfx.textWidth(last .. "...", font) > W - 2 * PAD do last = last:sub(1, -2) end
    lines[fits] = last .. "..."
  end
end

local function show(index)
  current = index
  shownAt = time.ms()
  layout()
end

-- Light the LED once for each story that reaches the threshold, but not for the
-- ones that were already there when the device started.
local function checkAlert(list)
  if config.alert <= 0 then return end
  for _, story in ipairs(list) do
    if story.score >= config.alert and not alerted[story.id] then
      alerted[story.id] = true
      if not firstLoad then led.set(ORANGE, 3) end
    end
  end
  firstLoad = false
end

local function fetchStories(ids)
  local found, asked, waiting = {}, 0, #ids
  local wanted = { id = true, title = true, url = true, score = true, descendants = true, time = true,
    by = true, dead = true, deleted = true }

  local function finish()
    local list = {}
    for i = 1, #ids do
      if found[i] then list[#list + 1] = found[i] end
    end
    if #list == 0 then
      if not stories then problem = "No stories came back" end
      return
    end
    -- Stay on the same story if it is still in the list.
    local keep = stories and stories[current] and stories[current].id
    stories, problem = list, nil
    local at = 1
    for i, story in ipairs(list) do
      if story.id == keep then at = i end
    end
    show(at)
    checkAlert(list)
  end

  local function ask()
    local i = asked + 1
    if i > #ids then return end
    asked = i
    http.get(API .. "item/" .. ids[i] .. ".json", { only = wanted }, function(res)
      local item = res.json
      if item and item.title and not item.dead and not item.deleted then
        found[i] = {
          id = item.id,
          rank = i,
          title = tostring(item.title),
          site = site(item.url),
          by = item.by,
          score = tonumber(item.score) or 0,
          comments = tonumber(item.descendants),
          time = tonumber(item.time),
        }
      end
      waiting = waiting - 1
      if waiting == 0 then finish() else ask() end
    end)
  end

  for _ = 1, math.min(AT_ONCE, #ids) do ask() end
end

function load()
  local count = math.max(1, math.min(30, math.floor(config.count)))
  local url = API .. config.list .. 'stories.json?orderBy="$key"&limitToFirst=' .. count
  http.get(url, function(res)
    if not res.ok or type(res.json) ~= "table" then
      if not stories then problem = res.error and "No connection" or ("Hacker News answered " .. res.status) end
      return
    end
    local ids = {}
    for _, id in ipairs(res.json) do
      if type(id) == "number" then ids[#ids + 1] = math.floor(id) end
    end
    if #ids == 0 then
      if not stories then problem = "No stories right now" end
      return
    end
    fetchStories(ids)
  end)
end

-- The next story every few seconds. Measured with time.ms(): at one frame a
-- second, dt is capped and would not add up to the real time.
function update()
  if not stories or #stories < 2 or config.advance <= 0 then return false end
  if time.ms() - shownAt < config.advance * 1000 then return false end
  show(current % #stories + 1)
end

function draw()
  if not stories then
    gfx.text(PAD, 6, "Hacker News", ORANGE, "medium")
    gfx.text(PAD, 34, problem or "Loading...", problem and "orange" or "gray", "small")
    return
  end
  local story = stories[current]

  -- Header: the rank in an orange box, where it goes and how old it is.
  local rank = tostring(story.rank)
  local boxW = gfx.textWidth(rank, "medium") + 10
  gfx.rect(PAD, 4, boxW, 19, ORANGE, 4)
  gfx.text(PAD + boxW / 2, 5, rank, "white", "medium", "center")
  local old = age(story.time)
  local right = W - PAD
  if old then
    gfx.text(right, 6, old, "white", "small", "right")
    right = right - gfx.textWidth(old, "small") - 8
  end
  local where = story.site or (story.by and "by " .. story.by) or ""
  local room = right - (PAD + boxW + 8)
  if gfx.textWidth(where, "small") > room then
    -- Keep the end of a long site name: "docs.example.com" says more as "...ample.com".
    while #where > 1 and gfx.textWidth("..." .. where, "small") > room do where = where:sub(2) end
    where = "..." .. where
  end
  gfx.text(right, 6, where, "gray", "small", "right")

  -- The title, centred in the space it has.
  local lineHeight = gfx.textHeight(font)
  local y = TOP + (BOTTOM - TOP - #lines * lineHeight) // 2
  for _, line in ipairs(lines) do
    gfx.text(PAD, y, line, "white", font)
    y = y + lineHeight
  end

  -- Footer: points and comments.
  local footer = story.score .. (story.score == 1 and " point" or " points")
  if story.comments then
    footer = footer .. " · " .. story.comments .. (story.comments == 1 and " comment" or " comments")
  end
  if gfx.textWidth(footer, "small") > W - 2 * PAD then footer = footer:gsub("points", "pts") end
  gfx.text(PAD, BOTTOM + 2, footer, ORANGE, "small")
end

-- Centre goes to the next story; up and down move between cards as usual.
function button(name)
  if name ~= "center" or not stories or #stories == 0 then return false end
  show(current % #stories + 1)
  return true
end
