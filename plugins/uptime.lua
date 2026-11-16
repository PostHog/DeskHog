-- Is it up? Checks a few web addresses every two minutes and shows which
-- answer. The LED turns red when one does not.

plugin = {
  name = "Uptime",
  description = "Checks up to five web addresses every two minutes. Red LED when one is down",
  author = "DeskHog",
  version = "1.0",
  refresh = 120,
}

config = {
  sites = { label = "Addresses to check, separated by spaces", default = "", order = 1 },
  light = { label = "Turn the LED red when one is down", default = true, order = 2 },
}

local W = gfx.width
local MOST = 5
local sites = {}

local function showLed()
  if not config.light then return end
  for _, site in ipairs(sites) do
    if site.state == "down" then return led.set("red") end
  end
  led.off()
end

local function check(site)
  http.get(site.url, { timeout = 8 }, function(res)
    -- Any answer short of an error counts, also a page too large for the device to keep.
    local tooLarge = (res.error or ""):find("too large", 1, true)
    if (res.status >= 200 and res.status < 400) or tooLarge then
      site.state, site.detail = "up", "up"
    else
      site.state = "down"
      site.detail = res.status > 0 and "answered " .. res.status or "no answer"
      print(site.name .. ": " .. (res.error or ("HTTP " .. res.status))) -- the full reason, for the log
    end
    showLed()
  end)
end

function load()
  if #sites == 0 then
    for url in config.sites:gmatch("[^%s,|]+") do
      if #sites < MOST then
        if not url:match("^https?://") then url = "https://" .. url end
        sites[#sites + 1] = { url = url, name = url:match("^https?://([^/]+)"), state = "?" }
      end
    end
  end
  for _, site in ipairs(sites) do check(site) end
end

function draw()
  if #sites == 0 then
    ui.title("Uptime")
    ui.label("Add the addresses to check on the web page")
    return
  end
  local down = 0
  for _, site in ipairs(sites) do
    if site.state == "down" then down = down + 1 end
  end
  gfx.text(8, 4, down == 0 and "All up" or down .. " down", down == 0 and "gray" or "red", "small")
  for i, site in ipairs(sites) do
    local y = 26 + (i - 1) * 21
    local color = site.state == "up" and "green" or site.state == "down" and "red" or "gray"
    gfx.circle(14, y + 9, 5, color)
    local detail = site.detail or "checking"
    gfx.text(W - 8, y, detail, site.state == "down" and "red" or "gray", "small", "right")
    gfx.clip(26, y, W - 44 - gfx.textWidth(detail, "small"), 20) -- a long name must not run into the detail
    gfx.text(26, y, site.name, "white", "small")
    gfx.clip()
  end
end

-- Centre checks again right now.
function button(name)
  if name ~= "center" then return false end
  load()
  return true
end
