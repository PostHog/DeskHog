-- PostHog insights on your desk: big numbers, line charts and funnels.
--
-- One connection is one PostHog project. Add a card per insight; they share
-- the project and API key, which you set once. Press the centre button on a
-- card to recalculate it now.

plugin = {
  name = "PostHog",
  description = "PostHog insights on your desk: big numbers, line charts and funnels",
  author = "DeskHog",
  version = "1.2",
  refresh = 600, -- every card's load() runs again every 10 minutes
}

-- Shared by all of this connection's cards.
config = {
  region = { label = "Region", default = "us", options = { "us", "eu" }, order = 1 },
  project = { label = "Project ID (the number in your PostHog URLs)", default = "", order = 2 },
  key = { label = "Personal API key (needs read access to insights)", secret = true, order = 3 },
  host = { label = "Address, only if you host PostHog yourself", default = "", order = 4 },
}

-- PostHog answers with a lot more than a small screen can show: who made the
-- insight, every dashboard it is on, labels for every point. This keeps the few
-- parts the cards use, so even a large answer fits in memory.
local SERIES = {
  data = true, count = true, aggregated_value = true, label = true, -- trends
  name = true, custom_name = true, order = true,                    -- funnel steps
}
local ONLY = {
  results = { {
    name = true,
    derived_name = true,
    result = SERIES,
    query = {
      kind = true,
      display = true,
      source = {
        kind = true,
        trendsFilter = { display = true, aggregationAxisPrefix = true, aggregationAxisPostfix = true },
      },
    },
    filters = { insight = true, display = true },
  } },
}

local function urlencode(text)
  return (tostring(text):gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

local function address()
  if config.host ~= "" then return (config.host:gsub("/+$", "")) end
  return "https://" .. config.region .. ".posthog.com"
end

-- 1234567 -> "1.23M", 12345 -> "12,345", 3.14159 -> "3.14"
local function format(n)
  if type(n) ~= "number" then return "-" end
  local size = math.abs(n)
  if size >= 1e9 then return string.format("%.2fB", n / 1e9) end
  if size >= 1e6 then return string.format("%.2fM", n / 1e6) end
  if size >= 1e5 then return string.format("%.1fK", n / 1e3) end
  local text
  if size < 1000 and size ~= math.floor(size) then
    text = string.format("%.2f", size):gsub("0+$", ""):gsub("%.$", "")
  else
    text = string.format("%d", math.floor(size + 0.5)):reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", "")
  end
  return (n < 0 and "-" or "") .. text
end

local function number(value, trends)
  trends = trends or {}
  return { kind = "number", text = (trends.aggregationAxisPrefix or "") .. format(value) .. (trends.aggregationAxisPostfix or "") }
end

local function line(values, label, more)
  local last
  for i = #values, 1, -1 do
    if type(values[i]) == "number" then last = values[i] break end
  end
  if not last then return { kind = "empty" } end
  if more and more > 0 then label = (label or "") .. " +" .. more .. " more" end
  return { kind = "line", values = values, last = last, label = label }
end

local function funnel(steps)
  local view = { kind = "funnel", steps = {} }
  for _, step in ipairs(steps) do
    if type(step) == "table" and type(step.count) == "number" then
      view.steps[#view.steps + 1] = { name = step.custom_name or step.name or ("Step " .. (#view.steps + 1)), count = step.count }
    end
  end
  return #view.steps > 0 and view or { kind = "empty" }
end

-- Work out what an insight is from the shape of its result. PostHog has a few:
-- trends give a list of series, funnels a list of steps (or a list of those,
-- one per breakdown), and SQL insights give rows.
local function understand(insight)
  local result = insight.result
  if type(result) ~= "table" or result[1] == nil then return { kind = "empty" } end
  local query = insight.query or {}
  local source = query.source or {}
  local filters = insight.filters or {}
  local trends = source.trendsFilter or {}
  local display = trends.display or query.display or filters.display
  local isFunnel = source.kind == "FunnelsQuery" or filters.insight == "FUNNELS"
  local first = result[1]

  if type(first) == "number" then return number(first) end
  if type(first) ~= "table" then return { kind = "unsupported" } end

  if first[1] == nil then -- a series or a funnel step
    if isFunnel or (first.order ~= nil and first.data == nil) then return funnel(result) end
    if type(first.aggregated_value) == "number" then return number(first.aggregated_value, trends) end
    if type(first.data) == "table" and display ~= "BoldNumber" then return line(first.data, first.label, #result - 1) end
    if type(first.count) == "number" then return number(first.count, trends) end
    return { kind = "unsupported" }
  end

  -- A list of lists.
  if type(first[1]) == "table" then return funnel(first) end -- a funnel with a breakdown: show the first
  if #result == 1 and type(first[1]) == "number" then return number(first[1]) end -- SQL: a single value
  local values = {}
  for _, row in ipairs(result) do -- SQL: rows ending in a number
    local value = type(row) == "table" and row[#row]
    if type(value) == "number" then values[#values + 1] = value end
  end
  if #values >= 2 then return line(values) end
  if #values == 1 then return number(values[1]) end
  return { kind = "unsupported" }
end

local function explain(res)
  if res.status == 401 or res.status == 403 then return "PostHog refused the API key" end
  if res.status == 404 then return "Project not found" end
  if res.status == 0 then return "No connection" end
  return "PostHog answered with error " .. res.status
end

-- mode is how hard PostHog should try: "force_cache" answers at once with
-- what it has, "blocking" recalculates first if that is stale.
local function fetch(card, mode)
  if config.project == "" or (config.key or "") == "" then
    card.view, card.status = nil, "Add your project ID and API key\non the DeskHog web page"
    return
  end
  if card.config.id == "" then
    card.view, card.status = nil, "Set this card's insight ID\non the DeskHog web page"
    return
  end

  local url = string.format("%s/api/projects/%s/insights/?short_id=%s&refresh=%s",
    address(), urlencode(config.project), urlencode(card.config.id), mode)
  local options = {
    headers = { Authorization = "Bearer " .. config.key },
    only = ONLY,
    timeout = mode == "blocking" and 30 or 15,
  }
  http.get(url, options, function(res)
    if not res.ok then
      -- Keep showing the last numbers through a network hiccup, but not when PostHog says no.
      if res.status ~= 0 or not card.view then card.view, card.status = nil, explain(res) end
      return
    end
    local insight = res.json and res.json.results and res.json.results[1]
    if not insight then
      card.view, card.status = nil, "No insight with the ID " .. card.config.id
      return
    end
    local result = insight.result
    local missing = type(result) ~= "table" or result[1] == nil
    if missing and mode ~= "blocking" then return fetch(card, "blocking") end -- never calculated: ask for it

    card.title = insight.name or insight.derived_name or ("Insight " .. card.config.id)
    card.view, card.status = understand(insight), nil
  end)
end

local function shorten(text, width)
  if gfx.textWidth(text) <= width then return text end
  while #text > 1 and gfx.textWidth(text .. "..") > width do
    text = text:sub(1, utf8.offset(text, -1) - 1) -- drop the last character, not the last byte
  end
  return text .. ".."
end

local function drawFunnel(view)
  local W, H = gfx.width, gfx.height
  local top = 26
  local shown = math.min(#view.steps, 4)
  local rowHeight = (H - top - 2) // shown
  local first = view.steps[1].count
  for i = 1, shown do
    -- With more than four steps, the fourth row shows the last one.
    local step = (i == shown) and view.steps[#view.steps] or view.steps[i]
    local share = first > 0 and step.count / first or 0
    local y = top + (i - 1) * rowHeight
    local figures = format(step.count) .. "  " .. math.floor(share * 100 + 0.5) .. "%"
    gfx.text(W - 8, y, figures, "white", "small", "right")
    gfx.text(8, y, shorten(step.name, W - 24 - gfx.textWidth(figures)), "gray", "small")
    local barHeight = math.max(3, rowHeight - 21)
    gfx.rect(8, y + 19, W - 16, barHeight, "#2a2a2a", 2)
    gfx.rect(8, y + 19, math.max(2, (W - 16) * share), barHeight, "orange", 2)
  end
end

cards = {
  insight = {
    name = "Insight",

    -- Set per card.
    config = {
      id = { label = "Insight ID (the letters at the end of the insight's URL)", default = "" },
    },

    load = function(card)
      -- The first time, show what PostHog has right away. After that, ask for fresh numbers.
      fetch(card, card.view and "blocking" or "force_cache")
    end,

    draw = function(card)
      local view = card.view
      ui.title(card.title or "PostHog")
      if not view then
        ui.label(card.status or "Loading...")
      elseif view.kind == "number" then
        ui.big(view.text)
      elseif view.kind == "line" then
        ui.big(format(view.last))
        if view.label then ui.label(view.label) end
        ui.chart(view.values, "orange")
      elseif view.kind == "funnel" then
        drawFunnel(view)
      elseif view.kind == "empty" then
        ui.label("No data yet")
      else
        ui.label("This kind of insight\nis not supported yet")
      end
    end,

    button = function(card, name)
      if name ~= "center" then return false end
      fetch(card, "blocking")
      return true
    end,
  },
}
