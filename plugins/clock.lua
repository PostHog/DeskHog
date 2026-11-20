-- A clock: the time in large digits, with the date under it.
--
-- It shows the device's own time zone, summer time included. Pick the zone
-- once, on the device's web page.

plugin = {
  name = "Clock",
  description = "The time in large digits, with the date under it",
  author = "DeskHog",
  version = "2.0",
  fps = 1, -- look at the time once a second; it is only repainted when the minute changes
}

config = {
  hours12 = { label = "12-hour clock", default = false, order = 1 },
  label = { label = "A line above the time (for example a city)", default = "", order = 2 },
}

local shown -- what was last drawn, to repaint only when it changes

function update()
  local now = time.date()
  local minute = now and now.yday * 1440 + now.hour * 60 + now.min or -1
  if minute == shown then return false end
  shown = minute
end

function draw()
  local now = time.now()
  if now == 0 then -- the device has not set its clock yet
    ui.title("Clock")
    ui.label("Waiting for the time. It comes from the internet once WiFi is connected.")
    return
  end
  local day = time.format("%A", now)
  local clock = time.format("%H:%M", now)
  if config.hours12 then clock = time.format("%-I:%M ", now) .. time.format("%p", now):lower() end

  ui.title(config.label ~= "" and config.label or day)
  ui.big(clock)
  ui.label((config.label ~= "" and day .. " " or "") .. time.format("%-d %B %Y", now))
end
