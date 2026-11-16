-- Homey Pro on your desk: a card per device. It shows one of the device's
-- values (a temperature, power use, whether it is on), and the centre button
-- can start a flow.
--
-- You need the address of your Homey Pro on your network (its IP address) and
-- an API key, made in the Homey app under Settings, API Keys, with access to
-- devices and flows. A device's and a flow's id are the last part of their
-- address when you open them at my.homey.app.
--
-- Written from Homey's Web API documentation and not yet tried against a real
-- Homey Pro. If it does not work for you, the plugin's log says what Homey
-- answered.

plugin = {
  name = "Homey",
  description = "A card per Homey Pro device: one of its values, and a button that starts a flow",
  author = "DeskHog",
  version = "0.9.2",
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
    card.problem = "Say which device on the web page (its id)"
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
  end)
end

cards = {
  device = {
    name = "Device",
    config = {
      device = { label = "Device id", default = "", order = 1 },
      value = { label = "Which value, for example measure_temperature, measure_power or onoff", default = "measure_temperature", order = 2 },
      flow = { label = "Flow the centre button starts (its id; optional)", default = "", order = 3 },
      advanced = { label = "That flow is an advanced flow", default = false, order = 4 },
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
      elseif card.what then
        ui.label(card.what)
      end
    end,

    button = function(card, name)
      if name ~= "center" or card.config.flow == "" or card.problem then return false end
      local kind = card.config.advanced and "advancedflow" or "flow"
      card.note, card.failed = "Starting...", false
      http.post(address("/api/manager/flow/" .. kind .. "/" .. card.config.flow .. "/trigger"), {}, { headers = headers() }, function(res)
        card.failed = not res.ok
        card.note = res.ok and "Flow started" or (res.error or ("Homey answered " .. res.status))
        if not res.ok then print("flow " .. card.config.flow .. ": " .. card.note) end
        refresh(card)
      end)
      return true
    end,
  },
}
