#include "plugins/PluginConnection.h"

#include <time.h>

#include <WiFi.h>

#include "esp_heap_caps.h"
#include "esp_random.h"
#include "hardware/NeoPixelController.h"
#include "plugins/PluginCard.h"
#include "plugins/PluginManager.h"

namespace {

// The Lua heap lives in PSRAM. Plain malloc would put the many small blocks in
// internal RAM, which WiFi and TLS need.
void* luaRealloc(void* ptr, size_t size) {
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void luaFree(void* ptr) { heap_caps_free(ptr); }

deskhog::LuaPlugin::Memory luaMemory() {
    deskhog::LuaPlugin::Memory memory;
    memory.realloc = luaRealloc;
    memory.free = luaFree;
    return memory;
}

// The card stack is rebuilt from scratch whenever its configuration changes:
// every card is deleted and created again. A connection waits this long before
// it forgets a card (or itself), so a rebuild does not restart running plugins.
const uint32_t kGraceMs = 3000;

}  // namespace

PluginConnection::PluginConnection(PluginManager& manager, const std::string& id, uint32_t runtimeId, int cardWidth,
                                   int cardHeight)
    : manager_(manager),
      id_(id),
      runtimeId_(runtimeId),
      metrics_(cardWidth, cardHeight),
      lua_(metrics_, *this, deskhog::LuaPlugin::Limits(), luaMemory()) {}

// How long a connection that was started only to list choices stays around.
static const uint32_t kKeepForChoicesMs = 30000;

PluginConnection::~PluginConnection() {
    releaseLed();
    // The program is about to go, and it tells nobody (see ~LuaPlugin).
    for (const auto& pair : choiceRequests_) {
        manager_.finishChoices(pair.second, std::vector<deskhog::Choice>(), "the connection was closed");
    }
}

void PluginConnection::start(const PluginConnectionRecord* record) {
    releaseLed();  // a restart begins with the LED back to normal
    store_ = manager_.loadStore(id_);
    detachedAtMs_.clear();
    plugin_ = record ? record->plugin : id_;

    String source;
    if (!record) {
        // Run a one-line stand-in, so the card explains itself instead of staying black.
        static const char missing[] = "error('this card belongs to a connection that no longer exists', 0)";
        lua_.start(plugin_, missing, sizeof(missing) - 1);
    } else if (!manager_.readSource(plugin_.c_str(), source)) {
        const std::string missing = "error('the plugin \"" + plugin_ +
                                    "\" is not installed. Open the web page of this device to install it.', 0)";
        lua_.start(plugin_, missing.data(), missing.size());
    } else {
        lua_.start(plugin_, source.c_str(), source.length(), record->settings);
    }
}

void PluginConnection::restart(const PluginConnectionRecord* record) {
    start(record);
    for (PluginCard* card : cards_) {
        card->resetFrames();
        addToProgram(*card, record);
    }
}

void PluginConnection::attach(PluginCard& card, const PluginConnectionRecord* record) {
    card.setConnection(this);
    cards_.push_back(&card);
    emptySinceMs_ = 0;
    detachedAtMs_.erase(card.cardId());
    // After a rebuild of the card stack the program still knows this card.
    if (lua_.attachCard(card.cardId(), card.gfx())) return;
    addToProgram(card, record);
}

void PluginConnection::addToProgram(PluginCard& card, const PluginConnectionRecord* record) {
    const PluginCardRecord* saved = record ? record->card(card.cardId()) : nullptr;
    std::string kind = saved ? saved->kind : "";
    // No kind on record: the plugin's first one. That is "main" for a single-card plugin.
    if (kind.empty() && !lua_.kinds().empty()) kind = lua_.kinds()[0].key;
    lua_.addCard(card.cardId(), kind, saved ? saved->settings : PluginSettings(), card.gfx());
}

void PluginConnection::detach(PluginCard& card) {
    lua_.detachCard(card.cardId());
    detachedAtMs_[card.cardId()] = ::millis();
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        if (*it == &card) {
            cards_.erase(it);
            break;
        }
    }
    if (cards_.empty()) emptySinceMs_ = ::millis();
    card.setConnection(nullptr);
}

bool PluginConnection::cleanUp(uint32_t nowMs) {
    for (auto it = detachedAtMs_.begin(); it != detachedAtMs_.end();) {
        if (nowMs - it->second > kGraceMs) {
            lua_.removeCard(it->first);
            it = detachedAtMs_.erase(it);
        } else {
            ++it;
        }
    }
    if (!cards_.empty()) return false;
    if (!choiceRequests_.empty()) return false;
    if (kept_ && static_cast<int32_t>(nowMs - keepUntilMs_) < 0) return false;
    return nowMs - emptySinceMs_ > kGraceMs;
}

void PluginConnection::askChoices(const std::string& function, const std::string& job,
                                  const deskhog::LuaPlugin::ChoiceContext& context) {
    kept_ = true;
    keepUntilMs_ = ::millis() + kKeepForChoicesMs;
    const int requestId = nextChoiceRequest_++;
    choiceRequests_[requestId] = job;
    lua_.choices(function, requestId, context);  // may answer before it returns
}

void PluginConnection::releaseLed() {
    if (!ledHeld_) return;
    ledHeld_ = false;
    manager_.led().clearOverride();
}

// ----------------------------------------------------------------- services

uint32_t PluginConnection::millis() { return ::millis(); }

int64_t PluginConnection::epochSeconds() {
    const time_t now = time(nullptr);
    // Before the clock is set the device counts from 1970.
    return now > 1600000000 ? static_cast<int64_t>(now) : 0;
}

bool PluginConnection::buttonHeld(deskhog::Button button) { return manager_.buttonHeld(static_cast<uint8_t>(button)); }

void PluginConnection::ledSet(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) {
    manager_.led().setOverride(r, g, b, durationMs);
    ledHeld_ = durationMs == 0;
}

void PluginConnection::ledOff() {
    manager_.led().clearOverride();
    ledHeld_ = false;
}

bool PluginConnection::httpStart(int requestId, const deskhog::HttpRequest& request) {
    return manager_.enqueueHttp(runtimeId_, requestId, request);
}

bool PluginConnection::storeGet(const std::string& key, std::string& json) {
    const auto it = store_.find(key);
    if (it == store_.end()) return false;
    json = it->second;
    return true;
}

bool PluginConnection::storeSet(const std::string& key, const std::string& json) {
    const auto it = store_.find(key);
    if (it != store_.end() && it->second == json) return true;  // unchanged: spare the flash
    if (it == store_.end() && store_.size() >= PluginManager::MAX_STORE_KEYS) return false;
    if (key.size() > 64 || json.size() > 2048) return false;

    // The whole store is one small file. Refuse a write that would outgrow
    // what loadStore() is able to read back.
    size_t total = key.size() + json.size() + 8;
    for (const auto& pair : store_) {
        if (pair.first != key) total += pair.first.size() + pair.second.size() + 8;
    }
    if (total > PluginManager::MAX_STORE_BYTES) return false;

    store_[key] = json;
    return manager_.saveStore(id_, store_);
}

void PluginConnection::log(const std::string& line) { manager_.appendLog(plugin_, line); }

std::string PluginConnection::timeZone() { return manager_.timeZone(); }

deskhog::NetStatus PluginConnection::netStatus() {
    deskhog::NetStatus status;
    status.online = WiFi.status() == WL_CONNECTED;
    if (status.online) {
        status.rssi = WiFi.RSSI();
        status.ssid = WiFi.SSID().c_str();
        status.ip = WiFi.localIP().toString().c_str();
    }
    return status;
}

bool PluginConnection::battery(deskhog::BatteryStatus& status) { return manager_.battery(status); }

std::string PluginConnection::deviceName() { return manager_.deviceName(); }

// The radio's noise makes these numbers; before WiFi is up they are weaker,
// but still differ from boot to boot.
uint32_t PluginConnection::entropy() { return esp_random(); }

void PluginConnection::notify(const std::string& cardId, const deskhog::Notice& notice) {
    std::string card = cardId;
    if (card.empty()) {
        // A single-card plugin: its only card. The plugin may ask while it
        // starts, before the card is attached; then the saved record knows.
        if (!cards_.empty()) {
            card = cards_[0]->cardId();
        } else {
            PluginConnectionRecord record;
            if (manager_.connection(id_, record) && !record.cards.empty()) card = record.cards[0].id;
        }
    }
    if (card.empty()) return;
    manager_.requestAttention(id_ + ":" + card, notice);
}

void PluginConnection::choicesDone(int requestId, const std::vector<deskhog::Choice>& choices, const std::string& error) {
    const auto found = choiceRequests_.find(requestId);
    if (found == choiceRequests_.end()) return;
    const std::string job = found->second;
    choiceRequests_.erase(found);
    manager_.finishChoices(job, choices, error);
}
