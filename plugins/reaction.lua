-- How fast are you? Press centre, wait for the screen to turn green, press
-- again. Your best time is kept.

plugin = {
  name = "Reaction timer",
  description = "Wait for green, then press. How many milliseconds did you need?",
  author = "Tim Broddin",
  version = "1.1",
  fps = 30,
}

local W, H = gfx.width, gfx.height
local state = "idle" -- "idle", "waiting", "go", "result" or "early"
local goAt, result
local best
local shown

function load()
  best = store.get("best", nil)
end

function update()
  if state == "waiting" and time.ms() >= goAt then
    state = "go"
    goAt = time.ms() -- count from the moment it turns green, not from when it was due
    led.set("green")
  end
  if state == shown then return false end
  shown = state
end

function draw()
  if state == "go" then
    gfx.clear("green")
    gfx.text(W / 2, 44, "NOW!", "black", "large", "center")
    return
  end
  if state == "waiting" then
    gfx.clear("#7a1f1f")
    gfx.text(W / 2, 52, "Wait for green...", "white", "medium", "center")
    return
  end
  ui.title("Reaction timer")
  if state == "result" then
    ui.big(result .. " ms", result == best and "green" or "white")
    ui.label(result == best and "Your best yet. Centre to go again" or "Best " .. best .. " ms. Centre to go again")
  elseif state == "early" then
    ui.big("Too soon", "orange")
    ui.label("Wait for green. Centre to try again")
  else
    ui.big("Ready?")
    ui.label(best and "Best " .. best .. " ms. Centre to start" or "Centre to start")
  end
end

function button(name)
  if name ~= "center" then return false end
  if state == "waiting" then
    state = "early"
    led.set("orange", 1)
  elseif state == "go" then
    result = time.ms() - goAt
    if not best or result < best then
      best = result
      store.set("best", best)
    end
    state = "result"
    led.off()
  else
    goAt = time.ms() + math.random(1500, 5000)
    state = "waiting"
    led.set("red")
  end
  return true
end
