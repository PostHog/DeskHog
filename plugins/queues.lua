-- How long are the queues? The longest waits in a theme park right now, or
-- the rides you name. Powered by Queue-Times.com, which covers parks all over
-- the world and is free to use with that credit.
--
-- A park's number is in its address there: queue-times.com/parks/160 is
-- Efteling, 4 Disneyland Paris, 51 Europa-Park, 276 Walibi Belgium.

plugin = {
  name = "Theme park queues",
  description = "The longest waits in a theme park right now, or the rides you care about",
  author = "DeskHog",
  version = "1.0",
  refresh = 300,
}

config = {
  park = { label = "Park number at queue-times.com", default = 160, order = 1 },
  title = { label = "What to call it", default = "Efteling", order = 2 },
  rides = { label = "Only these rides, separated by | (leave empty for the longest waits)", default = "", order = 3 },
}

local W = gfx.width
local ROWS = 4
local rows, problem

function load()
  local ride = { name = true, is_open = true, wait_time = true }
  local wanted = { lands = { { rides = { ride } } }, rides = { ride } }
  local url = string.format("https://queue-times.com/parks/%d/queue_times.json", config.park)
  http.get(url, { only = wanted }, function(res)
    if not res.ok or not res.json then
      if not rows then
        problem = res.status == 404 and "Queue-Times has no park number " .. config.park or res.error or "No answer"
      end
      return
    end
    local all = {}
    for _, item in ipairs(res.json.rides or {}) do all[#all + 1] = item end
    for _, land in ipairs(res.json.lands or {}) do
      for _, item in ipairs(land.rides or {}) do all[#all + 1] = item end
    end

    problem, rows = nil, {}
    if config.rides ~= "" then
      -- The rides asked for, in that order, open or not.
      for wantedName in config.rides:gmatch("[^|]+") do
        wantedName = wantedName:match("^%s*(.-)%s*$"):lower()
        for _, item in ipairs(all) do
          if wantedName ~= "" and tostring(item.name):lower():find(wantedName, 1, true) and #rows < ROWS then
            rows[#rows + 1] = item
            break
          end
        end
      end
      if #rows == 0 then problem = "None of those rides are in this park" end
    else
      local open = {}
      for _, item in ipairs(all) do
        if item.is_open then open[#open + 1] = item end
      end
      table.sort(open, function(a, b) return (a.wait_time or 0) > (b.wait_time or 0) end)
      for i = 1, math.min(ROWS, #open) do rows[i] = open[i] end
    end
  end)
end

function draw()
  gfx.text(8, 4, config.title, "gray", "small")
  gfx.text(W - 8, 4, "Queue-Times.com", "#666666", "small", "right")
  if problem or not rows then
    ui.label(problem or "Asking the park...", problem and "orange" or "gray")
    return
  end
  if #rows == 0 then
    ui.big("Closed", "gray")
    ui.label("No rides are open right now")
    return
  end
  for i, item in ipairs(rows) do
    local y = 27 + (i - 1) * 27
    local wait = item.wait_time or 0
    local text = item.is_open and wait .. " min" or "closed"
    local color = not item.is_open and "gray" or wait >= 60 and "red" or wait >= 30 and "orange" or "green"
    gfx.text(W - 8, y, text, color, "medium", "right")
    gfx.clip(8, y, W - 24 - gfx.textWidth(text, "medium"), 24)
    gfx.text(8, y, tostring(item.name), "white", "medium")
    gfx.clip()
  end
end
