-- The next trains from a Belgian station, with delays and platforms. Add a
-- card per station. Data from iRail (irail.be), which is free and needs no key.

plugin = {
  name = "Trains (Belgium)",
  description = "The next departures from a Belgian station, with delays and platforms",
  author = "DeskHog",
  version = "1.3",
  refresh = 60,
  fps = 1,
}

local W = gfx.width
local ROWS = 4
local DESTINATION = 96 -- where the destinations start, right of the minutes and the delay

local function urlencode(text)
  return (text:gsub("[^%w%-_%.~]", function(c)
    return string.format("%%%02X", c:byte())
  end))
end

-- "now", "7 min" or "1h05", counting the delay in.
local function leavesIn(departure)
  local seconds = departure.at + departure.delay * 60 - time.now()
  if seconds < 45 then return "now" end
  local minutes = (seconds + 30) // 60
  if minutes < 60 then return minutes .. " min" end
  return string.format("%dh%02d", minutes // 60, minutes % 60)
end

cards = {
  station = {
    name = "Station",
    config = {
      station = { label = "Station", default = "Antwerpen-Centraal", order = 1 },
      towards = { label = "Only trains to (part of the destination; optional)", default = "", order = 2 },
    },

    load = function(card)
      local url = "https://api.irail.be/v1/liveboard?format=json&lang=en&arrdep=departure&station="
        .. urlencode(card.config.station)
      local wanted = { station = true, departures = { departure = {
        { station = true, time = true, delay = true, platform = true, canceled = true } } } }
      http.get(url, { only = wanted }, function(res)
        local list = res.json and res.json.departures and res.json.departures.departure
        if not res.ok or not list then
          -- Keep showing the last answer if there is one; trains do not stop for a hiccup.
          if not card.departures then
            card.problem = res.status == 404 and "iRail knows no station called " .. card.config.station
              or res.error or ("iRail answered " .. res.status)
          end
          return
        end
        card.problem = nil
        card.name = res.json.station or card.config.station
        card.title = card.name
        card.departures = {}
        local towards = card.config.towards:lower()
        for _, item in ipairs(list) do
          local to = tostring(item.station or "?")
          if towards == "" or to:lower():find(towards, 1, true) then
            card.departures[#card.departures + 1] = {
              to = to,
              at = tonumber(item.time) or 0,
              delay = (tonumber(item.delay) or 0) // 60,
              platform = tostring(item.platform or "?"),
              canceled = item.canceled == "1" or item.canceled == 1,
            }
          end
        end
      end)
    end,

    -- The minutes count down between answers.
    update = function(card)
      local tick = time.now() // 20
      if tick == card.tick and card.drawn == card.departures then return false end
      card.tick, card.drawn = tick, card.departures
    end,

    draw = function(card)
      gfx.text(8, 4, card.name or card.config.station, "gray", "small")
      if card.problem then
        ui.label(card.problem, "orange")
        return
      end
      if not card.departures then
        gfx.text(8, 30, "Looking up the trains...", "gray", "small")
        return
      end
      local row = 0
      for _, departure in ipairs(card.departures) do
        if row == ROWS then break end
        -- Trains that have left drop off the list.
        if departure.at + departure.delay * 60 - time.now() > -30 or time.now() == 0 then
          local y = 27 + row * 27
          row = row + 1
          if departure.canceled then
            gfx.text(8, y, "cancelled", "red", "small")
          else
            gfx.text(8, y, time.now() == 0 and "..." or leavesIn(departure), "yellow", "medium")
            if departure.delay > 0 then gfx.text(62, y + 2, "+" .. departure.delay, "red", "small") end
          end
          gfx.text(W - 8, y, departure.platform, "gray", "medium", "right")
          gfx.clip(DESTINATION, y, W - DESTINATION - 30, 24) -- a long name must not run into the platform
          gfx.text(DESTINATION, y, departure.to, departure.canceled and "gray" or "white", "medium")
          gfx.clip()
        end
      end
      if row == 0 then gfx.text(8, 30, "No trains in the coming hour", "gray", "small") end
    end,
  },
}
