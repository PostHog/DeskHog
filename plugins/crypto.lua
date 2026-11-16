-- Coin prices with the last day as a chart. Add a card per coin; they share
-- the currency. From CoinGecko's free API, which needs no key.
--
-- A coin is named by its id at coingecko.com: bitcoin, ethereum, solana,
-- dogecoin and so on (the last part of the coin's address there).

plugin = {
  name = "Crypto prices",
  description = "The price of a coin, how it moved today, and the last 24 hours as a chart",
  author = "Tim Broddin",
  version = "1.1",
  refresh = 300,
}

-- Shared by all of this connection's cards.
config = {
  currency = { label = "Currency", default = "eur", options = { "eur", "usd", "gbp" } },
}

-- 58910.3 becomes "58,910"; 0.2134 stays "0.2134".
local function money(value)
  if value < 1 then return string.format("%.4f", value) end
  if value < 100 then return string.format("%.2f", value) end
  local text = string.format("%d", math.floor(value + 0.5))
  local grouped = text:reverse():gsub("(%d%d%d)", "%1,"):reverse()
  return (grouped:gsub("^,", ""))
end

cards = {
  coin = {
    name = "Coin",
    config = {
      coin = { label = "Coin, by its CoinGecko id", default = "bitcoin" },
    },

    load = function(card)
      local coin = card.config.coin:lower():match("^%s*([%w%-]+)%s*$") or ""
      card.name = coin:sub(1, 1):upper() .. coin:sub(2)
      card.title = card.name
      local url = "https://api.coingecko.com/api/v3/coins/" .. coin .. "/market_chart?days=1&vs_currency=" .. config.currency
      http.get(url, { only = { prices = true } }, function(res)
        local prices = res.json and res.json.prices
        if not res.ok or not prices or #prices < 2 then
          if not card.price then
            card.problem = res.status == 404 and "CoinGecko has no coin called " .. coin
              or res.status == 429 and "CoinGecko is busy. Trying again in a few minutes"
              or res.error or ("CoinGecko answered " .. res.status)
          end
          return
        end
        card.problem = nil
        -- Each entry is a time and a price. Sixty points are plenty for the chart.
        card.chart = {}
        local step = math.max(1, #prices // 60)
        for i = 1, #prices, step do card.chart[#card.chart + 1] = prices[i][2] end
        card.price = prices[#prices][2]
        card.chart[#card.chart + 1] = card.price
        card.change = (card.price - prices[1][2]) / prices[1][2] * 100
      end)
    end,

    draw = function(card)
      ui.title(card.name or "Crypto")
      if card.problem then
        ui.label(card.problem, "orange")
        return
      end
      if not card.price then
        ui.label("Asking for the price...")
        return
      end
      local up = card.change >= 0
      ui.big(money(card.price))
      ui.label(string.format("%s, %s%.1f%% in a day", config.currency:upper(), up and "+" or "", card.change), up and "green" or "red")
      ui.chart(card.chart, up and "green" or "red")
    end,
  },
}
