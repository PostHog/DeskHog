-- GitHub on your desk: how many pull requests are open, and whether the main
-- branch is passing. Add a card per repository. The LED turns red while a
-- build is failing.
--
-- Without a token GitHub allows about 60 requests an hour, which is enough for
-- a few public repositories. With a token (a fine-grained one with read access
-- to Actions and Pull requests is plenty) private repositories work too.

plugin = {
  name = "GitHub",
  description = "Open pull requests and whether the build passes, per repository. Red LED on failure",
  author = "DeskHog",
  version = "1.0",
  refresh = 300,
}

-- Shared by all of this connection's cards.
config = {
  token = { label = "Access token (optional)", default = "", secret = true, order = 1 },
  light = { label = "Turn the LED red while a build is failing", default = true, order = 2 },
}

local failing = {} -- card id -> true, for the LED

local function headers()
  local list = { Accept = "application/vnd.github+json" }
  if config.token ~= "" then list.Authorization = "Bearer " .. config.token end
  return list
end

local function showLed()
  if not config.light then return end
  if next(failing) then led.set("red") else led.off() end
end

local function explain(res)
  if res.status == 404 then return "No such repository. A private one needs a token" end
  if res.status == 401 then return "GitHub refused the token" end
  if res.status == 403 or res.status == 429 then return "Too many requests. A token allows more" end
  return res.error or ("GitHub answered " .. res.status)
end

cards = {
  repo = {
    name = "Repository",
    config = {
      repo = { label = "Repository, as owner/name", default = "", order = 1 },
      branch = { label = "Branch to watch", default = "main", order = 2 },
    },

    load = function(card)
      local repo = card.config.repo:match("^%s*([%w%-_.]+/[%w%-_.]+)%s*$")
      if not repo then
        card.problem = "Give the repository as owner/name on the web page"
        return
      end
      card.title = repo:match("/(.+)$")

      -- The newest runs on the branch. Those for its latest commit say how the build is doing.
      local runs = "https://api.github.com/repos/" .. repo .. "/actions/runs?per_page=10&exclude_pull_requests=true&branch="
        .. card.config.branch
      local wanted = { workflow_runs = { { name = true, status = true, conclusion = true, head_sha = true } } }
      http.get(runs, { headers = headers(), only = wanted }, function(res)
        local list = res.json and res.json.workflow_runs
        if not res.ok or not list then
          card.problem = explain(res)
          return
        end
        card.problem = nil
        card.build, card.broken = "no builds", nil
        if list[1] then
          local building, passed = false, false
          for _, run in ipairs(list) do
            if run.head_sha == list[1].head_sha then
              if run.status ~= "completed" then
                building = true
              elseif run.conclusion == "failure" or run.conclusion == "timed_out" then
                card.broken = card.broken or run.name
              elseif run.conclusion == "success" then
                passed = true
              end
            end
          end
          card.build = card.broken and "failing" or building and "building" or passed and "passing" or "no result"
        end
        failing[card.id] = card.broken and true or nil
        showLed()
      end)

      local search = "https://api.github.com/search/issues?per_page=1&q=repo:" .. repo .. "+type:pr+state:open"
      http.get(search, { headers = headers(), only = { total_count = true } }, function(res)
        if res.ok and res.json then card.pulls = res.json.total_count end
      end)
    end,

    draw = function(card)
      ui.title(card.title or "GitHub")
      if card.problem then
        ui.label(card.problem, "orange")
        return
      end
      ui.big(card.pulls and tostring(card.pulls) or "...")
      ui.label(card.pulls == 1 and "open pull request" or "open pull requests")
      if card.build then
        local color = card.build == "failing" and "red" or card.build == "passing" and "green" or "yellow"
        local what = card.build == "failing" and "failing: " .. card.broken or card.build
        ui.label(card.config.branch .. " is " .. what, color)
      end
    end,

    -- Centre looks again right now.
    button = function(card, name)
      if name ~= "center" then return false end
      cards.repo.load(card)
      return true
    end,
  },
}
