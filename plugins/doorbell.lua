-- A doorbell for anything on your network: send the card a message and it
-- comes to the front with the text, the LED flashes, and the card keeps the
-- last few with the time they arrived.
--
--   tools/deskhog send doorbell "Pizza's here"
--   tools/deskhog send doorbell '{"text": "Build failed", "color": "red"}'
--
--   curl -H 'Content-Type: text/plain' -d "Pizza's here" \
--     'http://deskhog.local/api/cards/message?key=doorbell:1'
--   curl -H 'Content-Type: application/json' -d '{"text": "Build failed", "color": "red"}' \
--     'http://deskhog.local/api/cards/message?key=doorbell:1'
--
-- A message is plain text, or JSON with a "text" and, if you like, a "color":
-- a name such as "green" or "#rrggbb". Use it from a script, a CI job, a home
-- automation, a real doorbell.
--
-- Shows message(), which is called when something is pushed to the plugin, so
-- nothing has to be polled, and notify(), which brings the card to the front.

plugin = {
  name = "Doorbell",
  description = "Shows what your scripts send it, at once: the text, the time, a flash of the LED",
  author = "DeskHog",
  version = "1.1",
  fps = 1, -- only to notice midnight, when "14:02" becomes "Mon 14:02"
}

config = {
  color = {
    label = "Colour of a message that does not name one",
    default = "orange",
    options = { "orange", "red", "yellow", "green", "blue", "purple", "white" },
  },
}

local W, H = gfx.width, gfx.height
local KEEP = 4         -- the latest, and three under it in the list
local LONGEST = 120    -- bytes of text kept per message; it is also what notify() keeps

-- Newest first: { text = ..., color = ..., at = time.now() when it arrived }.
-- Saved with store, so they are still there after a restart. A value in the
-- store is at most 2 KB, which four messages of 120 bytes stay well under.
local messages = store.get("recent", {})
if type(messages) ~= "table" then messages = {} end

local shownDay -- the day the card was last drawn on

-- What arrives comes from the network: anyone can send anything. A colour
-- that gfx does not know would stop the plugin, so only pass on ones it does.
-- gfx.color() says whether it is one, without stopping anything.
local function safeColor(color)
  if type(color) == "string" then color = color:lower() end
  if gfx.color(color) then return color end
end

-- The text cut to at most `longest` bytes, dots included, without cutting a character in half.
local function shorten(text, longest)
  if #text <= longest then return text end
  local cut = longest - 2
  while cut > 1 and text:byte(cut) & 0xC0 == 0x80 do cut = cut - 1 end -- back to the start of a UTF-8 character
  return text:sub(1, cut - 1) .. "..."
end

-- One line of clean text, no longer than what is kept.
local function tidy(text)
  return shorten(text:gsub("%s+", " "):gsub("%c", ""):match("^ ?(.-) ?$"), LONGEST)
end

function message(msg)
  local text, color = msg.body, nil
  if msg.json then -- {"text": ..., "color": ...}; without a text it just rings
    text, color = msg.json.text, safeColor(msg.json.color)
    text = (type(text) == "string" or type(text) == "number") and tostring(text) or ""
  end
  text = tidy(text)
  if text == "" then text = "Ding dong" end

  table.insert(messages, 1, { text = text, color = color, at = time.now() })
  while #messages > KEEP do table.remove(messages) end
  store.set("recent", messages)

  notify(text, { led = color or config.color })
end

-- "14:02" for today, "Mon 14:02" for another day, "" if the clock was not set when it came.
local function when(at)
  if not at or at == 0 or time.now() == 0 then return "" end
  local today = time.format("%F") == time.format("%F", at)
  return time.format(today and "%H:%M" or "%a %H:%M", at)
end

function update()
  local day = time.now() ~= 0 and time.format("%F") or ""
  if day == shownDay then return false end
  shownDay = day
end

function draw()
  ui.title("Doorbell")
  local latest = messages[1]
  if not latest then
    ui.label("Nothing yet. Send it a message:")
    ui.label('deskhog send doorbell "Hello"', "gray")
    return
  end
  gfx.text(W - 8, 6, when(latest.at), "gray", "small", "right")

  -- The latest message, in the largest font it fits in, centred in the space above the list.
  local older = #messages - 1
  local row = gfx.textHeight("tiny") - 1
  local top, bottom = 26, older > 0 and H - 8 - older * row or H - 6
  local lines, font
  for _, try in ipairs({ "large", "medium", "small" }) do
    lines, font = gfx.wrap(latest.text, W - 16, try), try
    if #lines * gfx.textHeight(try) <= bottom - top then break end
  end
  local height = gfx.textHeight(font)
  local fit = math.max(1, (bottom - top) // height) -- in the small font a long message loses its end
  if #lines > fit then lines[fit] = shorten(lines[fit], #lines[fit] - 1) end
  local count = math.min(#lines, fit)
  local y = top + (bottom - top - count * height) / 2
  for i = 1, count do
    gfx.text(W / 2, y + (i - 1) * height, lines[i], latest.color or config.color, font, "center")
  end

  -- The ones before it, in the tiny font: a time and one line each.
  if older == 0 then return end
  gfx.line(8, bottom + 2, W - 8, bottom + 2, "#333333")
  local times, widest = {}, 0
  for i = 1, older do
    times[i] = when(messages[i + 1].at)
    widest = math.max(widest, gfx.textWidth(times[i], "tiny"))
  end
  local left = widest > 0 and 8 + widest + 8 or 8
  for i = 1, older do
    local item = messages[i + 1]
    local y = bottom + 5 + (i - 1) * row
    gfx.text(8, y, times[i], "gray", "tiny")
    gfx.clip(left, y, W - 8 - left, row + 1) -- a line too long for the card is cut off at its edge
    gfx.text(left, y, item.text, item.color or "white", "tiny")
    gfx.clip()
  end
end
