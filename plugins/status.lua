-- Is the service you depend on having a bad day? A card per status page, with
-- its overall state and the incident that is open right now. Works with any
-- page hosted by Atlassian Statuspage, such as www.githubstatus.com,
-- www.cloudflarestatus.com, www.vercel-status.com or status.anthropic.com,
-- through their public API (no key needed).
--
-- The LED is shared by all cards: red while any of them reports a major or
-- critical outage, otherwise orange while any reports a minor one, otherwise
-- its normal glow.

plugin = {
  name = "Status board",
  description = "How the services you depend on are doing, from their status pages. Red LED during an outage",
  author = "DeskHog",
  version = "1.0",
  refresh = 180,
}

-- Shared by all of this connection's cards.
config = {
  light = { label = "Light the LED during an outage", default = true },
}

local W = gfx.width

local STATES = {
  none = { word = "Operational", color = "#2ecc71", level = 0 },
  minor = { word = "Minor", color = "#f39c12", level = 1 },
  major = { word = "Major", color = "#e74c3c", level = 2 },
  critical = { word = "Critical", color = "#e74c3c", level = 2 },
  maintenance = { word = "Maintenance", color = "#5dade2", level = 0 },
}
local IMPACT = { none = 0, maintenance = 0, minor = 1, major = 2, critical = 3 }

local levels = {} -- card id -> 0, 1 or 2, for the LED

local function showLed()
  if not config.light then return end
  local worst = 0
  for _, level in pairs(levels) do worst = math.max(worst, level) end
  if worst == 2 then led.set("red") elseif worst == 1 then led.set("#ff8800") else led.off() end
end

-- "https://www.githubstatus.com/history" becomes "www.githubstatus.com".
local function hostOf(text)
  return text:gsub("^%s*https?://", ""):match("^([%w%-%.:]+)")
end

-- The open incident with the biggest impact, the newest of those when tied.
local function worstOf(list)
  local worst
  for _, item in ipairs(list or {}) do
    if item.name and (not worst or (IMPACT[item.impact] or 0) > (IMPACT[worst.impact] or 0)) then worst = item end
  end
  return worst, list and #list or 0
end

local function fetchIncidents(card, base)
  local maintenance = card.state == "maintenance"
  local url = base .. (maintenance and "scheduled_maintenances/active.json" or "incidents/unresolved.json")
  local key = maintenance and "scheduled_maintenances" or "incidents"
  local wanted = { [key] = { { name = true, impact = true } } }
  http.get(url, { only = wanted }, function(res)
    if not (res.ok and res.json) then return end -- keep what was shown; some pages only offer the state
    local worst, count = worstOf(res.json[key])
    card.incident = worst and worst.name
    card.more = count - 1
  end)
end

cards = {
  page = {
    name = "Status page",
    config = {
      host = { label = "Status page address (any Statuspage-hosted page)", default = "www.githubstatus.com" },
    },

    load = function(card)
      local host = hostOf(card.config.host)
      if not host then
        card.problem = "Enter a status page address on the web page"
        return
      end
      local base = "https://" .. host .. "/api/v2/"
      local wanted = { page = { name = true }, status = { indicator = true, description = true } }
      http.get(base .. "status.json", { only = wanted }, function(res)
        local status = res.json and res.json.status
        if not (res.ok and status and STATES[status.indicator]) then
          if not card.state then
            card.problem = res.status == 0 and "No connection" or "No status page at " .. host
          end
          return
        end
        card.problem = nil
        card.state, card.description = status.indicator, status.description
        local name = res.json.page and res.json.page.name
        if name then card.title = name:gsub(" Status$", "") end
        levels[card.id] = STATES[card.state].level
        showLed()
        if card.state == "none" then
          card.incident, card.more = nil, 0
        else
          fetchIncidents(card, base)
        end
      end)
    end,

    draw = function(card)
      gfx.text(8, 4, card.title or hostOf(card.config.host) or "Status", "gray", "small")
      local lh = gfx.textHeight("small")
      if not card.state then
        local text = card.problem or "Loading..."
        for i, line in ipairs(gfx.wrap(text, W - 16, "medium")) do
          gfx.text(8, 30 + (i - 1) * gfx.textHeight("medium"), line, card.problem and "orange" or "gray", "medium")
        end
        return
      end
      local state = STATES[card.state]
      local x = math.min(8, math.floor((W - gfx.textWidth(state.word, "large")) / 2)) -- "Maintenance" only just fits
      gfx.text(x, 16, state.word, state.color, "large")

      local text, color = card.incident, "white"
      if not text then text, color = card.state == "none" and "No open incidents" or card.description or "", "gray" end
      local lines = gfx.wrap(text, W - 16, "small")
      local more = card.incident and card.more and card.more > 0
      local most = more and 3 or 4
      if #lines > most then
        lines[most] = lines[most]:gsub("%s*%S*$", "") .. "..."
      end
      local y = 58
      for i = 1, math.min(#lines, most) do
        gfx.text(8, y, lines[i], color, "small")
        y = y + lh
      end
      if more then
        gfx.text(8, y, "and " .. card.more .. (card.more == 1 and " more incident" or " more incidents"), "gray", "small")
      end
    end,

    -- Centre looks again right now.
    button = function(card, name)
      if name ~= "center" then return false end
      cards.page.load(card)
      return true
    end,
  },
}
