-- A deploy button that cannot be pressed by accident: press centre, then
-- press it again within four seconds. It calls your web address (a deploy
-- hook, a CI trigger, a Homey or Zapier webhook) and shows how that went.

plugin = {
  name = "Deploy button",
  description = "Press, confirm, and your deploy hook is called. Shows whether it worked and when it last ran",
  author = "DeskHog",
  version = "1.0",
  fps = 4,
}

config = {
  url = { label = "Web address to call", default = "", order = 1 },
  label = { label = "What the button says", default = "Deploy", order = 2 },
  secret = { label = "Authorization header to send (optional)", default = "", secret = true, order = 3 },
}

local CONFIRM_MS, RESULT_MS = 4000, 8000
local state = "idle" -- "idle", "armed", "sending", "done" or "failed"
local untilMs = 0
local detail
local last -- time.now() of the last deploy that worked
local shown

function load()
  last = store.get("last", nil)
end

local function ago()
  if not last or time.now() == 0 then return nil end
  local seconds = time.now() - last
  if seconds < 90 then return "Last one just now" end
  if seconds < 5400 then return "Last one " .. seconds // 60 .. " minutes ago" end
  if seconds < 172800 then return "Last one " .. seconds // 3600 .. " hours ago" end
  return "Last one " .. seconds // 86400 .. " days ago"
end

function update()
  if (state == "armed" or state == "done" or state == "failed") and time.ms() > untilMs then
    state = "idle"
    led.off()
  end
  local now = state .. (state == "armed" and (untilMs - time.ms()) // 1000 or "")
  if now == shown then return false end
  shown = now
end

function draw()
  ui.title(config.label)
  if config.url == "" then
    ui.label("Set the web address to call on the web page", "orange")
  elseif state == "armed" then
    ui.big("Sure?", "orange")
    ui.label("Centre again to go. " .. (untilMs - time.ms()) // 1000 + 1 .. "...")
  elseif state == "sending" then
    ui.big("Sending", "yellow")
  elseif state == "done" then
    ui.big("Done", "green")
    ui.label(detail)
  elseif state == "failed" then
    ui.big("Failed", "red")
    ui.label(detail, "red")
  else
    ui.big(config.label)
    ui.label(ago() or "Press centre twice")
  end
end

function button(name)
  if name ~= "center" then return false end
  if config.url == "" or state == "sending" then return true end
  if state ~= "armed" then
    state, untilMs = "armed", time.ms() + CONFIRM_MS
    led.set("orange")
    return true
  end

  state = "sending"
  led.set("yellow")
  local options = { timeout = 20 }
  if config.secret ~= "" then options.headers = { Authorization = config.secret } end
  http.post(config.url, { source = "deskhog", label = config.label, time = time.now() }, options, function(res)
    untilMs = time.ms() + RESULT_MS
    if res.ok then
      state, detail = "done", "The other side answered " .. res.status
      last = time.now()
      if last > 0 then store.set("last", last) end
      led.set("green", RESULT_MS / 1000)
    else
      state, detail = "failed", res.error or ("The other side answered " .. res.status)
      led.set("red", RESULT_MS / 1000)
    end
  end)
  return true
end
