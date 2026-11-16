-- A dad joke, a new one every half hour. Centre fetches another right away.
-- From icanhazdadjoke.com.

plugin = {
  name = "Dad jokes",
  description = "A new dad joke every half hour, or whenever you press the button",
  author = "DeskHog",
  version = "1.2",
  refresh = 1800,
  fps = 20,
}

local W, H = gfx.width, gfx.height
local PAD = 10
local SPEED, PAUSE = 24, 2 -- pixels a second, and seconds to rest at each end

local lines, font, lineHeight = { "Thinking of one..." }, "medium", gfx.textHeight("medium")
local overflow, offset, direction, rest = 0, 0, 1, PAUSE
local dirty = true

local function show(text)
  -- Medium if it fits the card, otherwise small, and scroll what still does not fit.
  font = "medium"
  lines = gfx.wrap(text, W - 2 * PAD, font)
  if #lines * gfx.textHeight(font) > H - 2 * PAD then
    font = "small"
    lines = gfx.wrap(text, W - 2 * PAD, font)
  end
  lineHeight = gfx.textHeight(font)
  overflow = math.max(0, #lines * lineHeight - (H - 2 * PAD))
  offset, direction, rest, dirty = 0, 1, PAUSE, true
end

function load()
  http.get("https://icanhazdadjoke.com/", { headers = { Accept = "application/json" } }, function(res)
    if res.json and res.json.joke then
      show(res.json.joke)
    elseif lines[1] == "Thinking of one..." then
      show(res.error or "The joke shop is closed. Centre to try again")
    end
  end)
end

function update(dt)
  if overflow > 0 then
    if rest > 0 then
      rest = rest - dt
    else
      offset = offset + direction * SPEED * dt
      if offset >= overflow or offset <= 0 then
        offset = math.max(0, math.min(overflow, offset))
        direction, rest = -direction, PAUSE
      end
      dirty = true
    end
  end
  if not dirty then return false end
  dirty = false
end

function draw()
  gfx.clip(PAD, PAD, W - 2 * PAD, H - 2 * PAD)
  local y = overflow > 0 and PAD - offset or (H - #lines * lineHeight) // 2
  for _, line in ipairs(lines) do
    gfx.text(W / 2, y, line, "white", font, "center")
    y = y + lineHeight
  end
  gfx.clip()
end

function button(name)
  if name ~= "center" then return false end
  show("Thinking of one...")
  load()
  return true
end
