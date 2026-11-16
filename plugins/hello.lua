-- The smallest useful plugin: it draws three lines of text and counts presses
-- of the centre button.

plugin = {
  name = "Hello",
  description = "The smallest useful plugin: three lines of text and a press counter",
  author = "Tim Broddin",
  version = "1.1",
}

local presses = 0

function draw()
  ui.title("Hello")
  ui.big("Hi there")
  ui.label(presses .. (presses == 1 and " press" or " presses"))
end

function button(name)
  if name == "center" then
    presses = presses + 1
    return true -- we used the press, so the card stack does not move
  end
end
