-- A clock: the time in large digits, with the date under it.
--
-- The device knows the time in UTC only. Set how many hours your time is ahead
-- of UTC (or behind, with a minus), and change it when the clocks change.

plugin = {
  name = "Clock",
  description = "The time in large digits, with the date under it",
  author = "Tim Broddin",
  version = "1.1",
  fps = 1, -- look at the time once a second; it is only repainted when the minute changes
}

config = {
  offset = { label = "Hours ahead of UTC (for example 1, -5 or 5.5)", default = "0", order = 1 },
  hours12 = { label = "12-hour clock", default = false, order = 2 },
  label = { label = "A line above the time (for example a city)", default = "", order = 3 },
}

local DAYS = { "Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday" } -- 1 January 1970 was a Thursday
local MONTHS = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
  "November", "December" }

-- Days since 1970 to year, month and day.
local function date(days)
  local year = 1970
  while true do
    local leap = year % 4 == 0 and (year % 100 ~= 0 or year % 400 == 0)
    local length = leap and 366 or 365
    if days < length then
      local lengths = { 31, leap and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
      for month, monthLength in ipairs(lengths) do
        if days < monthLength then return year, month, days + 1 end
        days = days - monthLength
      end
    end
    days = days - length
    year = year + 1
  end
end

local shown -- what was last drawn, to repaint only when it changes

local function now()
  local utc = time.now()
  if utc == 0 then return nil end -- the device has not set its clock yet
  local offset = tonumber(config.offset) or 0
  return utc + math.floor(offset * 3600 + 0.5)
end

function update()
  local seconds = now()
  local minute = seconds and seconds // 60 or -1
  if minute == shown then return false end
  shown = minute
end

function draw()
  local seconds = now()
  if not seconds then
    ui.title("Clock")
    ui.label("Waiting for the time. It comes from the internet once WiFi is connected.")
    return
  end
  local days = seconds // 86400
  local hour, minute = seconds % 86400 // 3600, seconds % 3600 // 60
  local suffix = ""
  if config.hours12 then
    suffix = hour < 12 and " am" or " pm"
    hour = (hour + 11) % 12 + 1
  end
  local year, month, day = date(days)

  ui.title(config.label ~= "" and config.label or DAYS[days % 7 + 1])
  ui.big(string.format(config.hours12 and "%d:%02d%s" or "%02d:%02d%s", hour, minute, suffix))
  ui.label(string.format("%s%d %s %d", config.label ~= "" and DAYS[days % 7 + 1] .. " " or "", day, MONTHS[month], year))
end
