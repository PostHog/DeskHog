-- A screensaver: stars flying past. Centre changes speed.

plugin = {
  name = "Starfield",
  description = "Stars flying past, for when the desk needs to look busy",
  author = "DeskHog",
  version = "1.0",
  fps = 20,
}

local W, H = gfx.width, gfx.height
local STARS = 30
local SPEEDS = { 0.35, 0.8, 1.6 }
local speed = 2
local stars = {}

local function reset(star, far)
  star.x = (math.random() - 0.5) * 2
  star.y = (math.random() - 0.5) * 2
  star.z = far and 1 or math.random() * 0.9 + 0.1
end

function load()
  if #stars > 0 then return end
  for i = 1, STARS do
    stars[i] = {}
    reset(stars[i], false)
  end
end

function update(dt)
  for _, star in ipairs(stars) do
    star.z = star.z - SPEEDS[speed] * dt
    if star.z <= 0.02 then reset(star, true) end
  end
end

function draw()
  for _, star in ipairs(stars) do
    local x = W / 2 + star.x / star.z * W / 2
    local y = H / 2 + star.y / star.z * H / 2
    if x < 0 or x >= W or y < 0 or y >= H then
      reset(star, true)
    else
      local near = 1 - star.z
      local size = near > 0.8 and 3 or near > 0.5 and 2 or 1
      local shade = math.floor(90 + near * 165)
      gfx.rect(x, y, size, size, shade * 65536 + shade * 256 + shade)
    end
  end
end

function button(name)
  if name ~= "center" then return false end
  speed = speed % #SPEEDS + 1
  return true
end
