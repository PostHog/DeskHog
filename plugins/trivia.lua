-- A trivia quiz: ten questions, three answers each, one button per answer
-- (top, middle, bottom). Questions come from the Open Trivia Database,
-- opentdb.com.
--
-- Shows fetching a batch once and playing from it, text that arrives
-- percent-encoded, and leaving out what does not fit the card.
--
-- All three buttons belong to the quiz while a round is on. To leave in the
-- middle, hold centre and press up.

plugin = {
  name = "Trivia",
  description = "Ten questions, three answers, one button each. From the Open Trivia Database",
  author = "DeskHog",
  version = "1.0",
  fps = 10,
}

local CATEGORIES = {
  ["Anything"] = 0, ["General knowledge"] = 9, ["Books"] = 10, ["Film"] = 11, ["Music"] = 12,
  ["Television"] = 14, ["Video games"] = 15, ["Science and nature"] = 17, ["Computers"] = 18,
  ["Sports"] = 21, ["Geography"] = 22, ["History"] = 23, ["Animals"] = 27,
}

config = {
  category = {
    label = "Category", default = "Anything", order = 1,
    options = { "Anything", "General knowledge", "Animals", "Books", "Computers", "Film", "Geography", "History",
      "Music", "Science and nature", "Sports", "Television", "Video games" },
  },
  difficulty = { label = "Difficulty", default = "Any", options = { "Any", "Easy", "Medium", "Hard" }, order = 2 },
}

local W, H = gfx.width, gfx.height
local ROUND = 10
local BUTTONS = { up = 1, center = 2, down = 3 }
local LETTERS = { "A", "B", "C" }
local QUESTION_LINES = 4
local ROW_Y, ROW_H = 68, 21 -- where the answers start, and how tall each is
local LOOK = 1.6            -- seconds to see which answer was right

local state = "start"       -- "start", "question", "answered" or "over"
local pool = {}             -- questions that fit the card, not asked yet
local fetching, problem = false, nil
local retry, tries = 0, 0   -- seconds until we ask again after being told to slow down, and how often we were
local current, asked, right, picked
local timer = 0
local best = 0
local dirty = true

local function decode(text)
  return (text:gsub("%%(%x%x)", function(hex) return string.char(tonumber(hex, 16)) end))
end

-- A fetched question as we show it: wrapped, with the right answer and two wrong ones in a random order.
local function prepare(item)
  if type(item.question) ~= "string" or type(item.correct_answer) ~= "string"
    or type(item.incorrect_answers) ~= "table" or #item.incorrect_answers < 2 then
    return nil
  end
  local lines = gfx.wrap(decode(item.question), W - 12, "small")
  if #lines > QUESTION_LINES then return nil end
  local wrong = {}
  for _, answer in ipairs(item.incorrect_answers) do wrong[#wrong + 1] = answer end
  while #wrong > 2 do table.remove(wrong, math.random(#wrong)) end -- three rows: the right one and two wrong
  local answers = { decode(item.correct_answer), decode(wrong[1]), decode(wrong[2]) }
  for _, answer in ipairs(answers) do
    if gfx.textWidth(answer, "small") > W - 34 then return nil end
  end
  local correct = math.random(3)
  answers[1], answers[correct] = answers[correct], answers[1]
  return { lines = lines, answers = answers, correct = correct }
end

local function fetch()
  if fetching then return end
  fetching, problem = true, nil
  -- One difficulty of one category can be a small set: ask for fewer, or nothing comes back.
  local amount = config.difficulty == "Any" and 30 or 12
  local url = "https://opentdb.com/api.php?amount=" .. amount .. "&type=multiple&encode=url3986"
  local category = CATEGORIES[config.category] or 0
  if category > 0 then url = url .. "&category=" .. category end
  if config.difficulty ~= "Any" then url = url .. "&difficulty=" .. config.difficulty:lower() end
  http.get(url, function(res)
    fetching, dirty = false, true
    local results = res.json and res.json.results
    if res.status == 429 or (res.json and res.json.response_code == 5) then
      -- The service takes one request per five seconds from an address. Ask again shortly.
      tries = tries + 1
      if tries <= 3 then retry = 6 else problem = "The question service is busy. Centre to try again" end
      return
    end
    tries = 0
    if type(results) ~= "table" then
      problem = res.error or "No questions came back. Centre to try again"
      return
    end
    for _, item in ipairs(results) do
      local question = prepare(item)
      if question then pool[#pool + 1] = question end
    end
    if #pool == 0 then problem = "Too few questions for this category and difficulty" end
  end)
end

local function nextQuestion()
  current = table.remove(pool)
  asked, picked = asked + 1, nil
  state, dirty = "question", true
end

local function newRound()
  asked, right = 0, 0
  nextQuestion()
end

function load()
  best = store.get("best", 0)
  if #pool < ROUND then fetch() end
end

function update(dt)
  if retry > 0 then
    retry = retry - dt
    if retry <= 0 then fetch() end
  end
  if state == "answered" then
    timer = timer - dt
    if timer <= 0 then
      if asked >= ROUND or #pool == 0 then
        state, dirty = "over", true
        if right > best then
          best = right
          store.set("best", best)
        end
        if #pool < ROUND then fetch() end -- ready for the next round
      else
        nextQuestion()
      end
    end
  end
  if not dirty then return false end
  dirty = false
end

function draw()
  if state == "start" or state == "over" then
    ui.title("Trivia")
    if state == "over" then
      ui.big(right .. " / " .. asked, right == asked and "green" or "white")
    else
      ui.big("Ready?")
    end
    if #pool < 1 and problem then
      ui.label(problem, "orange")
    elseif #pool < 1 then
      ui.label("Fetching questions...")
    elseif state == "over" then
      ui.label("Best " .. best .. ". Centre for another round")
    else
      ui.label(best > 0 and "Best " .. best .. " of " .. ROUND .. ". Centre to start" or "Centre to start")
    end
    return
  end

  gfx.rect(0, 0, W * asked / ROUND, 2, "#555555") -- how far into the round
  local lineHeight = gfx.textHeight("small")
  local y = 4 + (ROW_Y - 6 - #current.lines * lineHeight) // 2
  for _, line in ipairs(current.lines) do
    gfx.text(W / 2, y, line, "white", "small", "center")
    y = y + lineHeight
  end
  for i, answer in ipairs(current.answers) do
    local top = ROW_Y + (i - 1) * (ROW_H + 1)
    local back, tag = "#1c2330", "#34405a"
    if state == "answered" then
      if i == current.correct then back, tag = "#1e6b3a", "green"
      elseif i == picked then back, tag = "#7a1f1f", "red" end
    end
    gfx.rect(2, top, W - 4, ROW_H, back, 4)
    gfx.rect(4, top + 2, 18, ROW_H - 4, tag, 3)
    gfx.text(13, top + 3, LETTERS[i], "white", "small", "center")
    gfx.text(28, top + 3, answer, "white", "small")
  end
end

function button(name)
  if state == "question" then
    picked = BUTTONS[name]
    local good = picked == current.correct
    if good then right = right + 1 end
    led.set(good and "green" or "red", 1)
    state, timer, dirty = "answered", LOOK, true
    return true
  end
  if state == "answered" then return true end
  if name ~= "center" then return false end
  if #pool > 0 then
    newRound()
  else
    tries = 0
    fetch()
    dirty = true
  end
  return true
end
