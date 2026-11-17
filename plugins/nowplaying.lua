-- Now playing: the track you are listening to, or the last one you did, with
-- its album cover. From Last.fm, which hears it from Spotify, Apple Music and
-- most other players once you connect them ("scrobbling").
--
-- You need your Last.fm user name and an API key: make one at
-- last.fm/api/account/create, any name will do.
--
-- This plugin shows a picture from the network. gfx.fetchImage downloads a
-- PNG or JPEG, scales it down to the box you give and hands back an image to
-- draw. Pictures use a lot of memory for a small device, so the old cover is
-- released when the track changes.

plugin = {
  name = "Now playing",
  description = "What you are listening to, with the album cover, from Last.fm",
  author = "DeskHog",
  version = "1.1",
  refresh = 20,
  fps = 8,   -- only for the three dancing bars
}

config = {
  user = { label = "Last.fm user name", default = "", order = 1 },
  key = { label = "API key", default = "", secret = true, order = 2 },
}

local W, H = gfx.width, gfx.height
local PAD = 8
local ART = 107                      -- the cover's box: the picture is scaled to fit it
local ART_Y = (H - ART) // 2
local TEXT_X = PAD + ART + 10
local TEXT_W = W - PAD - TEXT_X
local ORANGE, GRAY, SOFT = "#f39c12", "#8a8f98", "#c8ccd4"

-- Last.fm has no "no cover": it sends this grey star instead.
local NO_COVER = "2a96cbd8b46e442fc41c2b86b821562f"

local track                          -- { name, artist, album, playing, at, lines... }
local cover                          -- the image on the card, if there is one
local coverUrl                       -- where `cover` came from, or is coming from
local problem
local answered = false               -- whether Last.fm has answered yet
local beat = 0

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

local coverPending = false           -- a fetch is on its way
local coverTries = 0                 -- fetches of coverUrl that failed or are under way

-- Last.fm's image server converts a cover to PNG when you ask for .png. Some
-- of its JPEGs are progressive, a kind the device cannot read; the PNG works.
local function asPng(url)
  return (url:gsub("%.jpe?g$", ".png"))
end

local function fetchCover(url, from)
  gfx.fetchImage(from, { width = ART, height = ART }, function(image, err)
    if url ~= coverUrl then          -- the track changed again while this one was on its way
      if image then gfx.release(image) end
      return
    end
    if not image and tostring(err):find("progressive", 1, true) and from ~= asPng(url) then
      fetchCover(url, asPng(url))    -- the same cover, as a PNG
      return
    end
    coverPending = false
    cover = image                    -- nil if it failed: the card draws a record instead
    if not image then print("No cover (try " .. coverTries .. "): " .. tostring(err)) end
  end)
end

-- Fetch the cover, unless it is the one we have. Two tracks of an album share
-- one. A fetch that failed (often for lack of memory while other cards were
-- busy) is tried again on the next refresh, three times in all.
local function showCover(url)
  if url == coverUrl and (cover or coverPending or coverTries >= 3) then return end
  if url ~= coverUrl then
    coverUrl = url
    coverTries = 0
    coverPending = false
    if cover then
      gfx.release(cover)             -- give its memory back before asking for the next
      cover = nil
    end
  end
  if not url then return end
  coverTries = coverTries + 1
  coverPending = true
  fetchCover(url, url)
end

-- The largest of Last.fm's sizes that is still a small download: 174 pixels.
local function coverOf(images)
  local found
  for _, image in ipairs(images or {}) do
    local url = image["#text"]
    if url and url ~= "" and not url:find(NO_COVER, 1, true) then
      if image.size == "large" then return url end
      found = url
    end
  end
  return found
end

-- At most `most` lines of the text, the last one ending in dots when there was more.
local function lines(text, font, most)
  local all = gfx.wrap(text, TEXT_W, font)
  if #all <= most then return all end
  local kept = {}
  for i = 1, most do kept[i] = all[i] end
  kept[most] = kept[most] .. "..."
  return kept
end

local function ago(at)
  local now = time.now()
  if not at or now == 0 then return "Last played" end
  local age = math.max(0, now - at)
  if age < 90 then return "Just now" end
  if age < 3600 then return age // 60 .. " min ago" end
  if age < 86400 * 2 then return age // 3600 .. " h ago" end
  return age // 86400 .. " days ago"
end

function load()
  if config.user == "" or config.key == "" then
    problem = "Set your Last.fm user name and API key on the web page"
    return
  end
  local url = "https://ws.audioscrobbler.com/2.0/?method=user.getrecenttracks&limit=1&format=json"
    .. "&user=" .. urlencode(config.user) .. "&api_key=" .. urlencode(config.key)

  -- Last.fm's keys have odd names, "#text" and "@attr": in Lua those need
  -- brackets. A filter without a list in it fits both one track and a list of them.
  local wanted = {
    error = true, message = true,
    recenttracks = { track = {
      name = true,
      artist = { ["#text"] = true },
      album = { ["#text"] = true },
      image = { { size = true, ["#text"] = true } },
      date = { uts = true },
      ["@attr"] = { nowplaying = true },
    } },
  }
  http.get(url, { only = wanted }, function(res)
    local answer = res.json
    local list = answer and answer.recenttracks and answer.recenttracks.track
    if not list then
      -- Last.fm explains itself in the answer: "User not found", "Invalid API key".
      problem = answer and answer.message or res.error or ("Last.fm answered " .. res.status)
      return
    end
    problem, answered = nil, true
    -- While something plays the list has it first, before the last finished track.
    local latest = list[1] or (list.name and list)
    if not latest then
      track = nil
      showCover(nil)
      return
    end

    local name = latest.name or "?"
    local artist = latest.artist and latest.artist["#text"] or ""
    local album = latest.album and latest.album["#text"] or ""
    -- Wrapped here, once, instead of on every repaint. Three lines fit beside the
    -- cover: a long title takes two of them, otherwise the artist may.
    local title = lines(name, "medium", 2)
    track = {
      playing = latest["@attr"] ~= nil and latest["@attr"].nowplaying == "true",
      at = latest.date and tonumber(latest.date.uts),
      name = title,
      artist = lines(artist, "small", 3 - #title),
      album = lines(album, "tiny", 1),
    }
    showCover(coverOf(latest.image))
  end)
end

-- Only the bars move, and only while something plays: otherwise there is nothing to repaint.
function update(dt)
  if not (track and track.playing) then return false end
  beat = beat + dt
end

-- A record, for a track without a cover and while the cover is on its way.
local function drawRecord()
  local x, y = PAD + ART / 2, ART_Y + ART / 2
  gfx.rect(PAD, ART_Y, ART, ART, "#1c1f26", 6)
  gfx.circle(x, y, 42, "#07080a")
  gfx.circle(x, y, 36, "#23262e", 1)         -- with a width: a ring, here a groove
  gfx.circle(x, y, 29, "#23262e", 1)
  gfx.circle(x, y, 22, "#23262e", 1)
  gfx.circle(x, y, 14, ORANGE)
  gfx.circle(x, y, 2, "#07080a")
end

local function drawBars(x, y)
  for i = 0, 2 do
    local height = 3 + 3 * (1 + math.sin(beat * (7 + i * 2.5) + i * 2))
    gfx.rect(x + i * 4, y + 9 - height, 3, height, ORANGE)
  end
end

function draw()
  if problem and not track then
    ui.title("Now playing")
    ui.label(problem, "orange")
    return
  end
  if not track then
    ui.title("Now playing")
    ui.label(answered and "Nothing played yet" or "Loading...")
    return
  end

  if cover then
    -- Scaling keeps the picture's shape, so one that is not square is smaller than its box.
    gfx.draw(cover, PAD + (ART - cover.width) // 2, ART_Y + (ART - cover.height) // 2)
  else
    drawRecord()
  end

  local y = ART_Y + 1
  if track.playing then
    drawBars(TEXT_X, y)
    gfx.text(TEXT_X + 15, y, "NOW PLAYING", ORANGE, "tiny")
  else
    gfx.text(TEXT_X, y, ago(track.at), GRAY, "tiny")
  end
  y = y + 17

  gfx.clip(TEXT_X, 0, TEXT_W, H)             -- one long word cannot run off the card
  for _, line in ipairs(track.name) do
    gfx.text(TEXT_X, y, line, "white", "medium")
    y = y + gfx.textHeight("medium")
  end
  y = y + 3
  for _, line in ipairs(track.artist) do
    gfx.text(TEXT_X, y, line, SOFT, "small")
    y = y + gfx.textHeight("small")
  end
  if track.album[1] then
    gfx.text(TEXT_X, ART_Y + ART - gfx.textHeight("tiny"), track.album[1], GRAY, "tiny")
  end
  gfx.clip()
end
