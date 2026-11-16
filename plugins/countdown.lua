-- Days, hours and minutes until something: a launch, a holiday, a birthday.
-- Add a card per date; they share the time zone.

plugin = {
  name = "Countdown",
  description = "How long until the launch, the holiday, the birthday. A card per date",
  author = "DeskHog",
  version = "1.0",
  fps = 1,
}

-- Shared by all of this plugin's cards.
config = {
  offset = { label = "Hours ahead of UTC (for example 1, -5 or 5.5)", default = "0" },
}

local MONTHS = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
  "November", "December" }

-- A date as the number of days since 1 January 1970.
local function daysFromDate(year, month, day)
  if month <= 2 then year = year - 1 end
  local era = year // 400
  local yearOfEra = year - era * 400
  local dayOfYear = (153 * ((month + 9) % 12) + 2) // 5 + day - 1
  return era * 146097 + yearOfEra * 365 + yearOfEra // 4 - yearOfEra // 100 + dayOfYear - 719468
end

-- What the card should say right now: the big text, the line under it and a colour.
local function describe(card)
  local year, month, day = card.config.date:match("^%s*(%d%d%d%d)-(%d%d?)-(%d%d?)%s*$")
  year, month, day = tonumber(year), tonumber(month), tonumber(day)
  if not year or month < 1 or month > 12 or day < 1 or day > 31 then
    return "?", "Write the date as 2027-01-31", "orange"
  end
  local hour, minute = card.config.time:match("^%s*(%d%d?):(%d%d)%s*$")
  hour, minute = tonumber(hour), tonumber(minute)
  local on = string.format("%d %s %d", day, MONTHS[month], year)
  if hour then on = on .. string.format(", %d:%02d", hour, minute) end

  local utc = time.now()
  if utc == 0 then return "...", "Waiting for the time", "gray" end
  local now = utc + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)
  -- Days and seconds apart, so dates far ahead do not overflow.
  local days = daysFromDate(year, month, day) - now // 86400
  local seconds = (hour or 0) * 3600 + (minute or 0) * 60 - now % 86400
  if not hour and days == 0 then return "Today!", on, "green" end
  if seconds < 0 then days, seconds = days - 1, seconds + 86400 end

  if days < 0 then
    local ago = -days - (seconds > 0 and 1 or 0)
    if ago == 0 then return "Today!", on, "green" end
    return ago == 1 and "1 day ago" or ago .. " days ago", on, "gray"
  end
  if days >= 2 then return days .. " days", on, "white" end
  local hours = days * 24 + seconds // 3600
  if hours >= 1 then return string.format("%dh %02dm", hours, seconds % 3600 // 60), on, "yellow" end
  return string.format("%d:%02d", seconds // 60, seconds % 60), on, "orange"
end

cards = {
  date = {
    name = "Date",
    config = {
      what = { label = "What happens", default = "New Year", order = 1 },
      date = { label = "Date, as year-month-day", default = "2027-01-01", order = 2 },
      time = { label = "Time, as 9:30 or 17:00 (leave empty for the whole day)", default = "", order = 3 },
    },
    load = function(card)
      card.title = card.config.what
    end,
    update = function(card)
      local big = describe(card)
      if big == card.shown then return false end -- only repaint when the text changes
      card.shown = big
    end,
    draw = function(card)
      local big, on, color = describe(card)
      ui.title(card.config.what)
      ui.big(big, color)
      ui.label(on)
    end,
  },
}
