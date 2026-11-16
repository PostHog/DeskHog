-- A word of the day: the word, what kind of word it is, its syllables and what
-- it means. A new one when the date changes; centre shows the next meaning,
-- then an example or why the word was picked today.
--
-- In English the word is Wiktionary's Word of the day, chosen by Wiktionary's
-- editors (en.wiktionary.org), and the syllables come from the word's own entry.
-- In Dutch it is WikiWoordenboek's Woord van de dag (nl.wiktionary.org), with the
-- meanings and examples from the word's entry. Both are free and need no key.
-- Without an internet connection the card picks a word from a short list of its
-- own, and tries again later.
--
-- The card's fonts have no phonetic symbols, so the pronunciation is shown as
-- syllables (ka·ta·ba·sis) rather than in IPA.

plugin = {
  name = "Word of the day",
  description = "A new word every day, with what it means, from Wiktionary",
  author = "DeskHog",
  version = "1.0",
  refresh = 3600, -- try again every hour after a failed lookup
  fps = 20,       -- for scrolling a long meaning; repainted only when something moved
}

config = {
  language = { label = "Language", default = "English", options = { "English", "Nederlands" }, order = 1 },
  offset = { label = "Hours ahead of UTC, for when the new day starts (for example 1 or -5)", default = "0", order = 2 },
}

local W, H = gfx.width, gfx.height
local PAD = 8
local ACCENT, EXAMPLE, NOTE = "#7fb3ff", "#ffd479", "#9be3a8"
local DOT = "\194\183" -- the middle dot between syllables
local MAX_PAGES = 8
local MONTHS = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
  "November", "December" }

local TEXT = {
  en = {
    title = "Word of the day",
    loading = "Looking up today's word...",
    waiting = "Waiting for the time. It comes from the internet once WiFi is connected.",
    nothing = "No meaning found for this one.",
    example = "example",
    note = "why today",
    offline = "offline",
  },
  nl = {
    title = "Woord van de dag",
    loading = "Het woord van vandaag opzoeken...",
    waiting = "Wachten op de tijd. Die komt van het internet zodra de wifi verbonden is.",
    nothing = "Geen betekenis gevonden voor dit woord.",
    example = "voorbeeld",
    note = "waarom vandaag",
    offline = "offline",
  },
}

-- Wiktionary's short parts of speech.
local EN_POS = {
  n = "noun", v = "verb", adj = "adjective", adv = "adverb", prep = "preposition", conj = "conjunction",
  interj = "interjection", pron = "pronoun", num = "numeral", det = "determiner", art = "article",
  ["proper n"] = "proper noun", abbr = "abbreviation", init = "initialism", ["prep phrase"] = "prepositional phrase",
}
-- WikiWoordenboek's codes, in its list of words and in the headings of an entry.
local NL_POS = {
  noun = "zelfstandig naamwoord", adjc = "bijvoeglijk naamwoord", verb = "werkwoord", advb = "bijwoord",
  adverb = "bijwoord", prep = "voorzetsel", name = "eigennaam", num = "telwoord", conj = "voegwoord",
  interj = "tussenwerpsel", phrase = "uitdrukking",
}

-- For when there is no internet: word|part of speech|syllables|meaning.
local OWN = {
  en = {
    "petrichor|noun|pet·ri·chor|The earthy smell that rises when rain falls on dry ground.",
    "serendipity|noun|ser·en·dip·i·ty|Finding something good by chance while looking for something else.",
    "ephemeral|adjective|e·phem·er·al|Lasting only a very short time.",
    "defenestration|noun|de·fen·es·tra·tion|The act of throwing someone or something out of a window.",
    "sesquipedalian|adjective|ses·qui·pe·da·li·an|Fond of long words; of a word: very long.",
    "apricity|noun|a·pric·i·ty|The warmth of the sun on a cold winter's day.",
    "crepuscular|adjective|cre·pus·cu·lar|Of twilight; of animals: most active at dawn and dusk.",
    "susurrus|noun|su·sur·rus|A soft whispering or rustling sound, like wind in the leaves.",
    "quidnunc|noun|quid·nunc|Someone who always wants to know the latest news and gossip.",
    "borborygmus|noun|bor·bo·ryg·mus|The rumbling of a hungry stomach.",
    "kerfuffle|noun|ker·fuf·fle|A commotion or fuss, usually over something small.",
    "gallimaufry|noun|gal·li·mau·fry|A jumbled mixture of all sorts of things.",
    "lollygag|verb|lol·ly·gag|To dawdle, or spend time doing nothing much.",
    "mellifluous|adjective|mel·lif·lu·ous|Of a voice or a sound: sweet, smooth and pleasant to hear.",
    "penumbra|noun|pe·num·bra|The partly lit outer edge of a shadow.",
    "ubiquitous|adjective|u·biq·ui·tous|Seeming to be everywhere at once.",
    "flâneur|noun|flâ·neur|Someone who strolls through a city with no aim but to watch its life.",
    "palimpsest|noun|pal·imp·sest|A page written on again after the old text was scraped off; anything that still shows traces of earlier layers.",
    "quixotic|adjective|quix·ot·ic|Idealistic in a way that is noble but not practical.",
    "syzygy|noun|syz·y·gy|Three heavenly bodies, such as the sun, the earth and the moon, lined up in a row.",
    "zephyr|noun|zeph·yr|A soft, gentle breeze.",
    "cacophony|noun|ca·coph·o·ny|A harsh, jarring mix of sounds.",
    "obfuscate|verb|ob·fus·cate|To make something unclear on purpose.",
    "perspicacious|adjective|per·spi·ca·cious|Quick to notice and understand things.",
    "snollygoster|noun|snol·ly·gos·ter|A shrewd person, often a politician, led by self-interest rather than principle.",
    "tintinnabulation|noun|tin·tin·nab·u·la·tion|The ringing and tinkling of bells.",
    "verisimilitude|noun|ver·i·si·mil·i·tude|The quality of seeming true or real.",
    "wanderlust|noun|wan·der·lust|A strong wish to travel and see the world.",
    "halcyon|adjective|hal·cy·on|Calm, happy and peaceful, usually of a time remembered fondly.",
    "collywobbles|noun|col·ly·wob·bles|Butterflies in the stomach; a fit of nerves.",
    "discombobulate|verb|dis·com·bob·u·late|To confuse someone or throw them off balance.",
    "interrobang|noun|in·ter·ro·bang|A punctuation mark that is a question mark and an exclamation mark in one.",
    "nudiustertian|adjective|nu·di·us·ter·tian|Of the day before yesterday.",
    "ineffable|adjective|in·ef·fa·ble|Too great or too strange to be put into words.",
    "jentacular|adjective|jen·tac·u·lar|Of breakfast, especially one eaten early.",
    "onomatopoeia|noun|on·o·mat·o·poe·ia|A word that sounds like what it describes, such as buzz or sizzle.",
    "gobbledygook|noun|gob·ble·dy·gook|Wordy language so full of jargon that nobody can follow it.",
    "scuttlebutt|noun|scut·tle·butt|Rumour and gossip.",
    "lagniappe|noun|la·gniappe|A small extra thrown in for free with a purchase.",
    "hullabaloo|noun|hul·la·ba·loo|A noisy fuss or uproar.",
    "mondegreen|noun|mon·de·green|A misheard phrase, especially in the words of a song.",
    "apophenia|noun|a·po·phe·ni·a|The habit of seeing patterns or meaning in random things.",
    "sobriquet|noun|so·bri·quet|A nickname.",
    "pareidolia|noun|par·ei·do·li·a|Seeing faces or familiar shapes in things such as clouds or plug sockets.",
  },
  nl = {
    "gezellig|bijvoeglijk naamwoord|ge·zel·lig|Prettig, knus en ontspannen, meestal samen met anderen.",
    "uitwaaien|werkwoord|uit·waai·en|In de wind een frisse neus halen om je hoofd leeg te maken.",
    "plezant|bijvoeglijk naamwoord|ple·zant|(Belgisch) Aangenaam, leuk om te doen.",
    "foefelen|werkwoord|foe·fe·len|(Belgisch) Sjoemelen, iets stiekem regelen.",
    "goesting|zelfstandig naamwoord|goes·ting|(Belgisch) Zin of trek in iets.",
    "smoutebol|zelfstandig naamwoord|smou·te·bol|(Belgisch) Een gefrituurde deegbal met poedersuiker, van de kermis.",
    "kattenkwaad|zelfstandig naamwoord|kat·ten·kwaad|Onschuldige kwajongensstreken.",
    "slaapdronken|bijvoeglijk naamwoord|slaap·dron·ken|Nog suf en wankel, net wakker geworden.",
    "hebbedingetje|zelfstandig naamwoord|heb·be·din·ge·tje|Een leuk maar overbodig ding dat je toch graag wilt hebben.",
    "klungelen|werkwoord|klun·ge·len|Onhandig en zonder veel resultaat aan iets werken.",
    "schemering|zelfstandig naamwoord|sche·me·ring|Het halfdonker vlak na zonsondergang of voor zonsopgang.",
    "dagdromen|werkwoord|dag·dro·men|Met je gedachten ver weg zijn, in prettige fantasieën.",
    "vergeetachtig|bijvoeglijk naamwoord|ver·geet·ach·tig|Geneigd om dingen snel te vergeten.",
    "struisvogelpolitiek|zelfstandig naamwoord|struis·vo·gel·po·li·tiek|Doen alsof een probleem niet bestaat, in de hoop dat het vanzelf overgaat.",
    "doorzetter|zelfstandig naamwoord|door·zet·ter|Iemand die niet opgeeft als het moeilijk wordt.",
    "muggenziften|werkwoord|mug·gen·zif·ten|Vitten op onbelangrijke details.",
    "appelflauwte|zelfstandig naamwoord|ap·pel·flauw·te|Een gespeelde flauwte, om aandacht te krijgen of ergens onderuit te komen.",
    "klakkeloos|bijwoord|klak·ke·loos|Zonder na te denken of te controleren.",
    "schaterlachen|werkwoord|scha·ter·la·chen|Hard en uitbundig lachen.",
    "gortdroog|bijvoeglijk naamwoord|gort·droog|Kurkdroog; ook: heel saai.",
    "pruttelen|werkwoord|prut·te·len|Zacht borrelen, zoals soep op het vuur; ook: binnensmonds mopperen.",
  },
}

local lang = "en"
local entry        -- the word on screen: { key, lang, word, syll, pages = { { pos, text, kind } }, stale, offline }
local current = 1  -- which page of it
local fetching     -- the key of the day being looked up
local lines, font, top, visible = {}, "small", 0, 0
local wordFont, wordLines = "large", {}
local SPEED, PAUSE = 20, 2500 -- pixels a second, and milliseconds to rest at each end
local overflow, offset, direction, restUntil, lastMs = 0, 0, 1, 0, 0

-- Days since 1970 to year, month and day.
local function date(days)
  local year = 1970
  while true do
    local leap = year % 4 == 0 and (year % 100 ~= 0 or year % 400 == 0)
    local length = leap and 366 or 365
    if days < length then
      local lengths = { 31, leap and 29 or 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
      for month, monthLength in ipairs(lengths) do
        if days < monthLength then return year, month, days + 1 end
        days = days - monthLength
      end
    end
    days = days - length
    year = year + 1
  end
end

-- Today in the person's time zone, as days since 1970; nil until the clock is set.
local function today()
  local utc = time.now()
  if utc == 0 then return nil end
  return (utc + math.floor((tonumber(config.offset) or 0) * 3600 + 0.5)) // 86400
end

local function encode(text)
  return (text:gsub("[^%w%-%._~:/]", function(c) return string.format("%%%02X", c:byte()) end))
end

-- The fonts have Western European letters only: fold or drop the rest.
local FOLD = "AaAaAaCcCcCcCcDdDdEeEeEeEeEeGgGgGgGgHhHhIiIiIiIiIiIiJjKkkLlLlLlLlLlNnNnNnnNnOoOoOoOoRrRrRrSsSsSsSsTtTtTtUuUuUuUuUuUuWwYyYZzZzZzs"
local SWAP = {
  [0x2018] = "'", [0x2019] = "'", [0x2032] = "'", [0x201C] = '"', [0x201D] = '"', [0x201E] = '"', [0x2033] = '"',
  [0x2013] = "-", [0x2014] = " - ", [0x2212] = "-", [0x2010] = "-", [0x2011] = "-", [0x2026] = "...",
  [0x2009] = " ", [0x200A] = " ", [0x202F] = " ", [0x2002] = " ", [0x2003] = " ",
}
local function latin1(text)
  if not text:find("[\128-\255]") then return text end
  local out = {}
  local ok = pcall(function()
    for _, code in utf8.codes(text) do
      if code < 0x80 or (code >= 0xA0 and code <= 0xFF) then
        out[#out + 1] = utf8.char(code)
      elseif code >= 0x100 and code <= 0x17F then
        out[#out + 1] = FOLD:sub(code - 0xFF, code - 0xFF)
      elseif SWAP[code] then
        out[#out + 1] = SWAP[code]
      end
    end
  end)
  if not ok then return (text:gsub("[\128-\255]", "")) end
  return table.concat(out)
end

local ENTITIES = { amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = " " }
local function entity(name)
  local number = name:match("^#x(%x+)$")
  number = number and tonumber(number, 16) or tonumber(name:match("^#(%d+)$") or "")
  if number then return number < 0x110000 and utf8.char(number) or "" end
  return ENTITIES[name] or ("&" .. name .. ";")
end

local function tidy(text)
  text = text:gsub("&(#?%w+);", entity)
  text = latin1(text):gsub("%s+", " ")
  return text:match("^%s*(.-)%s*$")
end

local function fromHtml(html)
  return tidy(html:gsub("<style.-</style>", ""):gsub("<[^>]*>", ""))
end

-- {{figuurlijk|nld}} is a label, (figuurlijk); {{w|Gent}} keeps its text, {{species|Malus}}
-- becomes (Malus); the rest goes.
local function template(inner)
  local parts = {}
  for part in (inner .. "|"):gmatch("(.-)|") do parts[#parts + 1] = part end
  local name = parts[1]:match("^%s*(.-)%s*$")
  if (name == "w" or name == "l") and parts[2] then return parts[#parts] end
  if name == "species" and parts[2] then return "(" .. parts[2] .. ")" end
  if parts[2] == "nld" and table.concat(parts, "", 3) == "" then return "(" .. name .. ")" end
  return ""
end

local function fromWiki(text)
  text = text:gsub("<!%-%-.-%-%->", ""):gsub("<ref.-</ref>", ""):gsub("<ref[^>]*/>", "")
  text = text:gsub("%[%[[^%]|]*|([^%]]*)%]%]", "%1"):gsub("%[%[([^%]]*)%]%]", "%1")
  text = text:gsub("'''?", "")
  local count
  repeat text, count = text:gsub("{{([^{}]*)}}", template) until count == 0
  return tidy(text:gsub("<[^>]*>", ""))
end

local function shorten(text)
  if #text <= 400 then return text end
  return text:sub(1, 397):gsub("[\128-\191]*$", ""):gsub("[\192-\255]$", "") .. "..."
end

-- ---------------------------------------------------------------- the screen

local function words() return TEXT[lang] end

-- The meaning in medium if it fits under the word, otherwise small (and always
-- small under a medium word). What still does not fit scrolls.
local function wrap(text, top)
  for _, f in ipairs({ "medium", "small" }) do
    local wrapped, lineHeight = gfx.wrap(text, W - 2 * PAD, f), gfx.textHeight(f)
    local room = (H - top) // lineHeight * lineHeight -- whole lines only
    if (#wrapped * lineHeight <= room and wordFont ~= "medium") or f == "small" then
      return wrapped, f, math.max(0, #wrapped * lineHeight - room), room
    end
  end
end

local function textTop() return 2 + #wordLines * gfx.textHeight(wordFont) + gfx.textHeight("small") + 4 end

local function pageText(page) return page.text ~= "" and page.text or words().nothing end

-- The word large if it fits, and smaller when that makes room for the whole
-- first meaning; when nothing does, the meaning scrolls under the large word.
-- A long phrase goes over two lines. The word stays the same size on every page.
local function pickWord()
  local room = W - 2 * PAD
  local fonts = {}
  for _, f in ipairs({ "large", "medium" }) do
    if gfx.textWidth(entry.word, f) <= room then
      fonts[#fonts + 1] = f
    end
  end
  wordLines = { entry.word }
  for _, f in ipairs(fonts) do
    wordFont = f
    local _, _, overflow = wrap(pageText(entry.pages[1]), textTop())
    if overflow == 0 then return end
  end
  if #fonts > 0 then
    wordFont = fonts[1]
    return
  end
  wordFont = "medium"
  wordLines = gfx.wrap(entry.word, room, "medium")
  for i = #wordLines, 3, -1 do wordLines[i] = nil end
end

local function layout()
  if not entry then return end
  top = textTop()
  lines, font, overflow, visible = wrap(pageText(entry.pages[current]), top)
  offset, direction, restUntil = 0, 1, time.ms() + PAUSE
end

local function show(e)
  entry, current = e, 1
  pickWord()
  layout()
end

local function save()
  if not entry or entry.offline then return end
  while #entry.pages > 1 and #json.encode(entry) > 1900 do entry.pages[#entry.pages] = nil end
  store.set("word", entry)
end

-- A word from the card's own list, the same one all day.
local function ownWord(day, key)
  local list = OWN[lang]
  local word, pos, syll, meaning = list[day % #list + 1]:match("^(.-)|(.-)|(.-)|(.*)$")
  return { key = key, lang = lang, word = word, syll = syll, offline = true, stale = true,
    pages = { { pos = pos, text = meaning } } }
end

-- ------------------------------------------------------------------- English

local WIKT = "https://en.wiktionary.org/w/api.php?format=json&formatversion=2&action=parse"

local function parseWotd(html)
  local word = html:match('id="WOTD%-rss%-title">(.-)</span>')
  local _, start = html:find('id="WOTD-rss-description">', 1, true)
  if not word or not start then return nil end
  local pos = html:match("</b>%s*<i>(.-)</i>", html:find('id="WOTD-rss-title"', 1, true))
  local body = html:sub(start + 1, (html:find("</td>", start, true) or #html + 1) - 1)

  -- Why the word was picked today: a small note after a pointing hand.
  local note
  local hand = body:find("PointingHand", 1, true)
  if hand then
    note = body:sub(hand):match('<span style="font%-size:80%%;">(.*)</span>')
    body = body:sub(1, (body:sub(1, hand):match(".*()<p>") or hand) - 1)
  end

  -- One page per meaning, leaving out what goes under one (a colour panel, say). A second
  -- part of speech starts with <p><b>word</b> <i>n</i> and maybe a label</p>.
  body = body:gsub("<style.-</style>", ""):gsub("<dl>.-</dl>", "")
  body = body:gsub("<p><b>.-</b>%s*<i>(.-)</i>(.-)</p>", "\2%1\3%2"):gsub("<li[^>]*>", "\1")
  local pages, label = {}, ""
  local kind = pos and fromHtml(pos) or ""
  kind = EN_POS[kind] or kind
  for piece in (body .. "\1"):gmatch("(.-)\1") do
    local text, change, over = piece, nil, nil
    local a, b, p = piece:find("\2(.-)\3")
    if a then text, change, over = piece:sub(1, a - 1), p, fromHtml(piece:sub(b + 1)) end
    text = fromHtml(text)
    if text:match("^%b()$") then
      label = text .. " " -- a label over the meanings below it, such as (by extension)
    elseif text ~= "" and #pages < MAX_PAGES then
      pages[#pages + 1] = { pos = kind, text = shorten(label .. text) }
      label = ""
    end
    if change then
      kind = fromHtml(change)
      kind, label = EN_POS[kind] or kind, over ~= "" and over .. " " or ""
    end
  end
  if #pages == 0 then pages[1] = { pos = kind, text = "" } end
  note = note and fromHtml(note)
  if note and note ~= "" then
    if #pages >= MAX_PAGES then pages[#pages] = nil end
    pages[#pages + 1] = { kind = "note", text = shorten(note) }
  end
  return { word = fromHtml(word), pages = pages }
end

-- The syllables, from the hyphenation in the Pronunciation part of the word's entry.
local function fetchSyllables(e)
  local page = encode(e.word:gsub(" ", "_"))
  http.get(WIKT .. "&prop=sections&page=" .. page,
    { only = { parse = { sections = { { line = true, level = true, index = true } } } } }, function(res)
    local sections = res.json and res.json.parse and res.json.parse.sections
    if type(sections) ~= "table" then return end
    local english, index = false, nil
    for _, section in ipairs(sections) do
      if section.level == "2" then
        if english then break end
        english = section.line == "English"
      elseif english and section.line == "Pronunciation" then
        index = tonumber(section.index)
        break
      end
    end
    if not index then return end
    http.get(WIKT .. "&prop=wikitext&section=" .. index .. "&page=" .. page, { only = { parse = { wikitext = true } } },
      function(res2)
        local text = res2.json and res2.json.parse and res2.json.parse.wikitext
        local found = type(text) == "string" and text:match("{{hyph%a*|en|([^}]*)}}")
        if not found or entry ~= e then return end
        local parts = {}
        for part in (found .. "|"):gmatch("(.-)|") do
          if part == "" then break end
          if not part:find("=", 1, true) then parts[#parts + 1] = latin1(part) end
        end
        if #parts < 2 then return end
        e.syll = table.concat(parts, DOT)
        layout()
        save()
      end)
  end)
end

local function fetchEnglish(day, key, earlier, failed)
  local year, month, d = date(day)
  local url = WIKT .. "&prop=text&disablelimitreport=1&disableeditsection=1&page=Wiktionary:Word_of_the_day/"
    .. year .. "/" .. MONTHS[month] .. "_" .. d
  http.get(url, { only = { parse = { text = true }, error = { code = true } }, timeout = 15 }, function(res)
    if fetching ~= key then return end
    local html = res.json and res.json.parse and res.json.parse.text
    if type(html) ~= "string" then
      -- Ahead of UTC, today's page may not be up yet: show yesterday's until it is.
      if res.json and res.json.error and not earlier then return fetchEnglish(day - 1, key, true, failed) end
      return failed()
    end
    local e = parseWotd(html)
    if not e then return failed() end
    fetching = nil
    e.key, e.lang, e.stale = key, "en", earlier or nil
    show(e)
    save()
    fetchSyllables(e)
  end)
end

-- --------------------------------------------------------------------- Dutch

local WIKW = "https://nl.wiktionary.org/w/api.php?format=json&formatversion=2"

local function parseDutch(text, word)
  local start = text:find("{{=nld=}}", 1, true)
  if not start then return nil end
  text = text:sub(start + 9)
  local stop = text:find("{{=[%a%-]+=}}")
  if stop then text = text:sub(1, stop - 1) end
  text = text:gsub("{{pn}}", function() return word end)

  local syll = text:match("{{%-syll%-}}%s*%*%s*([^\n]+)")
  local pages, kind, last = {}, nil, nil
  for line in text:gmatch("[^\n]+") do
    local head = line:match("^{{%-(%a+)%-|nld")
    if head and NL_POS[head] then
      kind = NL_POS[head]
    elseif line:match("^#+[^#:*]") and #pages < MAX_PAGES then
      -- WikiWoordenboek starts its meanings in lower case, after any labels.
      local meaning = fromWiki(line:match("^#+(.*)"))
      local at = 1
      while meaning:match("^%b()%s*", at) do at = at + #meaning:match("^%b()%s*", at) end
      meaning = meaning:sub(1, at - 1) .. meaning:sub(at, at):upper() .. meaning:sub(at + 1)
      if meaning ~= "" then
        last = { pos = kind, text = shorten(meaning) }
        pages[#pages + 1] = last
      end
    elseif last and #pages < MAX_PAGES and line:match("^{{bijv%-1|") then
      local example = fromWiki(line:match("^{{bijv%-1|(.-)}}%s*$") or "")
      if example ~= "" then pages[#pages + 1] = { kind = "example", text = shorten(example) } end
      last = nil -- one example per meaning
    end
  end
  syll = syll and fromWiki(syll)
  return pages, (syll and syll:find(DOT, 1, true)) and syll or nil
end

local function fetchDutch(day, key, failed)
  local _, month, d = date(day)
  local list = "{{Woordvandedaglijst|" .. d .. "|" .. string.format("%02d", month) .. "}}"
  http.get(WIKW .. "&action=expandtemplates&prop=wikitext&text=" .. encode(list), function(res)
    if fetching ~= key then return end
    local line = res.json and res.json.expandtemplates and res.json.expandtemplates.wikitext
    if type(line) ~= "string" then return failed() end
    local word, code = line:match("^%s*(.-)%s+(%l+)%s*$")
    if not word or not NL_POS[code] then word, code = line:match("^%s*(.-)%s*$"), nil end
    if word == "" or word:find("[{}|]") then return failed() end
    local e = { key = key, lang = "nl", word = latin1(word), pages = { { pos = NL_POS[code], text = "" } } }
    -- The meanings, from the word's entry.
    http.get(WIKW .. "&action=parse&prop=wikitext&page=" .. encode(word:gsub(" ", "_")), function(res2)
      if fetching ~= key then return end
      fetching = nil
      local text = res2.json and res2.json.parse and res2.json.parse.wikitext
      if type(text) == "string" then
        local pages, syll = parseDutch(text, word)
        if pages and #pages > 0 then
          if not pages[1].pos then pages[1].pos = NL_POS[code] end
          e.pages, e.syll = pages, syll
        end
      elseif res2.status == 0 and not (res2.error or ""):find("too large", 1, true) then
        e.stale = true -- the word without its meaning: try again later
      end
      show(e)
      save()
    end)
  end)
end

-- ------------------------------------------------------------------ the card

-- Look up today's word when the day changed, or (again) when `retry` and the
-- word on screen is a stand-in.
local function check(retry)
  local day = today()
  if not day then return false end
  local key = lang .. day
  if fetching == key then return false end
  if entry and entry.key == key and not (retry and entry.stale) then return false end
  if not entry or entry.key ~= key then
    local saved = store.get("word")
    if type(saved) == "table" and saved.key == key and type(saved.pages) == "table" and #saved.pages > 0 then
      show(saved)
      if not (retry and saved.stale) then return true end
    end
  end

  fetching = key
  local function failed()
    if fetching ~= key then return end
    fetching = nil
    if not entry or entry.key ~= key then show(ownWord(day, key)) end
  end
  if lang == "nl" then fetchDutch(day, key, failed) else fetchEnglish(day, key, false, failed) end
  return true
end

function load()
  lang = config.language == "Nederlands" and "nl" or "en"
  if not entry then
    local saved = store.get("word")
    if type(saved) == "table" and saved.lang == lang and type(saved.pages) == "table" and #saved.pages > 0 then
      show(saved) -- yesterday's word is better than nothing while the clock is not set
    end
  end
  check(true)
end

-- Long meanings scroll down, rest, scroll back up, rest. Timed with time.ms():
-- dt is capped and would not add up to the real time.
function update()
  local now = time.ms()
  local elapsed = math.min(now - lastMs, 1000)
  lastMs = now
  local changed = check(false)
  if overflow > 0 and now >= restUntil then
    offset = offset + direction * SPEED * elapsed / 1000
    if offset >= overflow or offset <= 0 then
      offset = math.max(0, math.min(overflow, offset))
      direction, restUntil = -direction, now + PAUSE
    end
    changed = true
  end
  if not changed then return false end
end

function draw()
  local t = words()
  if not entry then
    gfx.text(PAD, 6, t.title, ACCENT, "medium")
    local y = 34
    for _, line in ipairs(gfx.wrap(today() and t.loading or t.waiting, W - 2 * PAD, "small")) do
      gfx.text(PAD, y, line, "gray", "small")
      y = y + gfx.textHeight("small")
    end
    return
  end
  local page = entry.pages[current]

  -- The word, with which page this is in the corner.
  local y = 2
  for _, line in ipairs(wordLines) do
    gfx.text(PAD, y, line, "white", wordFont)
    y = y + gfx.textHeight(wordFont)
  end

  -- What kind of word and its syllables, or what this page is; and which page
  -- this is on the right.
  local right = W - PAD
  local corner = #entry.pages > 1 and (current .. "/" .. #entry.pages) or (entry.offline and t.offline)
  if corner then
    gfx.text(right, y, corner, "gray", "small", "right")
    right = right - gfx.textWidth(corner, "small") - 10
  end
  local x = PAD
  local kind = page.kind == "example" and t.example or page.kind == "note" and t.note or page.pos
  if kind and kind ~= "" then
    gfx.text(x, y, kind, page.kind == "example" and EXAMPLE or page.kind == "note" and NOTE or ACCENT, "small")
    x = x + gfx.textWidth(kind, "small") + 10
  end
  if entry.syll and not page.kind and x + gfx.textWidth(entry.syll, "small") <= right then
    gfx.text(x, y, entry.syll, "gray", "small")
  end

  -- The meaning, example or note.
  local color = page.kind == "example" and EXAMPLE or "white"
  gfx.clip(0, top, W, visible)
  y = top - math.floor(offset)
  for _, line in ipairs(lines) do
    gfx.text(PAD, y, line, color, font)
    y = y + gfx.textHeight(font)
  end
  gfx.clip()
end

-- Centre shows the next meaning; up and down move between cards as usual.
function button(name)
  if name ~= "center" or not entry or #entry.pages < 2 then return false end
  current = current % #entry.pages + 1
  layout()
  return true
end
