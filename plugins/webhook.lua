-- A physical button for anything with a URL: press centre to POST to a webhook.
-- The LED and the card show whether it worked.
--
-- Set the URL from your computer:
--   tools/deskhog config webhook url=https://example.com/hook label=Deploy

plugin = {
  name = "Webhook",
  description = "A button that calls a web address and lights the LED",
  author = "DeskHog",
  version = "1.0",
}

config = {
  url = { label = "Webhook URL", default = "" },
  label = { label = "Button label", default = "Send" },
}

local status, color = "Press center", "gray"

function button(name)
  if name ~= "center" then return false end
  if config.url == "" then
    status, color = "No URL set", "orange"
    return true
  end

  status, color = "Sending...", "yellow"
  led.set("yellow")
  http.post(config.url, { source = "deskhog", label = config.label, time = time.now() }, function(res)
    if res.ok then
      status, color = "Sent", "green"
      led.set("green", 3)
    else
      status, color = "Failed: " .. (res.error or ("HTTP " .. res.status)), "red"
      led.set("red", 3)
    end
  end)
  return true
end

function draw()
  ui.title("Webhook")
  ui.big(config.label)
  ui.label(status, color)
end
