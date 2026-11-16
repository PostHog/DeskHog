-- Release radar: the latest release of up to five GitHub repositories on one
-- card, with how long ago each came out. From the GitHub API: the latest
-- release of each repository, or its newest tag when it makes no releases.
--
-- A release from the last 24 hours, or one you have not looked at yet, is
-- marked NEW, and the LED blinks green when one comes out while the device is
-- on. Centre goes through the repositories one at a time, which also marks
-- their release as seen, and then back to the list.
--
-- Without a token GitHub allows 60 requests an hour per address, shared with
-- anything else on your network that asks without one (the GitHub plugin, for
-- instance). This card asks once per repository every 15 minutes: 20 an hour
-- for five. A token (a fine-grained one with no extra permissions will do)
-- raises the limit to 5000 and lets you follow private repositories.

plugin = {
  name = "Release radar",
  description = "The latest release of the GitHub repositories you follow, with new ones marked",
  author = "DeskHog",
  version = "1.0",
  refresh = 900,
  fps = 1,
}

config = {
  repos = { label = "Repositories, as owner/name, separated by spaces (up to five)",
    default = "oven-sh/bun nodejs/node neovim/neovim", order = 1 },
  prereleases = { label = "Include pre-releases", default = false, order = 2 },
  light = { label = "Blink the LED when a new release comes out", default = true, order = 3 },
  token = { label = "Access token (optional)", default = "", secret = true, order = 4 },
}

local API = "https://api.github.com/repos/"
local W, H = gfx.width, gfx.height
local PAD = 8
local MOST = 5
local GREEN = "#2ecc71"
local DAY = 86400
local BACK_AFTER = 30000            -- ms before the card returns from one repository to the list

local repos = {}                    -- { key = "owner/name", name, tag, title, at, pre, unseen, problem, tags }
local seen                          -- "owner/name" -> the tag last looked at, kept in store
local showing = 0                   -- 0 for the list, otherwise the repository shown on its own
local pressedAt = 0

local function headers()
  local list = { Accept = "application/vnd.github+json" }
  if config.token ~= "" then list.Authorization = "Bearer " .. config.token end
  return list
end

-- Days since 1970-01-01 for a date, after Howard Hinnant's days_from_civil.
local function days(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * (m > 2 and m - 3 or m + 9) + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end

-- "2026-11-15T10:20:30Z" as Unix time.
local function parseTime(text)
  local y, mo, d, h, mi, s = tostring(text or ""):match("^(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)")
  if not y then return nil end
  return days(tonumber(y), tonumber(mo), tonumber(d)) * DAY + tonumber(h) * 3600 + tonumber(mi) * 60 + tonumber(s)
end

local function since(at)
  local now = time.now()
  if now == 0 or not at then return nil end
  return math.max(0, now - at)
end

-- "3h" for the list, "3 hours ago" when there is room.
local function short(at)
  local s = since(at)
  if not s then return "" end
  if s < 3600 then return math.max(1, s // 60) .. "m" end
  if s < DAY then return s // 3600 .. "h" end
  if s < 60 * DAY then return s // DAY .. "d" end
  if s < 365 * DAY then return s // (30 * DAY) .. "mo" end
  return s // (365 * DAY) .. "y"
end

local function long(at)
  local s = since(at)
  if not s then return nil end
  local n, unit
  if s < 3600 then n, unit = math.max(1, s // 60), "minute"
  elseif s < DAY then n, unit = s // 3600, "hour"
  elseif s < 60 * DAY then n, unit = s // DAY, "day"
  elseif s < 365 * DAY then n, unit = s // (30 * DAY), "month"
  else n, unit = s // (365 * DAY), "year" end
  return n .. " " .. unit .. (n == 1 and "" or "s") .. " ago"
end

local function isNew(repo)
  if not repo.tag then return false end
  local s = since(repo.at)
  return repo.unseen or (s ~= nil and s < DAY)
end

local function saveSeen()
  local keep = {}
  for _, repo in ipairs(repos) do keep[repo.key] = seen[repo.key] end
  seen = keep
  store.set("seen", seen)
end

-- A short word for the list and a sentence for when the repository is shown on its own.
local function explain(res)
  if res.status == 0 then return "No connection", "No connection" end
  if res.status == 404 then return "Not found", "No such repository. A private one needs a token" end
  if res.status == 401 then return "Token refused", "GitHub refused the token" end
  if res.status == 403 or res.status == 429 then
    return "Too many requests", "Too many requests. A token allows more"
  end
  return "Error " .. res.status, "GitHub answered " .. res.status
end

-- A release (or tag) came back for this repository.
local function found(repo, tag, title, at, pre)
  local before = repo.tag
  repo.tag, repo.title, repo.at, repo.pre, repo.problem, repo.why = tag, title, at, pre, nil, nil
  if seen[repo.key] == nil then
    seen[repo.key] = tag          -- the first time ever: what is out now is not news
    saveSeen()
  end
  repo.unseen = seen[repo.key] ~= tag
  -- Blink only for a release that came out while the card was running.
  if before and before ~= tag and config.light then led.set(GREEN, 3) end
end

local function failed(repo, res)
  if not repo.tag then repo.problem, repo.why = explain(res) end
  if res.status ~= 0 then print(repo.key .. ": " .. (res.error or ("HTTP " .. res.status))) end
end

-- For a repository without releases: its newest tag, dated by the tag's commit.
local function fetchTag(repo)
  local wanted = { { name = true, commit = { sha = true } } }
  http.get(API .. repo.key .. "/tags?per_page=1", { headers = headers(), only = wanted }, function(res)
    if not (res.ok and type(res.json) == "table") then return failed(repo, res) end
    local first = res.json[1]
    if not (first and first.name) then
      repo.problem, repo.why = "No releases", "No releases or tags yet"
      return
    end
    repo.tags = true
    local name = tostring(first.name)
    if name == repo.tag then return end -- same tag as before: its date is known
    local sha = first.commit and first.commit.sha
    if not sha then return found(repo, name, nil, nil, false) end
    -- The git object, not /commits/<sha>: that one lists the changed files and can run past 256 KB.
    local date = { committer = { date = true } }
    http.get(API .. repo.key .. "/git/commits/" .. sha, { headers = headers(), only = date }, function(r)
      local c = r.json and r.json.committer
      found(repo, name, nil, parseTime(c and c.date), false)
    end)
  end)
end

local RELEASE = { tag_name = true, name = true, published_at = true, prerelease = true, draft = true }

local function fromRelease(repo, release)
  local title = release.name ~= release.tag_name and release.name or nil
  if title == "" then title = nil end
  found(repo, tostring(release.tag_name), title and tostring(title), parseTime(release.published_at),
    release.prerelease == true)
end

local function fetch(repo)
  if repo.tags then return fetchTag(repo) end
  if config.prereleases then
    -- /releases/latest skips pre-releases, so read the newest few and take the first published one.
    http.get(API .. repo.key .. "/releases?per_page=5", { headers = headers(), only = { RELEASE } }, function(res)
      if res.status == 404 then return fetchTag(repo) end
      if not (res.ok and type(res.json) == "table") then return failed(repo, res) end
      for _, release in ipairs(res.json) do
        if release.tag_name and not release.draft then return fromRelease(repo, release) end
      end
      fetchTag(repo)
    end)
  else
    http.get(API .. repo.key .. "/releases/latest", { headers = headers(), only = RELEASE }, function(res)
      if res.status == 404 then return fetchTag(repo) end -- no releases, or no such repository
      if not (res.ok and res.json and res.json.tag_name) then return failed(repo, res) end
      fromRelease(repo, res.json)
    end)
  end
end

function load()
  if #repos == 0 then
    for word in config.repos:gmatch("[^%s,]+") do
      local key = word:gsub("^https?://", ""):gsub("^github%.com/", ""):gsub("%.git$", ""):gsub("/+$", "")
      key = key:match("^[%w%-_.]+/[%w%-_.]+$")
      if key and #repos < MOST then
        repos[#repos + 1] = { key = key, name = key:match("/(.+)$") }
      end
    end
    seen = store.get("seen", {})
    if type(seen) ~= "table" then seen = {} end
  end
  for _, repo in ipairs(repos) do fetch(repo) end
end

-- Moving on from a repository shown on its own marks its release as seen.
local function show(index)
  local repo = repos[showing]
  if repo and repo.unseen then
    repo.unseen = false
    seen[repo.key] = repo.tag
    saveSeen()
  end
  showing, pressedAt = index, time.ms()
end

-- Back to the list after a while. Measured with time.ms(): dt is capped and
-- would not add up to the real time at one frame a second.
function update()
  if showing > 0 and #repos > 1 and time.ms() - pressedAt > BACK_AFTER then
    show(0)
    return true
  end
  return false
end

-- Cut text to a width, with "..." at the end.
local function fit(text, width, font)
  if gfx.textWidth(text, font) <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "...", font) > width do text = text:sub(1, -2) end
  return text .. "..."
end

-- "bun-v1.4.4" says no more than "v1.4.4" next to the name bun.
local function shortTag(repo)
  local tag = repo.tag
  local name = repo.name:lower()
  if tag:sub(1, #name):lower() == name then
    local rest = tag:sub(#name + 1):match("^[%-_@ /](.+)$")
    if rest and rest:match("%d") then return rest end
  end
  return tag
end

local function pill(x, y, text, color)
  local w = gfx.textWidth(text, "small") + 8
  gfx.rect(x, y, w, 16, color, 4)
  gfx.text(x + w / 2, y, text, "black", "small", "center")
  return w
end

local function drawList()
  local count = 0
  for _, repo in ipairs(repos) do
    if isNew(repo) then count = count + 1 end
  end
  gfx.text(PAD, 4, "Releases", "gray", "small")
  if count > 0 then gfx.text(W - PAD, 4, count .. " new", GREEN, "small", "right") end

  local rowH = #repos > 4 and 21 or 24
  for i, repo in ipairs(repos) do
    local y = 26 + (i - 1) * rowH
    local right = W - PAD
    if repo.tag then
      local age = short(repo.at)
      gfx.text(right, y, age, "gray", "small", "right")
      right = right - math.max(gfx.textWidth(age, "small"), gfx.textWidth("00h", "small")) - 6
      local fresh = isNew(repo)
      local pillW = fresh and gfx.textWidth("NEW", "small") + 14 or 0
      local nameW = math.min(gfx.textWidth(repo.name, "small"), 80)
      local tag = fit(shortTag(repo), right - PAD - nameW - 8 - pillW, "small")
      gfx.text(right, y, tag, fresh and GREEN or "white", "small", "right")
      right = right - gfx.textWidth(tag, "small") - 6
      if fresh then
        right = right - gfx.textWidth("NEW", "small") - 8
        pill(right, y + 1, "NEW", GREEN)
        right = right - 6
      end
    else
      local text = repo.problem or "..."
      gfx.text(right, y, text, repo.problem and "orange" or "gray", "small", "right")
      right = right - gfx.textWidth(text, "small") - 6
    end
    gfx.text(PAD, y, fit(repo.name, right - PAD, "small"), "white", "small")
  end
end

local function drawOne(repo)
  local header = repo.key
  if #repos > 1 then header = showing .. "/" .. #repos .. "  " .. header end
  gfx.text(PAD, 4, fit(header, W - 2 * PAD, "small"), "gray", "small")

  if not repo.tag then
    for i, line in ipairs(gfx.wrap(repo.why or "Loading...", W - 2 * PAD, "medium")) do
      gfx.text(PAD, 30 + (i - 1) * gfx.textHeight("medium"), line, repo.problem and "orange" or "gray", "medium")
    end
    return
  end

  local fresh = isNew(repo)
  local tag = repo.tag
  if gfx.textWidth(tag, "large") > W - 2 * PAD then tag = shortTag(repo) end -- "posthog-js@1.298.0"
  local font = gfx.textWidth(tag, "large") <= W - 2 * PAD and "large" or "medium"
  tag = fit(tag, W - 2 * PAD, font)
  local y = font == "large" and 24 or 28
  gfx.text(PAD, y, tag, fresh and GREEN or "white", font)
  y = y + gfx.textHeight(font) + 4

  if repo.title then
    local lines = gfx.wrap(repo.title, W - 2 * PAD, "small")
    local most = font == "large" and 2 or 3
    if #lines > most then lines[most] = fit(lines[most] .. " " .. lines[most + 1], W - 2 * PAD, "small") end
    for i = 1, math.min(#lines, most) do
      gfx.text(PAD, y, lines[i], "white", "small")
      y = y + gfx.textHeight("small")
    end
  end

  local x = PAD
  local by = H - 22
  if fresh then x = x + pill(x, by + 1, "NEW", GREEN) + 6 end
  if repo.pre then x = x + pill(x, by + 1, "pre", "#f39c12") + 6 end
  local when = long(repo.at) or ""
  if repo.tags then when = "tag" .. (when ~= "" and ", " .. when or "") end
  gfx.text(x, by, fit(when, W - PAD - x, "small"), "gray", "small")
end

function draw()
  if #repos == 0 then
    ui.title("Release radar")
    ui.label("Add repositories, as owner/name, on the web page")
    return
  end
  local repo = repos[showing] or (#repos == 1 and repos[1])
  if repo then return drawOne(repo) end
  drawList()
end

-- Centre goes through the repositories one at a time, then back to the list.
-- With a single repository it marks the release as seen and looks again.
function button(name)
  if name ~= "center" or #repos == 0 then return false end
  if #repos == 1 then
    showing = 1
    show(1)
    load()
  else
    show(showing < #repos and showing + 1 or 0)
  end
  return true
end
