-- CI watch: the latest GitHub Actions runs of one repository, as a dense list
-- in the tiny font. A green dot passed, a red one failed, a yellow ring is
-- still running. When a workflow that was fine fails, the card comes to the
-- front and the LED flashes red.
--
-- This plugin shows how to poll politely. GitHub sends an ETag with the list,
-- a fingerprint of the answer. Send it back as If-None-Match and GitHub answers
-- 304 with no body while nothing changed: almost nothing to download, and with
-- a token such an answer does not count towards the rate limit. How many
-- requests are left this hour comes from a header too, and is shown top right.
--
-- Without a token GitHub allows 60 requests an hour per address, which this
-- card's two-minute poll stays under. A token (a fine-grained one with read
-- access to Actions) raises that to 5000 and makes private repositories work.

plugin = {
  name = "CI watch",
  description = "The latest GitHub Actions runs of a repository, with a nudge when one goes red",
  author = "DeskHog",
  version = "1.1",
  refresh = 120,
}

config = {
  repo = { label = "Repository, as owner/name", default = "", order = 1 },
  branch = { label = "Branch (empty for all of them)", default = "", order = 2 },
  token = { label = "Access token (optional)", default = "", secret = true, order = 3 },
}

local W, H = gfx.width, gfx.height
local PAD = 8
local TOP = 21                       -- where the list starts, under the header
local ROW = 12                       -- a line of the tiny font, packed a little tighter than it asks for
local ROWS = 9
local NAME_W, AGE_W = 74, 26         -- the columns: workflow name, and how long ago, right-aligned
local TITLE_X = PAD + 10 + NAME_W + 6
local TITLE_W = W - PAD - AGE_W - TITLE_X - 4

local GREEN, RED, YELLOW, GRAY = "#2ecc71", "#e74c3c", "#f1c40f", "#8a8f98"

local repo                           -- "owner/name", once load() has read the setting
local runs                           -- what the card lists: { name, title, state, at }
local etag                           -- the fingerprint of the answer `runs` was made from
local left, limit, resetAt           -- the rate limit, from the headers of the last answer
local problem

-- Which workflows were failing when last looked, kept in the store so that a
-- restart neither forgets a red build nor announces it a second time. Nothing
-- stored means this is the first look: then there is nothing to compare with.
local failing

-- "2026-11-17T10:21:33Z" as Unix time. time.make() does the calendar; the Z says UTC.
local function seconds(text)
  local y, mo, d, h, mi, s = tostring(text or ""):match("^(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)")
  if not y then return nil end
  return time.make({ year = tonumber(y), month = tonumber(mo), day = tonumber(d),
    hour = tonumber(h), min = tonumber(mi), sec = tonumber(s) }, "UTC0")
end

local function ago(at)
  local now = time.now()
  if not at or now == 0 then return "" end
  local age = math.max(0, now - at)
  if age < 60 then return "now" end
  if age < 3600 then return age // 60 .. "m" end
  if age < 86400 then return age // 3600 .. "h" end
  if age < 86400 * 14 then return age // 86400 .. "d" end
  return age // (86400 * 7) .. "w"
end

-- The text, shortened until it fits the width. Works outside draw(), so the
-- list is fitted once per answer instead of once per repaint.
local function fit(text, width)
  if gfx.textWidth(text, "tiny") <= width then return text end
  local ends = {}                    -- where each character ends: names may hold é and the like
  for position in utf8.codes(text) do ends[#ends + 1] = position - 1 end
  for count = #ends - 1, 1, -1 do
    local shorter = text:sub(1, ends[count + 1]):gsub("%s+$", "") .. ".."
    if gfx.textWidth(shorter, "tiny") <= width then return shorter end
  end
  return ""
end

-- GitHub's status and conclusion as the four things the card tells apart.
local function stateOf(run)
  if run.status ~= "completed" then return "running" end
  if run.conclusion == "success" then return "passed" end
  if run.conclusion == "failure" or run.conclusion == "timed_out" or run.conclusion == "startup_failure" then
    return "failed"
  end
  return "skipped"                   -- cancelled, skipped, neutral
end

-- Compare each workflow's newest run with how it was, and speak up about the
-- ones that turned red.
local function announce(list)
  local now, turned = {}, {}
  local newest = {}
  for _, run in ipairs(list) do      -- the list is newest first
    if not newest[run.name] then
      newest[run.name] = true
      if run.state == "failed" then
        now[run.name] = true
        if failing and not failing[run.name] then turned[#turned + 1] = run.name end
      elseif run.state == "running" and failing and failing[run.name] then
        now[run.name] = true         -- a retry of a red build is not news until it has a result
      end
    end
  end
  failing = now
  store.set("failing", now)
  if #turned == 1 then
    notify(turned[1] .. " failed", { led = "red" })
  elseif #turned > 1 then
    notify(turned[1] .. " and " .. #turned - 1 .. " more failed", { led = "red" })
  end
end

local function explain(res)
  if res.status == 404 then return "No such repository. A private one needs a token" end
  if res.status == 401 then return "GitHub refused the token" end
  if res.status == 403 or res.status == 429 then
    if resetAt and time.now() > 0 and resetAt > time.now() then
      return "Out of requests for " .. (resetAt - time.now()) // 60 + 1 .. " minutes. A token allows more"
    end
    return "Too many requests. A token allows more"
  end
  return res.error or ("GitHub answered " .. res.status)
end

function load()
  -- Settings have their values only after the file has run, so read them here.
  repo = config.repo:match("^%s*([%w%-_.]+/[%w%-_.]+)%s*$")
  failing = failing or store.get("failing")
  if not repo then
    problem = "Give the repository as owner/name on the web page"
    return
  end

  local headers = { Accept = "application/vnd.github+json" }
  if config.token ~= "" then headers.Authorization = "Bearer " .. config.token end
  if etag then headers["If-None-Match"] = etag end

  local url = "https://api.github.com/repos/" .. repo .. "/actions/runs?per_page=" .. ROWS
  if config.branch ~= "" then url = url .. "&branch=" .. config.branch end

  http.request({
    url = url,
    headers = headers,
    -- A run is 10 KB of JSON, of which the card shows five words. `only` keeps those.
    only = { workflow_runs = { {
      name = true, display_title = true, status = true, conclusion = true, created_at = true,
    } } },
  }, function(res)
    -- Header names arrive in lower case, their values as text.
    left = tonumber(res.headers["x-ratelimit-remaining"]) or left
    limit = tonumber(res.headers["x-ratelimit-limit"]) or limit
    resetAt = tonumber(res.headers["x-ratelimit-reset"]) or resetAt

    if res.status == 304 then        -- nothing changed: what is on the card still holds
      problem = nil
      return
    end
    local list = res.json and res.json.workflow_runs
    if not res.ok or not list then
      problem = explain(res)
      return
    end
    problem = nil
    etag = res.headers.etag

    runs = {}
    for i, run in ipairs(list) do
      if i > ROWS then break end
      runs[i] = {
        name = run.name or "?",
        state = stateOf(run),
        at = seconds(run.created_at),
        label = fit(run.name or "?", NAME_W),
        title = fit(run.display_title or "", TITLE_W),
      }
    end
    announce(runs)
  end)
end

local function dot(x, y, state)
  if state == "running" then
    gfx.circle(x, y, 3, YELLOW, 1.5)      -- with a width: a ring
  elseif state == "passed" then
    gfx.circle(x, y, 3, GREEN)
  elseif state == "failed" then
    gfx.circle(x, y, 3, RED)
  else
    gfx.circle(x, y, 3, "#555b66")
  end
end

function draw()
  local name = repo and repo:match("/(.+)$") or "CI watch"
  gfx.text(PAD, 2, name, "white", "small")
  if repo and config.branch ~= "" then
    gfx.text(PAD + gfx.textWidth(name, "small") + 6, 6, config.branch, GRAY, "tiny")
  end
  if left then
    -- Red when the hour's requests are nearly gone.
    local few = limit and left < limit / 10
    gfx.text(W - PAD, 6, left .. " requests left", few and RED or GRAY, "tiny", "right")
  end

  if problem and not runs then
    for i, line in ipairs(gfx.wrap(problem, W - 2 * PAD)) do
      gfx.text(PAD, TOP + 4 + (i - 1) * 17, line, "orange")
    end
    return
  end
  if not runs then
    gfx.text(PAD, TOP + 4, "Loading...", GRAY)
    return
  end
  if #runs == 0 then
    gfx.text(PAD, TOP + 4, "No workflow runs yet", GRAY)
    return
  end

  for i, run in ipairs(runs) do
    local y = TOP + (i - 1) * ROW
    dot(PAD + 3, y + 6, run.state)
    gfx.text(PAD + 10, y, run.label, run.state == "failed" and RED or "white", "tiny")
    gfx.text(TITLE_X, y, run.title, GRAY, "tiny")
    gfx.text(W - PAD, y, ago(run.at), GRAY, "tiny", "right")
  end

  -- An answer that failed after a good one: keep the list, and say so over its last line.
  if problem then
    gfx.rect(0, H - 15, W, 15, "#3a2a00")
    gfx.text(PAD, H - 13, fit(problem, W - 2 * PAD), "orange", "tiny")
  end
end

-- Centre looks again right now.
function button(name)
  if name ~= "center" then return false end
  load()
  return true
end
