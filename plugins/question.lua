-- Icebreaker questions for the awkward minutes before a meeting starts.
-- Press the centre button for the next one.
--
-- Shows wrapping text to fit, and scrolling what does not fit inside a
-- clipped area.

plugin = {
  name = "Questions",
  description = "Icebreaker questions for the minutes before a meeting starts",
  author = "DeskHog",
  version = "1.2",
  fps = 20,
}

config = {
  questions = { label = "Your own questions, separated by |", default = "" },
}

local QUESTIONS = {
  "If you had to describe your last weekend using only 3 emojis, what would they be?",
  "If your name were the acronym for your next job title, what would it stand for?",
  "What's the last photo you took on your phone that you'd be willing to share and explain?",
  "What song title best describes your current mood or week?",
  "What's a random topic you could give a 10-minute presentation on with no prep?",
  "What's a trend you secretly (or not so secretly) loved?",
  "Pineapple on pizza: yes or no?",
}
local COLORS = { "#4a4a4a", "#2c3e50", "#27ae60", "#8e44ad", "#e67e22", "#2980b9", "#c0392b" }

local W, H = gfx.width, gfx.height
local PAD = 12                   -- from the card's edge to the text
local SPEED, PAUSE = 30, 1.5     -- pixels a second, and seconds to rest at each end

local questions = {}
local current = 1
local lines, lineHeight = {}, gfx.textHeight("loud")
local overflow = 0               -- how many pixels of text do not fit
local offset, direction, rest = 0, 1, PAUSE

local function show(index)
  current = index
  lines = gfx.wrap(questions[current], W - 2 * PAD, "loud")
  overflow = math.max(0, #lines * lineHeight - (H - 2 * PAD))
  offset, direction, rest = 0, 1, PAUSE
end

function load()
  questions = {}
  for question in config.questions:gmatch("[^|]+") do
    question = question:match("^%s*(.-)%s*$")
    if question ~= "" then questions[#questions + 1] = question end
  end
  if #questions == 0 then questions = QUESTIONS end
  show(1)
end

-- Long questions scroll down, rest, scroll back up, rest.
function update(dt)
  if overflow == 0 then return false end -- nothing moves: no need to repaint
  if rest > 0 then
    rest = rest - dt
    return false
  end
  offset = offset + direction * SPEED * dt
  if offset >= overflow or offset <= 0 then
    offset = math.max(0, math.min(overflow, offset))
    direction, rest = -direction, PAUSE
  end
end

function draw()
  gfx.rect(5, 5, W - 10, H - 10, COLORS[(current - 1) % #COLORS + 1], 8)
  gfx.clip(PAD, PAD, W - 2 * PAD, H - 2 * PAD) -- text scrolls inside the panel, not over its edge
  local y = PAD - offset
  if overflow == 0 then y = (H - #lines * lineHeight) // 2 end -- short questions sit in the middle
  for _, line in ipairs(lines) do
    gfx.text(PAD + 1, y + 1, line, "black", "loud")
    gfx.text(PAD, y, line, "white", "loud")
    y = y + lineHeight
  end
  gfx.clip()
end

function button(name)
  if name ~= "center" then return false end
  show(current % #questions + 1)
  return true
end
