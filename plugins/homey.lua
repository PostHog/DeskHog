-- Homey Pro on your desk: a card per device. It shows one of the device's
-- values (a temperature, power use, whether it is on). The centre button
-- starts a flow, or switches the device on and off when the card shows
-- "onoff" and has no flow.
--
-- You need the address of your Homey Pro on your network (its IP address) and
-- an API key, made in the Homey app under Settings, API Keys, with access to
-- devices and flows. Once the card exists, the web page lists your devices
-- and flows to pick from. Until then, or when the list does not come, type an
-- id: the last part of a device's or a flow's address at my.homey.app.
--
-- Shows http.put, and settings with `choices`: lists the plugin fetches.
--
-- Written from Homey's Web API documentation and not yet tried against a real
-- Homey Pro. If it does not work for you, the plugin's log says what Homey
-- answered.

plugin = {
  name = "Homey",
  description = "A card per Homey Pro device: one of its values, and a button that switches it or starts a flow",
  author = "DeskHog",
  version = "0.11",
  refresh = 30,
}

-- Shared by all of this connection's cards.
config = {
  url = { label = "Address of your Homey Pro, for example http://192.168.1.20", default = "", order = 1 },
  key = { label = "API key", default = "", secret = true, order = 2 },
}

local function address(path)
  return config.url:gsub("/+$", "") .. path
end

local function headers()
  return { Authorization = "Bearer " .. config.key, ["Content-Type"] = "application/json" }
end

-- Why a request failed, in a few words.
local function trouble(res)
  return res.status == 401 and "Homey refused the API key" or res.error or ("Homey answered " .. res.status)
end

-- The lists behind the Device and Flow settings. The web page calls these
-- when someone opens a card's settings, with done() to answer through.
-- Homey sends its devices and flows as one object, with their ids as the keys,
-- and says a great deal about each. `only` with "*" keeps the name (and the
-- room) of every one and stores nothing else, so a home with a hundred
-- devices still fits.
local function listOf(path, done)
  if config.url == "" or config.key == "" then return done(nil, "Set the address and the API key first.") end
  local wanted = { ["*"] = { name = true, zoneName = true } }
  http.get(address(path), { headers = headers(), only = wanted }, function(res)
    if not res.ok or type(res.json) ~= "table" then
      print("list " .. path .. ": " .. trouble(res))
      return done(nil, trouble(res) .. ".")
    end
    local list = {}
    for id, item in pairs(res.json) do
      if type(item) == "table" and item.name then
        local room = item.zoneName -- devices say which room they are in
        list[#list + 1] = { value = id, label = room and (item.name .. " (" .. room .. ")") or item.name }
      end
    end
    table.sort(list, function(a, b) return a.label:lower() < b.label:lower() end)
    done(list)
  end)
end

function listDevices(done)
  listOf("/api/manager/devices/device/", done)
end

-- Flows, or advanced flows when the box under the list is ticked. `ctx.settings`
-- is the form as it is filled in right now, saved or not: tick the box, press
-- Refresh, and the list is the other kind.
function listFlows(done, ctx)
  local kind = ctx.settings.advanced and "advancedflow" or "flow"
  listOf("/api/manager/flow/" .. kind .. "/", done)
end

local function tidy(value, units)
  if type(value) == "boolean" then return value and "On" or "Off" end
  if type(value) == "number" then
    local text = value == math.floor(value) and string.format("%d", value) or string.format("%.1f", value)
    return units and text .. " " .. units or text
  end
  return tostring(value)
end

local function refresh(card)
  if config.url == "" or config.key == "" then
    card.problem = "Set the address and the API key under Connections"
    return
  end
  if card.config.device == "" then
    card.problem = "Pick a device on the web page"
    return
  end
  local wanted = { name = true, capabilitiesObj = true }
  http.get(address("/api/manager/devices/device/" .. card.config.device), { headers = headers(), only = wanted }, function(res)
    if not res.ok or not res.json then
      card.problem = res.status == 401 and "Homey refused the API key"
        or res.status == 404 and "Homey has no device with that id"
        or res.error or ("Homey answered " .. res.status)
      print("device " .. card.config.device .. ": " .. card.problem)
      return
    end
    card.name = res.json.name or "Homey"
    card.title = card.name
    local value = (res.json.capabilitiesObj or {})[card.config.value]
    if not value then
      local has = {}
      for key in pairs(res.json.capabilitiesObj or {}) do has[#has + 1] = key end
      table.sort(has)
      print(card.name .. " has: " .. table.concat(has, ", "))
      card.problem = "It has no " .. card.config.value .. ". The plugin's log lists what it has"
      return
    end
    card.problem = nil
    card.value = tidy(value.value, value.units)
    card.what = value.title
    -- Something to switch: a true or false that Homey lets you set.
    card.on = nil
    if card.config.value == "onoff" and type(value.value) == "boolean" and value.setable ~= false then
      card.on = value.value
    end
  end)
end

-- Start the card's flow. A flow whose id was typed may be of the other kind
-- than the "advanced" setting says: Homey answers 404 then, and the other
-- kind gets a try.
local function startFlow(card, kind, mayRetry)
  card.note, card.failed = "Starting...", false
  http.post(address("/api/manager/flow/" .. kind .. "/" .. card.config.flow .. "/trigger"), {}, { headers = headers() }, function(res)
    if res.status == 404 and mayRetry then
      return startFlow(card, kind == "flow" and "advancedflow" or "flow", false)
    end
    card.failed = not res.ok
    card.note = res.ok and "Flow started" or (res.error or ("Homey answered " .. res.status))
    if not res.ok then print("flow " .. card.config.flow .. ": " .. card.note) end
    refresh(card)
  end)
end

cards = {
  device = {
    name = "Device",
    config = {
      device = { label = "Device", default = "", choices = "listDevices", order = 1 },
      value = { label = "Which value, for example measure_temperature, measure_power or onoff", default = "measure_temperature", order = 2 },
      flow = { label = "Flow the centre button starts (optional)", default = "", choices = "listFlows", order = 3 },
      advanced = { label = "That flow is an advanced flow (tick it and press Refresh to list those)", default = false, order = 4 },
    },

    load = refresh,

    draw = function(card)
      ui.title(card.name or "Homey")
      if card.problem then
        ui.label(card.problem, "orange")
        return
      end
      ui.big(card.value or "...")
      if card.note then
        ui.label(card.note, card.failed and "red" or "green")
      elseif card.config.flow ~= "" then
        ui.label((card.what or "") .. (card.what and ". " or "") .. "Centre starts the flow")
      elseif card.on ~= nil then
        ui.label("Centre switches it " .. (card.on and "off" or "on"))
      elseif card.what then
        ui.label(card.what)
      end
    end,

    button = function(card, name)
      if name ~= "center" or card.problem then return false end
      if card.config.flow ~= "" then
        startFlow(card, card.config.advanced and "advancedflow" or "flow", true)
        return true
      end
      if card.on == nil then return false end

      -- Switch it: PUT the new value to the device's onoff capability.
      local on = not card.on
      card.note, card.failed = on and "Switching on..." or "Switching off...", false
      local path = "/api/manager/devices/device/" .. card.config.device .. "/capability/onoff"
      http.put(address(path), { value = on }, { headers = headers() }, function(res)
        card.failed = not res.ok
        if res.ok then
          card.note = nil
          card.on, card.value = on, tidy(on) -- show it at once; the next refresh confirms it
        else
          card.note = trouble(res)
          print("switching " .. card.config.device .. ": " .. card.note)
          refresh(card)
        end
      end)
      return true
    end,
  },
}
