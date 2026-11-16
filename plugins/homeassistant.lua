-- Home Assistant on your desk: a card per entity. It shows the entity's state
-- (a temperature, a power reading, whether a light is on), and the centre
-- button can toggle it or switch it on or off. That also runs a script or
-- activates a scene.
--
-- You need the address of Home Assistant as the device can reach it (use its
-- IP address: names ending in .local do not work from the device) and a
-- long-lived access token, made at the bottom of your profile page in Home
-- Assistant.

plugin = {
  name = "Home Assistant",
  description = "A card per entity: its state, and a button to toggle it or run a script or scene",
  author = "Tim Broddin",
  version = "1.1",
  refresh = 30,
}

-- Shared by all of this connection's cards.
config = {
  url = { label = "Address, for example http://192.168.1.10:8123", default = "", order = 1 },
  token = { label = "Long-lived access token", default = "", secret = true, order = 2 },
}

local SERVICES = { toggle = "toggle", ["turn on"] = "turn_on", ["turn off"] = "turn_off" }

local function address(path)
  return config.url:gsub("/+$", "") .. path
end

local function headers()
  return { Authorization = "Bearer " .. config.token, ["Content-Type"] = "application/json" }
end

-- "21.456" becomes "21.5", "on" becomes "On".
local function tidy(state)
  local number = tonumber(state)
  if number then
    if number == math.floor(number) then return string.format("%d", number) end
    return string.format("%.1f", number)
  end
  state = tostring(state):gsub("_", " ")
  return state:sub(1, 1):upper() .. state:sub(2)
end

local function refresh(card)
  if config.url == "" or config.token == "" then
    card.problem = "Set the address and the token under Connections"
    return
  end
  if card.config.entity == "" then
    card.problem = "Say which entity on the web page, like sensor.office_temperature"
    return
  end
  local wanted = { state = true, attributes = { friendly_name = true, unit_of_measurement = true } }
  http.get(address("/api/states/" .. card.config.entity), { headers = headers(), only = wanted }, function(res)
    if not res.ok or not res.json then
      card.problem = res.status == 401 and "Home Assistant refused the token"
        or res.status == 404 and "There is no entity " .. card.config.entity
        or res.error or ("Home Assistant answered " .. res.status)
      return
    end
    local about = res.json.attributes or {}
    card.problem = nil
    card.name = about.friendly_name or card.config.entity
    card.title = card.name
    card.value = tidy(res.json.state)
    if about.unit_of_measurement then card.value = card.value .. " " .. about.unit_of_measurement end
  end)
end

cards = {
  entity = {
    name = "Entity",
    config = {
      entity = { label = "Entity, for example sensor.office_temperature", default = "", order = 1 },
      press = { label = "The centre button", default = "does nothing", options = { "does nothing", "toggle", "turn on", "turn off" }, order = 2 },
    },

    load = refresh,

    draw = function(card)
      ui.title(card.name or "Home Assistant")
      if card.problem then
        ui.label(card.problem, "orange")
        return
      end
      ui.big(card.value or "...")
      if card.note then
        ui.label(card.note, card.failed and "red" or "green")
      elseif SERVICES[card.config.press] then
        ui.label("Centre to " .. card.config.press)
      end
    end,

    button = function(card, name)
      local service = SERVICES[card.config.press]
      if name ~= "center" or not service or card.problem then return false end
      card.note, card.failed = "Asking...", false
      http.post(address("/api/services/homeassistant/" .. service), { entity_id = card.config.entity },
        { headers = headers() }, function(res)
          card.failed = not res.ok
          card.note = res.ok and "Done" or (res.error or ("Home Assistant answered " .. res.status))
          refresh(card) -- show what it changed
        end)
      return true
    end,
  },
}
