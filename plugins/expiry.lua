-- Expiry: how many days until the TLS certificate and the domain registration of
-- your sites run out, one row per site. Green when there is more than a month
-- left, orange within 30 days, red within a week or once it has expired.
--
-- The device cannot look inside a certificate from Lua, so it asks
-- networkcalc.com, which connects to the site and reports the certificate it
-- was handed (free, no API key). Certificate Transparency logs (crt.sh, Cert
-- Spotter) were the alternative, but they list every certificate ever issued
-- rather than the one in use, and crt.sh is often slow or down.
--
-- The domain comes from RDAP, the registries' own replacement for whois, through
-- rdap.org, which forwards to the right registry. Not every registry takes part:
-- .be, .de, .io and .ch have no RDAP service, and .nl has one without the date.
-- Those show n/a in the domain column.
--
-- What it finds is saved as dates, so after a restart the card is filled in at
-- once. It looks again every six hours, one request after the other. With more
-- than five sites the centre button turns the page; otherwise it checks again.

plugin = {
  name = "Expiry",
  description = "Days until the certificates and domain names of your sites expire. Orange LED a month ahead",
  author = "DeskHog",
  version = "1.0",
  refresh = 21600,
}

config = {
  domains = { label = "Sites to watch, up to ten, separated by spaces (e.g. example.com)", default = "", order = 1 },
  light = { label = "Light the LED when one expires within 30 days", default = true, order = 2 },
}

local W = gfx.width
local MOST, ROWS = 10, 5
local GREEN, ORANGE, RED = "#2ecc71", "#f39c12", "#e74c3c"
local CERT_X, DOMAIN_X = W - 68, W - 8 -- right edges of the two columns

-- A site is { host, domain, cert, reg }. cert and reg hold the expiry in minutes
-- since 1970 (seconds would not fit in 32 bits after 2038), "none" when there is
-- no such site or domain, "n/a" when the registry does not publish the date, or
-- nil while unknown.
local sites = {}
local page = 1
local busy, startedAt = false, 0
local answered, failed = 0, 0 -- requests in the last round that did and did not get an answer

-- Days since 1970-01-01 for a date (Howard Hinnant's days_from_civil).
local function daysFrom(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * (m + (m > 2 and -3 or 9)) + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end

-- "2027-01-27T23:59:59.000Z" becomes minutes since 1970.
local function minutesOf(text)
  local y, m, d, hh, mm = tostring(text or ""):match("^(%d+)-(%d+)-(%d+)T?(%d*):?(%d*)")
  if not y then return nil end
  return daysFrom(tonumber(y), tonumber(m), tonumber(d)) * 1440 + (tonumber(hh) or 0) * 60 + (tonumber(mm) or 0)
end

local function daysLeft(value)
  local now = time.now()
  if type(value) ~= "number" or now == 0 then return nil end
  return (value - now // 60) // 1440
end

-- 0 fine, 1 within a month, 2 within a week or gone; nil when there is no date.
local function levelOf(value)
  local days = daysLeft(value)
  if not days then return nil end
  return days <= 7 and 2 or days <= 30 and 1 or 0
end
local COLORS = { [0] = GREEN, [1] = ORANGE, [2] = RED }

local function cell(value, missing)
  local days = daysLeft(value)
  if days then
    local text = days < 0 and "expired" or days == 0 and "today" or days .. " d"
    return text, COLORS[levelOf(value)]
  end
  if value == "none" then return missing, "gray" end
  if value == "n/a" then return "n/a", "gray" end
  return busy and "..." or "?", "gray"
end

local ledLater = false -- the clock was not set yet, so the LED waits for it

local function showLed()
  if not config.light then return end
  ledLater = time.now() == 0
  local worst = 0
  for _, site in ipairs(sites) do
    worst = math.max(worst, levelOf(site.cert) or 0, levelOf(site.reg) or 0)
  end
  if worst == 2 then led.set("red") elseif worst == 1 then led.set("#ff8800") else led.off() end
end

local function save()
  local saved = {}
  for _, site in ipairs(sites) do saved[site.host] = { c = site.cert or false, r = site.reg or false } end
  store.set("seen", saved)
end

-- The name, shortened with ".." when it is wider than room.
local function fit(name, room)
  if gfx.textWidth(name, "small") <= room then return name end
  while #name > 1 and gfx.textWidth(name .. "..", "small") > room do name = name:sub(1, -2) end
  return name .. ".."
end

-- "https://www.Example.com:443/path" becomes "www.example.com".
local function hostOf(text)
  local host = text:lower():gsub("^%a+://", ""):match("^([%w%-%.]+)")
  return host and (host:gsub("%.$", ""))
end

-- The part of the name that is registered: "www.bbc.co.uk" becomes "bbc.co.uk".
-- A guess without the public suffix list, right for the usual cases.
local SECOND = { co = true, com = true, net = true, org = true, ac = true, gov = true, edu = true, ltd = true, plc = true, ne = true, ["or"] = true, go = true }
local function domainOf(host)
  local labels = {}
  for label in host:gmatch("[^%.]+") do labels[#labels + 1] = label end
  local n = #labels
  local keep = (n >= 3 and #labels[n] == 2 and SECOND[labels[n - 1]]) and 3 or 2
  return table.concat(labels, ".", math.max(1, n - keep + 1), n)
end

local function fetchCert(site, done)
  local url = "https://networkcalc.com/api/security/certificate/" .. site.host
  http.get(url, { timeout = 15, only = { status = true, certificate = { valid_to = true } } }, function(res)
    local body = res.json or {}
    local expires = body.certificate and minutesOf(body.certificate.valid_to)
    if res.json then answered = answered + 1 end
    if expires then
      site.cert = expires
    elseif body.status == "HOST_NOT_RESOLVED" or body.status == "INVALID_HOST" then
      site.cert = "none"
    else
      failed = failed + 1 -- keep the date we had: it is still the same certificate, most likely
      print(site.host .. " certificate: " .. (res.error or body.status or ("HTTP " .. res.status)))
    end
    done()
  end)
end

local function fetchDomain(site, known, done)
  if known[site.domain] ~= nil then
    site.reg = known[site.domain]
    return done()
  end
  local wanted = { events = { { eventAction = true, eventDate = true } }, title = true }
  http.get("https://rdap.org/domain/" .. site.domain, { timeout = 15, only = wanted }, function(res)
    local body = res.json or {}
    if res.status > 0 then answered = answered + 1 end
    if res.ok then
      site.reg = "n/a" -- registered, but some registries leave the date out
      for _, event in ipairs(body.events or {}) do
        if event.eventAction == "expiration" then site.reg = minutesOf(event.eventDate) or "n/a" end
      end
    elseif res.status == 404 then
      -- rdap.org says so when the registry has no RDAP; the registry itself when the domain is free.
      site.reg = (body.title or ""):find("No RDAP", 1, true) and "n/a" or "none"
    else
      failed = failed + 1
      print(site.domain .. " registration: " .. (res.error or ("HTTP " .. res.status)))
    end
    known[site.domain] = site.reg
    done()
  end)
end

-- One request at a time, each started when the one before it has answered.
local function checkAll()
  if busy and time.ms() - startedAt < 120000 then return end
  busy, startedAt, answered, failed = true, time.ms(), 0, 0
  local known, i = {}, 0
  local function nextSite()
    i = i + 1
    local site = sites[i]
    if not site then
      busy = false
      save()
      showLed()
      return
    end
    fetchCert(site, function()
      fetchDomain(site, known, nextSite)
    end)
  end
  nextSite()
end

function load()
  if #sites == 0 then
    local seen = store.get("seen", {})
    for word in config.domains:gmatch("[^%s,|]+") do
      local host = hostOf(word)
      if host and host:find(".", 1, true) and #sites < MOST then
        local old = seen[host] or {}
        sites[#sites + 1] = { host = host, domain = domainOf(host), cert = old.c or nil, reg = old.r or nil }
      end
    end
    showLed()
  end
  if #sites > 0 then checkAll() end
end

function draw()
  if ledLater then showLed() end
  if #sites == 0 then
    ui.title("Expiry")
    ui.label("Add the sites to watch on the web page")
    return
  end

  -- Top line: how things stand, and the column names.
  local gone, week, month = 0, 0, 0
  for _, site in ipairs(sites) do
    local days = math.min(daysLeft(site.cert) or 9999, daysLeft(site.reg) or 9999)
    if days < 0 then gone = gone + 1 elseif days <= 7 then week = week + 1 elseif days <= 30 then month = month + 1 end
  end
  local summary, color = "All fine", "gray"
  if gone > 0 then
    summary, color = gone .. " expired", RED
  elseif week > 0 then
    summary, color = week .. " this week", RED
  elseif month > 0 then
    summary, color = month .. " this month", ORANGE
  elseif busy then
    summary = "Checking"
  elseif failed > 0 and answered == 0 then
    summary = "Offline"
  end
  local pages = (#sites + ROWS - 1) // ROWS
  if page > pages then page = 1 end
  if pages > 1 then summary = summary .. "  " .. page .. "/" .. pages end
  gfx.text(8, 4, summary, color, "small")
  gfx.text(CERT_X, 4, "cert", "gray", "small", "right")
  gfx.text(DOMAIN_X, 4, "domain", "gray", "small", "right")
  gfx.line(8, 23, W - 8, 23, "#333333")

  local first = (page - 1) * ROWS
  for row = 1, ROWS do
    local site = sites[first + row]
    if not site then break end
    local y = 27 + (row - 1) * 21
    local level = math.max(levelOf(site.cert) or -1, levelOf(site.reg) or -1)
    gfx.circle(14, y + 9, 4, COLORS[level] or "gray")
    local certText, certColor = cell(site.cert, "no site")
    local regText, regColor = cell(site.reg, "none")
    local left
    if site.cert == "none" and site.reg == "none" then
      gfx.text(DOMAIN_X, y, "not found", ORANGE, "small", "right")
      left = DOMAIN_X - gfx.textWidth("not found", "small")
    else
      gfx.text(CERT_X, y, certText, certColor, "small", "right")
      gfx.text(DOMAIN_X, y, regText, regColor, "small", "right")
      left = CERT_X - gfx.textWidth(certText, "small")
    end
    gfx.text(24, y, fit(site.host, left - 7 - 24), "white", "small")
  end
end

-- Centre turns the page, or with one page checks again right now.
function button(name)
  if name ~= "center" then return false end
  if #sites > ROWS then
    page = page % ((#sites + ROWS - 1) // ROWS) + 1
  elseif #sites > 0 then
    checkAll()
  end
  return true
end
