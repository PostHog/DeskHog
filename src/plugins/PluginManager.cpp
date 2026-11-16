#include "plugins/PluginManager.h"
#include "SourceHash.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "ConfigManager.h"
#include "EventQueue.h"
#include "bundled_plugins.h"
#include "esp_heap_caps.h"
#include "config/CardConfig.h"
#include "hardware/DisplayInterface.h"
#include "hardware/NeoPixelController.h"
#include "plugins/PluginCard.h"
#include "plugins/PluginConnection.h"

namespace {

/** Holds lock_ for the current scope. */
class Locked {
public:
    explicit Locked(SemaphoreHandle_t lock) : lock_(lock) { xSemaphoreTake(lock_, portMAX_DELAY); }
    ~Locked() { xSemaphoreGive(lock_); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;

private:
    SemaphoreHandle_t lock_;
};

/** Collects a response body, refusing to grow past a cap. */
class CappedSink : public Stream {
public:
    CappedSink(std::string& out, size_t cap) : out_(out), cap_(cap) {}
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        if (out_.size() + size > cap_) {
            overflow_ = true;
            return 0;
        }
        out_.append(reinterpret_cast<const char*>(data), size);
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    bool overflow() const { return overflow_; }

private:
    std::string& out_;
    size_t cap_;
    bool overflow_ = false;
};

bool validName(const String& name) { return deskhog::isValidPluginName(std::string(name.c_str())); }

bool readFile(const String& path, String& out, size_t maxBytes) {
    if (!LittleFS.exists(path)) return false;  // also keeps "does not exist" out of the error log
    File file = LittleFS.open(path, "r");
    if (!file || file.isDirectory()) return false;
    const size_t size = file.size();
    if (size > maxBytes) {
        file.close();
        return false;
    }
    out = "";
    if (!out.reserve(size)) {
        file.close();
        return false;
    }
    uint8_t chunk[256];
    while (file.available()) {
        const size_t got = file.read(chunk, sizeof(chunk));
        if (got == 0) break;
        out.concat(reinterpret_cast<const char*>(chunk), got);
    }
    file.close();
    return out.length() == size;
}

// Write to a temporary file first so a power cut cannot leave half a plugin behind.
bool writeFile(const String& path, const uint8_t* data, size_t length) {
    const String temporary = path + ".tmp";
    File file = LittleFS.open(temporary, "w");
    if (!file) return false;
    const size_t written = length ? file.write(data, length) : 0;
    file.close();
    if (written != length) {
        LittleFS.remove(temporary);
        return false;
    }
    // LittleFS replaces the target in one step, so there is never a moment
    // without a complete file. Only if that fails, fall back to remove + rename.
    if (LittleFS.rename(temporary, path)) return true;
    LittleFS.remove(path);
    if (LittleFS.rename(temporary, path)) return true;
    LittleFS.remove(temporary);
    return false;
}

// Append `value` as a quoted JSON string.
void appendJsonString(std::string& out, const std::string& value) {
    out += '"';
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char escaped[8];
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
}

// ------------------------------------------------------- records on flash

// A connection is saved as one small JSON file:
//   {"plugin":"weather","name":"Weather","next":3,"settings":{"unit":"C"},
//    "cards":[{"id":"1","kind":"city","settings":{"city":"Ghent"}}]}

void settingsToJson(JsonObject target, const PluginSettings& settings) {
    for (const auto& pair : settings) target[pair.first.c_str()] = pair.second.c_str();
}

PluginSettings settingsFromJson(JsonVariantConst source) {
    PluginSettings settings;
    if (!source.is<JsonObjectConst>()) return settings;
    for (JsonPairConst pair : source.as<JsonObjectConst>()) {
        settings[pair.key().c_str()] = pair.value().as<String>().c_str();
    }
    return settings;
}

String recordToJson(const PluginConnectionRecord& record) {
    DynamicJsonDocument doc(8192);
    doc["plugin"] = record.plugin.c_str();
    doc["name"] = record.name.c_str();
    doc["next"] = record.nextCard;
    settingsToJson(doc.createNestedObject("settings"), record.settings);
    JsonArray cards = doc.createNestedArray("cards");
    for (const PluginCardRecord& card : record.cards) {
        JsonObject item = cards.createNestedObject();
        item["id"] = card.id.c_str();
        item["kind"] = card.kind.c_str();
        settingsToJson(item.createNestedObject("settings"), card.settings);
    }
    String text;
    serializeJson(doc, text);
    return text;
}

bool recordFromJson(const String& text, PluginConnectionRecord& record) {
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, text) || !doc.is<JsonObject>()) return false;
    record.plugin = doc["plugin"] | "";
    record.name = doc["name"] | "";
    record.nextCard = doc["next"] | 1;
    record.settings = settingsFromJson(doc["settings"]);
    record.cards.clear();
    for (JsonVariantConst item : doc["cards"].as<JsonArrayConst>()) {
        PluginCardRecord card;
        card.id = item["id"] | "";
        card.kind = item["kind"] | "";
        card.settings = settingsFromJson(item["settings"]);
        if (!card.id.empty()) record.cards.push_back(card);
    }
    return deskhog::isValidPluginName(record.plugin);
}

// Plugin cards in the card stack are stored as "<connection id>:<card id>".
std::string connectionOfKey(const String& key) {
    const int colon = key.indexOf(':');
    return colon > 0 ? std::string(key.substring(0, colon).c_str()) : std::string();
}

}  // namespace

bool PluginManager::splitKey(const std::string& key, std::string& connectionId, std::string& cardId) {
    const size_t colon = key.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= key.size()) return false;
    connectionId = key.substr(0, colon);
    cardId = key.substr(colon + 1);
    return deskhog::isValidPluginName(connectionId) && deskhog::isValidPluginName(cardId);
}

PluginManager::PluginManager(ConfigManager& configManager, NeoPixelController& led, EventQueue& eventQueue)
    : configManager_(configManager), led_(led), eventQueue_(eventQueue) {}

void PluginManager::begin() {
    lock_ = xSemaphoreCreateMutex();
    jobs_ = xQueueCreate(16, sizeof(HttpJob*));
    results_ = xQueueCreate(16, sizeof(HttpResult*));

    // The first boot after flashing formats the partition, which takes a few seconds.
    storageReady_ = LittleFS.begin(true);
    if (!storageReady_) {
        Serial.println("[plugins] Could not mount plugin storage. Plugins are unavailable.");
    } else {
        installBundledPlugins();
        LittleFS.mkdir("/connections");
        LittleFS.mkdir("/data");
        loadConnections();
        migrateOldCards();
        migrateBuiltInPostHog();
        Serial.printf("[plugins] Storage ready: %u of %u bytes used, %u connections\n", (unsigned)LittleFS.usedBytes(),
                      (unsigned)LittleFS.totalBytes(), (unsigned)records_.size());
    }

    // TLS handshakes need a deep stack; a request has been measured at 4.3 KB
    // with verbose logging on. The stack has to be internal RAM: lwIP asserts
    // that a task opening a TCP connection does not run on a PSRAM stack.
    xTaskCreatePinnedToCore(httpTaskEntry, "pluginHttp", 8192, this, 1, &httpTask_, 0);
}

void PluginManager::loadConnections() {
    File dir = LittleFS.open("/connections");
    if (!dir || !dir.isDirectory()) return;
    for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
        String fileName = file.name();
        const int slash = fileName.lastIndexOf('/');
        if (slash >= 0) fileName = fileName.substring(slash + 1);
        const bool isRecord = !file.isDirectory() && fileName.endsWith(".json");
        file.close();
        if (!isRecord) continue;

        PluginConnectionRecord record;
        record.id = fileName.substring(0, fileName.length() - 5).c_str();
        String text;
        if (!deskhog::isValidPluginName(record.id)) continue;
        if (!readFile(connectionPath(record.id), text, 8192) || !recordFromJson(text, record)) continue;
        records_[record.id] = record;
    }
    dir.close();
}

// The plugin that took over from each card type that used to be built into the firmware.
static const char* pluginForOldCardType(const String& type) {
    if (type == "FRIEND") return "friend";
    if (type == "HELLO_WORLD") return "hello";
    if (type == "FLAPPY_HOG") return "flappy";
    if (type == "QUESTION") return "question";
    if (type == "PADDLE") return "paddle";
    return nullptr;
}

// Two kinds of old card are moved to a plugin connection of their own, so they
// keep working: cards that were built into the firmware (Max, the games), and
// plugin cards from early builds, which were stored as just the plugin's name.
void PluginManager::migrateOldCards() {
    std::vector<CardConfig> configs = configManager_.getCardConfigs();
    bool changed = false;
    for (auto it = configs.begin(); it != configs.end();) {
        const char* successor = pluginForOldCardType(it->type);
        if (!successor) {
            ++it;
            continue;
        }
        changed = true;
        // The plugin may not be on the device (plugins come from the directory,
        // not with the firmware). The card is kept either way: it says which
        // plugin it needs, and the web page offers to install it.
        Serial.printf("[plugins] The built-in %s card is now a card of the %s plugin\n", it->type.c_str(),
                      successor);
        it->type = kPluginCardType;
        it->config = successor;
        it->name = successor;  // not a name someone chose: the plugin's own title is shown
        ++it;
    }
    for (CardConfig& config : configs) {
        if (!config.isPlugin() || config.config.indexOf(':') >= 0) continue;
        const std::string plugin = config.config.c_str();
        if (!deskhog::isValidPluginName(plugin)) continue;

        PluginConnectionRecord record;
        record.id = freeConnectionId(plugin);
        record.plugin = plugin;
        record.name = plugin;
        PluginCardRecord card;
        card.id = "1";  // no kind: the plugin's first one
        record.cards.push_back(card);
        record.nextCard = 2;
        if (!saveRecord(record)) continue;
        records_[record.id] = record;
        config.config = String(record.id.c_str()) + ":1";
        changed = true;
    }
    if (changed) configManager_.saveCardConfigs(configs);
}

// The card stack and the connections are saved in two places (settings and
// plugin storage). If they ever disagree, the card stack wins: forget cards
// that are not in it, and connections that only existed for such a card.
void PluginManager::pruneOrphans() {
    const std::vector<CardConfig> configs = configManager_.getCardConfigs();
    Locked locked(lock_);
    for (auto it = records_.begin(); it != records_.end();) {
        PluginConnectionRecord& record = it->second;
        const size_t before = record.cards.size();
        for (auto card = record.cards.begin(); card != record.cards.end();) {
            const String key = String((record.id + ":" + card->id).c_str());
            bool inStack = false;
            for (const CardConfig& config : configs) inStack = inStack || (config.isPlugin() && config.config == key);
            card = inStack ? card + 1 : record.cards.erase(card);
        }
        const auto meta = metas_.find(record.plugin);
        const bool shared = meta != metas_.end() && meta->second.multiCard;
        if (record.cards.empty() && meta != metas_.end() && !shared) {
            deleteRecordFiles(record.id);
            it = records_.erase(it);
            continue;
        }
        if (record.cards.size() != before) saveRecord(record);
        ++it;
    }
}

// PostHog insights used to be a card type built into the firmware, with one
// project and API key saved in the device settings. They are a plugin now.
// Move cards and settings from older firmware over, once, so they keep working.
void PluginManager::migrateBuiltInPostHog() {
    std::vector<CardConfig> configs = configManager_.getCardConfigs();
    bool any = false;
    for (const CardConfig& config : configs) any = any || config.type == "INSIGHT";
    if (!any) return;  // the plugin itself may still have to be installed; the cards wait for it

    PluginConnectionRecord record;
    record.id = freeConnectionId("posthog");
    if (record.id.empty()) return;
    record.plugin = "posthog";
    record.name = "PostHog";
    record.settings["region"] = configManager_.getRegion().c_str();
    if (configManager_.getTeamId() != ConfigManager::NO_TEAM_ID) {
        record.settings["project"] = std::to_string(configManager_.getTeamId());
    }
    if (configManager_.getApiKey().length()) record.settings["key"] = configManager_.getApiKey().c_str();

    for (CardConfig& config : configs) {
        if (config.type != "INSIGHT") continue;
        PluginCardRecord card;
        card.id = std::to_string(record.nextCard++);
        card.kind = "insight";
        card.settings["id"] = config.config.c_str();
        record.cards.push_back(card);
        config.type = kPluginCardType;
        config.config = String((record.id + ":" + card.id).c_str());
        config.name = "Insight";  // the plugin shows the insight's own name
    }
    if (!saveRecord(record)) return;
    records_[record.id] = record;
    configManager_.saveCardConfigs(configs);
    configManager_.clearPostHogSettings();
    Serial.printf("[plugins] Moved %u PostHog insight cards to the PostHog plugin\n", (unsigned)record.cards.size());
}

bool PluginManager::saveRecord(const PluginConnectionRecord& record) {
    const String text = recordToJson(record);
    return writeFile(connectionPath(record.id), reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
}

std::string PluginManager::freeConnectionId(const std::string& plugin) const {
    if (records_.find(plugin) == records_.end()) return plugin;
    for (int n = 2; n < 100; n++) {
        // Ids are at most 32 characters, like plugin names.
        const std::string candidate = plugin.substr(0, 28) + "-" + std::to_string(n);
        if (records_.find(candidate) == records_.end()) return candidate;
    }
    return std::string();
}

void PluginManager::deleteRecordFiles(const std::string& id) {
    LittleFS.remove(connectionPath(id));
    LittleFS.remove(storePath(id));
}

// Bundled plugins are copied to storage once each. The names already offered
// are kept in a list, so a plugin someone deleted stays deleted, while a
// plugin that is new in a firmware update still arrives.
void PluginManager::installBundledPlugins() {
    static const char offeredPath[] = "/plugins/.offered";
    if (!LittleFS.exists("/plugins")) LittleFS.mkdir("/plugins");

    String offered;
    readFile(offeredPath, offered, 4096);
    String updated = offered;
    unsigned installed = 0;
    for (size_t i = 0; i < BUNDLED_PLUGIN_COUNT; i++) {
        const BundledPlugin& plugin = BUNDLED_PLUGINS[i];
        const String line = String(plugin.name) + "\n";
        if ((String("\n") + offered).indexOf(String("\n") + line) >= 0) continue;
        updated += line;
        if (LittleFS.exists(sourcePath(plugin.name))) continue;  // there already: leave it as it is
        if (writeFile(sourcePath(plugin.name), reinterpret_cast<const uint8_t*>(plugin.source), plugin.length)) installed++;
    }
    if (updated != offered) {
        writeFile(offeredPath, reinterpret_cast<const uint8_t*>(updated.c_str()), updated.length());
    }
    if (installed) Serial.printf("[plugins] Installed %u bundled plugins\n", installed);
}

void PluginManager::startNetworkServices() {
    if (networkServicesStarted_) return;
    networkServicesStarted_ = true;
    // So tools can find the device as http://deskhog.local
    if (MDNS.begin("deskhog")) MDNS.addService("http", "tcp", 80);
    // Plugins get the time from time.now(); UTC, no timezone handling yet.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// Run a plugin file just far enough to learn its title, settings and card kinds.
void PluginManager::inspectPlugin(const std::string& name) {
    PluginMeta meta;
    meta.title = name;
    String source;
    if (!readSource(name.c_str(), source)) {
        Locked locked(lock_);
        metas_.erase(name);
        return;
    }
    meta.hash = deskhog::sourceHash(source.c_str(), source.length());

    // Throwaway services: an inspected plugin does not run load(), and whatever
    // its top-level code tries to reach goes nowhere.
    struct NoServices : deskhog::PluginServices {
        uint32_t millis() override { return ::millis(); }
        int64_t epochSeconds() override { return 0; }
        bool buttonHeld(deskhog::Button) override { return false; }
        void ledSet(uint8_t, uint8_t, uint8_t, uint32_t) override {}
        void ledOff() override {}
        bool httpStart(int, const deskhog::HttpRequest&) override { return true; }
        bool storeGet(const std::string&, std::string&) override { return false; }
        bool storeSet(const std::string&, const std::string&) override { return true; }
        void log(const std::string&) override {}
    } services;
    LvglMetrics metrics(cardWidth_, cardHeight_);
    {
        deskhog::LuaPlugin::Memory memory;
        memory.realloc = [](void* ptr, size_t size) { return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); };
        memory.free = [](void* ptr) { heap_caps_free(ptr); };
        deskhog::LuaPlugin probe(metrics, services, deskhog::LuaPlugin::Limits(), memory);
        if (probe.inspect(name, source.c_str(), source.length())) {
            meta.title = probe.title();
            meta.description = probe.description();
            meta.version = probe.version();
            meta.author = probe.author();
            meta.config = probe.configSchema();
            meta.kinds = probe.kinds();
            meta.multiCard = !(meta.kinds.size() == 1 && meta.kinds[0].key == deskhog::LuaPlugin::kMainKind);
        } else {
            meta.error = probe.error();
        }
    }
    Locked locked(lock_);
    metas_[name] = meta;
}

// ------------------------------------------------------------------- cards

PluginCard* PluginManager::createCard(lv_obj_t* parent, const String& key, int width, int height) {
    cardWidth_ = width;
    cardHeight_ = height;

    // Each card needs 63 KB for its pixels, and each connection a Lua heap, all
    // from the 2 MB of PSRAM. Refuse a card rather than run the device out of memory.
    const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (cards_.size() >= MAX_PLUGIN_CARDS || freePsram < 400 * 1024) {
        Serial.printf("[plugins] Not adding the %s card: %u cards, %u KB of PSRAM free\n", key.c_str(),
                      (unsigned)cards_.size(), (unsigned)(freePsram / 1024));
        return nullptr;
    }

    PluginCard* card = new PluginCard(*this, parent, key.c_str(), width, height);
    if (!card->ok()) {
        Serial.printf("[plugins] No memory for the %s card\n", key.c_str());
        delete card;
        return nullptr;
    }

    PluginConnectionRecord record;
    const bool known = connection(card->connectionId(), record);
    PluginConnection* running = runningConnection(card->connectionId());
    if (!running) {
        running_.emplace_back(new PluginConnection(*this, card->connectionId(), nextRuntimeId_++, width, height));
        running = running_.back().get();
        running->start(known ? &record : nullptr);
    }
    running->attach(*card, known ? &record : nullptr);
    cards_.push_back(card);
    return card;
}

void PluginManager::cardDestroyed(PluginCard* card) {
    if (card->connection()) card->connection()->detach(*card);
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        if (*it == card) {
            cards_.erase(it);
            break;
        }
    }
}

PluginConnection* PluginManager::runningConnection(const std::string& id) {
    for (auto& running : running_) {
        if (running->id() == id) return running.get();
    }
    return nullptr;
}

PluginCard* PluginManager::runningCard(const std::string& key) {
    for (PluginCard* card : cards_) {
        if (card->key() == key) return card;
    }
    return nullptr;
}

void PluginManager::tick() {
    // Learn what every installed plugin offers, once, before anything else.
    if (!inspectedAll_) {
        inspectedAll_ = true;
        for (const PluginInfo& info : list()) inspectPlugin(info.name.c_str());
        pruneOrphans();
    }

    // 1. Work the web server asked for.
    std::vector<Command> commands;
    {
        Locked locked(lock_);
        commands.swap(commands_);
    }
    for (const Command& command : commands) runCommand(command);

    // 2. Everything else touches plugins and LVGL, so it holds the LVGL mutex.
    if (!display_ || !display_->takeMutex(pdMS_TO_TICKS(100))) return;
    const uint32_t now = millis();

    // When the network comes up (it has an address, not merely an access
    // point), let plugins that started without it fetch their data.
    if (now - lastWifiCheckMs_ > 500) {
        lastWifiCheckMs_ = now;
        const bool wifiUp = WiFi.status() == WL_CONNECTED;
        if (wifiUp && !wifiWasUp_) {
            for (auto& running : running_) running->lua().refreshNow();
            networkServicesWanted_ = true;
        }
        wifiWasUp_ = wifiUp;
    }

    HttpResult* result = nullptr;
    while (xQueueReceive(results_, &result, 0) == pdTRUE) {
        for (auto& running : running_) {
            if (running->runtimeId() == result->runtimeId) {
                running->lua().httpResponse(result->requestId, result->response);
                break;
            }
        }
        delete result;  // the connection may be gone by now; then the answer is simply dropped
    }

    for (auto it = running_.begin(); it != running_.end();) {
        PluginConnection& running = **it;
        for (PluginCard* card : running.cards()) running.lua().setVisible(card->cardId(), card->onScreen());
        running.lua().tick(now);
        for (PluginCard* card : running.cards()) {
            if (card->onScreen() || card->framesWanted()) card->paintIfNeeded();
        }
        if (running.cleanUp(now)) {
            it = running_.erase(it);
        } else {
            ++it;
        }
    }

    if (now - lastPublishMs_ > 1000) {
        lastPublishMs_ = now;
        publishStates(now);
    }

    // Headroom numbers for the web API, measured here because this is the
    // task whose stack and LVGL use matter.
    if (now - lastMeasureMs_ > 2000) {
        lastMeasureMs_ = now;
        lv_mem_monitor_t monitor;
        lv_mem_monitor(&monitor);
        lvglHeapTotal_ = monitor.total_size;
        lvglHeapPeak_ = monitor.max_used;
        uiStackUnused_ = uxTaskGetStackHighWaterMark(nullptr);
        cardCount_ = cards_.size();
    }

    display_->giveMutex();
}

void PluginManager::publishStates(uint32_t) {
    std::map<std::string, PluginConnectionState> fresh;
    for (auto& running : running_) {
        PluginConnectionState& state = fresh[running->id()];
        state.running = true;
        state.failed = running->lua().failed();
        state.error = running->lua().error();
        state.memory = running->lua().memoryUsed();
        for (PluginCard* card : running->cards()) {
            PluginCardState& cardState = state.cards[card->cardId()];
            cardState.frames = card->frames();
            cardState.onScreen = card->onScreen();
            cardState.title = running->lua().cardTitle(card->cardId());
        }
    }
    Locked locked(lock_);
    states_.swap(fresh);
}

void PluginManager::restart(PluginConnection& running) {
    PluginConnectionRecord record;
    const bool known = connection(running.id(), record);
    running.restart(known ? &record : nullptr);
}

void PluginManager::runCommand(const Command& command) {
    if (command.kind == CommandKind::PluginChanged) {
        // A plugin file was saved. See what it offers now, then restart whatever runs it.
        inspectPlugin(command.target);
        bool anyRecord = false;
        {
            Locked locked(lock_);
            for (const auto& pair : records_) anyRecord = anyRecord || pair.second.plugin == command.target;
        }
        if (!anyRecord && command.button) {
            // Nothing shows this plugin yet: give whoever pushed it a card to look at.
            String error;
            addCard(command.target, "", "", PluginSettings(), "", "", PluginSettings(), error);
            return;
        }
        if (!anyRecord) return;  // installed, but no card asks for it yet
    }

    if (!display_ || !display_->takeMutex(portMAX_DELAY)) return;
    switch (command.kind) {
        case CommandKind::PluginChanged: {
            bool shown = false;
            for (auto& running : running_) {
                if (running->plugin() != command.target) continue;
                restart(*running);
                // Whoever just pushed this plugin wants to look at it.
                if (!shown && showCard_ && !running->cards().empty()) {
                    showCard_(running->cards()[0]->getCard());
                    shown = true;
                }
            }
            break;
        }
        case CommandKind::ConnectionChanged: {
            PluginConnection* running = runningConnection(command.target);
            if (running) restart(*running);
            break;
        }
        case CommandKind::RefreshAll:
            for (auto& running : running_) running->lua().refreshNow();
            break;
        case CommandKind::Press: {
            PluginCard* card = runningCard(command.target);
            if (card) card->handleButtonPress(command.button);
            break;
        }
        case CommandKind::Show: {
            PluginCard* card = runningCard(command.target);
            if (card && showCard_) showCard_(card->getCard());
            break;
        }
    }
    display_->giveMutex();
}

void PluginManager::queue(CommandKind kind, const std::string& target, uint8_t button) {
    Locked locked(lock_);
    commands_.push_back({kind, target, button});
}

// --------------------------------------------------------------------- http

bool PluginManager::enqueueHttp(uint32_t connectionRuntimeId, int requestId, const deskhog::HttpRequest& request) {
    HttpJob* job = new HttpJob{connectionRuntimeId, requestId, request};
    if (xQueueSend(jobs_, &job, 0) != pdTRUE) {
        delete job;
        return false;
    }
    return true;
}

void PluginManager::httpTaskEntry(void* arg) { static_cast<PluginManager*>(arg)->httpLoop(); }

void PluginManager::httpLoop() {
    for (;;) {
        HttpJob* job = nullptr;
        const bool gotJob = xQueueReceive(jobs_, &job, pdMS_TO_TICKS(1000)) == pdTRUE && job;
        if (networkServicesWanted_.exchange(false)) startNetworkServices();
        if (!gotJob) continue;

        HttpResult* result = new HttpResult{job->runtimeId, job->requestId, {}};
        perform(job->request, result->response);
        delete job;

        // The UI task empties this queue every few milliseconds.
        if (xQueueSend(results_, &result, pdMS_TO_TICKS(5000)) != pdTRUE) delete result;
    }
}

void PluginManager::perform(const deskhog::HttpRequest& request, deskhog::HttpResponse& response) {
    if (WiFi.status() != WL_CONNECTED) {
        response.error = "no WiFi";
        return;
    }

    const bool secure = request.url.compare(0, 8, "https://") == 0;
    NetworkClient plainClient;
    NetworkClientSecure secureClient;
    // Certificates are not verified yet (the same holds for the PostHog client).
    // Traffic is encrypted, but the device cannot prove who it is talking to.
    secureClient.setInsecure();
    // Without this a host that stalls mid-handshake would hold up every
    // plugin's requests for two minutes.
    secureClient.setHandshakeTimeout(request.timeoutMs / 1000 + 1);

    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout((int32_t)request.timeoutMs);
    http.setTimeout((uint16_t)(request.timeoutMs > 60000 ? 60000 : request.timeoutMs));
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("DeskHog");

    const String url(request.url.c_str());
    const bool began = secure ? http.begin(secureClient, url) : http.begin(plainClient, url);
    if (!began) {
        response.error = "bad url";
        return;
    }
    for (const auto& header : request.headers) http.addHeader(header.first.c_str(), header.second.c_str());

    int code;
    if (request.method == "POST") {
        code = http.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(request.body.data())), request.body.size());
    } else {
        code = http.GET();
    }
    if (code <= 0) {
        response.error = HTTPClient::errorToString(code).c_str();
        http.end();
        return;
    }

    // An answer with no body (204, or Content-Length: 0) is complete already.
    // Reading it would report "not connected" once the server has hung up.
    const int expected = http.getSize();  // -1 when the server did not say
    if (expected == 0 || code == 204 || code == 304) {
        http.end();
        response.status = code;
        return;
    }

    // How much to accept. A plugin that keeps only part of the answer may ask
    // for more than usual, as long as there is room: the answer sits in PSRAM
    // while it is read, and the buffer can briefly take twice its size as it grows.
    size_t limit = MAX_RESPONSE_BYTES;
    if (request.maxBytes > limit) {
        const size_t room = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 3;
        limit = std::min(std::min(request.maxBytes, MAX_FILTERED_RESPONSE_BYTES), std::max(room, MAX_RESPONSE_BYTES));
    }
    const String tooLarge = String("response too large (max ") + String((unsigned)(limit / 1024)) + " KB)";
    if (expected > 0 && static_cast<size_t>(expected) > limit) {
        http.end();
        response.error = tooLarge.c_str();
        return;
    }
    if (expected > 0) response.body.reserve(static_cast<size_t>(expected));

    CappedSink sink(response.body, limit);
    const int written = http.writeToStream(&sink);
    http.end();
    if (sink.overflow()) {
        response.body.clear();
        response.error = tooLarge.c_str();
        return;
    }
    // With a known length a read error means a truncated body. Without one,
    // the server closing the connection is how the body ends.
    if (written < 0 && expected > 0) {
        response.body.clear();
        response.error = HTTPClient::errorToString(written).c_str();
        return;
    }
    response.status = code;
}

// -------------------------------------------------------- state for the API

void PluginManager::appendLog(const std::string& plugin, const std::string& line) {
    Serial.printf("[plugin:%s] %s\n", plugin.c_str(), line.c_str());
    Locked locked(lock_);
    std::deque<std::string>& lines = logs_[plugin];
    lines.push_back(line);
    while (lines.size() > MAX_LOG_LINES) lines.pop_front();
}

std::vector<String> PluginManager::logLines(const String& plugin) {
    std::vector<String> lines;
    Locked locked(lock_);
    const auto log = logs_.find(plugin.c_str());
    if (log != logs_.end()) {
        for (const std::string& line : log->second) lines.push_back(line.c_str());
    }
    return lines;
}

std::map<std::string, PluginMeta> PluginManager::metas() {
    Locked locked(lock_);
    return metas_;
}

std::map<std::string, PluginConnectionState> PluginManager::states() {
    Locked locked(lock_);
    return states_;
}

PluginSystemInfo PluginManager::systemInfo() {
    PluginSystemInfo info;
    info.heapFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    info.heapLowest = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    info.psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    info.uiStackUnused = uiStackUnused_;
    info.httpStackUnused = httpTask_ ? uxTaskGetStackHighWaterMark(httpTask_) : 0;
    info.lvglHeapTotal = lvglHeapTotal_;
    info.lvglHeapPeak = lvglHeapPeak_;
    info.cards = cardCount_;
    return info;
}

bool PluginManager::copyCardPixels(const std::string& key, std::vector<uint8_t>& out, int& width, int& height,
                                   uint32_t& frames, const uint32_t* have) {
    // The LVGL mutex also guards the card list: cards only come and go under it.
    if (!display_ || !display_->takeMutex(pdMS_TO_TICKS(400))) return false;
    PluginCard* card = runningCard(key);
    const bool found = card && card->gfx().pixels();
    if (found) {
        frames = card->frames();
        if (!have || *have != frames) {
            out.assign(card->gfx().pixels(), card->gfx().pixels() + card->gfx().pixelBytes());
        }
        width = card->gfx().width();
        height = card->gfx().height();
        // Someone is looking at this card in a browser: keep it painted even
        // while it is not the card on the device's screen.
        card->wantFrames(15000);
    }
    display_->giveMutex();
    return found;
}

// ---------------------------------------------------------------- directory

#ifndef PLUGIN_DIRECTORY
#define PLUGIN_DIRECTORY "TimBroddin/DeskHog"
#endif

const char* PluginManager::defaultDirectorySource() { return PLUGIN_DIRECTORY; }

String PluginManager::directorySource() {
    const String saved = configManager_.getPluginDirectory();
    return saved.length() ? saved : String(PLUGIN_DIRECTORY);
}

bool PluginManager::setDirectorySource(const String& source, String& error) {
    if (source.length() > 200) {
        error = "that address is too long (200 characters at most)";
        return false;
    }
    for (size_t i = 0; i < source.length(); i++) {
        if (source[i] <= ' ' || source[i] == '"' || source[i] == '<' || source[i] == '>' || source[i] == 0x7F) {
            error = "that does not look like \"owner/repo\" or a web address";
            return false;
        }
    }
    configManager_.setPluginDirectory(source);
    return true;
}

bool PluginManager::press(const std::string& key, uint8_t button) {
    if (button > 2) return false;
    queue(CommandKind::Press, key, button);
    return true;
}

bool PluginManager::show(const std::string& key) {
    queue(CommandKind::Show, key);
    return true;
}

std::string PluginManager::firstCardOf(const std::string& plugin) {
    Locked locked(lock_);
    for (const auto& pair : records_) {
        if (pair.second.plugin == plugin && !pair.second.cards.empty()) {
            return pair.first + ":" + pair.second.cards[0].id;
        }
    }
    return std::string();
}

// ------------------------------------------------------ connections, cards

std::vector<PluginConnectionRecord> PluginManager::connections() {
    std::vector<PluginConnectionRecord> all;
    Locked locked(lock_);
    for (const auto& pair : records_) all.push_back(pair.second);
    return all;
}

bool PluginManager::connection(const std::string& id, PluginConnectionRecord& out) {
    Locked locked(lock_);
    const auto found = records_.find(id);
    if (found == records_.end()) return false;
    out = found->second;
    return true;
}

std::string PluginManager::createConnection(const std::string& plugin, const std::string& name,
                                            const PluginSettings& settings, String& error) {
    if (!storageReady_) {
        error = "plugin storage is not available";
        return std::string();
    }
    Locked locked(lock_);
    const auto meta = metas_.find(plugin);
    if (meta == metas_.end()) {
        error = String("no plugin named ") + plugin.c_str();
        return std::string();
    }
    if (records_.size() >= MAX_CONNECTIONS) {
        error = "too many connections";
        return std::string();
    }
    PluginConnectionRecord record;
    record.id = freeConnectionId(plugin);
    record.plugin = plugin;
    record.name = name.empty() ? meta->second.title : name;
    record.settings = settings;
    if (record.id.empty() || !saveRecord(record)) {
        error = "could not save the connection";
        return std::string();
    }
    records_[record.id] = record;
    return record.id;
}

bool PluginManager::updateConnection(const std::string& id, const std::string* name, const PluginSettings& settings) {
    {
        Locked locked(lock_);
        const auto found = records_.find(id);
        if (found == records_.end()) return false;
        PluginConnectionRecord& record = found->second;
        if (name) record.name = *name;
        for (const auto& pair : settings) record.settings[pair.first] = pair.second;
        if (!saveRecord(record)) return false;
    }
    if (!settings.empty()) queue(CommandKind::ConnectionChanged, id);  // settings apply on a restart
    return true;
}

bool PluginManager::removeConnection(const std::string& id) {
    {
        Locked locked(lock_);
        if (records_.erase(id) == 0) return false;
        deleteRecordFiles(id);
    }
    // Take its cards out of the stack. The running program ends once they are gone.
    const std::vector<CardConfig> configs = configManager_.getCardConfigs();
    std::vector<CardConfig> kept;
    for (const CardConfig& config : configs) {
        if (config.isPlugin() && connectionOfKey(config.config) == id) continue;
        kept.push_back(config);
    }
    if (kept.size() != configs.size()) configManager_.saveCardConfigs(kept);
    return true;
}

std::string PluginManager::addCard(const std::string& plugin, const std::string& connectionId,
                                   const std::string& connectionName, const PluginSettings& connectionSettings,
                                   const std::string& kind, const std::string& cardName,
                                   const PluginSettings& cardSettings, String& error) {
    if (!storageReady_) {
        error = "plugin storage is not available";
        return std::string();
    }
    std::vector<CardConfig> configs = configManager_.getCardConfigs();
    size_t pluginCards = 0;
    int lastOrder = -1;
    for (const CardConfig& config : configs) {
        if (config.isPlugin()) pluginCards++;
        if (config.order > lastOrder) lastOrder = config.order;
    }
    if (pluginCards >= MAX_PLUGIN_CARDS) {
        error = "the device shows at most 12 plugin cards";
        return std::string();
    }

    std::string key;
    std::string defaultName;
    {
        Locked locked(lock_);
        const auto meta = metas_.find(plugin);
        if (meta == metas_.end()) {
            error = String("no plugin named ") + plugin.c_str();
            return std::string();
        }
        if (!meta->second.error.empty()) {
            error = String("the plugin does not load: ") + meta->second.error.c_str();
            return std::string();
        }
        std::string cardKind = kind;
        std::string kindName;
        for (const auto& candidate : meta->second.kinds) {
            if (cardKind.empty()) cardKind = candidate.key;
            if (candidate.key == cardKind) kindName = candidate.name;
        }
        if (kindName.empty()) {
            error = String("the plugin has no card kind ") + kind.c_str();
            return std::string();
        }

        // Cards of a plugin with card kinds share a connection. A single-card
        // plugin gets a connection per card, so two copies do not share state.
        PluginConnectionRecord* record = nullptr;
        PluginConnectionRecord fresh;
        if (meta->second.multiCard && !connectionId.empty()) {
            const auto found = records_.find(connectionId);
            if (found == records_.end() || found->second.plugin != plugin) {
                error = "no such connection";
                return std::string();
            }
            record = &found->second;
        } else {
            if (records_.size() >= MAX_CONNECTIONS) {
                error = "too many connections";
                return std::string();
            }
            fresh.id = freeConnectionId(plugin);
            fresh.plugin = plugin;
            fresh.name = connectionName.empty() ? meta->second.title : connectionName;
            fresh.settings = connectionSettings;
            if (fresh.id.empty()) {
                error = "could not create the connection";
                return std::string();
            }
            record = &fresh;
        }

        PluginCardRecord card;
        card.id = std::to_string(record->nextCard++);
        card.kind = cardKind;
        card.settings = cardSettings;
        record->cards.push_back(card);
        if (!saveRecord(*record)) {
            error = "could not save (storage full?)";
            if (record != &fresh) {
                record->cards.pop_back();
                record->nextCard--;
            }
            return std::string();
        }
        if (record == &fresh) records_[fresh.id] = fresh;
        key = record->id + ":" + card.id;
        defaultName = meta->second.multiCard ? kindName : meta->second.title;
    }

    configs.push_back(CardConfig(kPluginCardType, key.c_str(), lastOrder + 1,
                                 cardName.empty() ? defaultName.c_str() : cardName.c_str()));
    configManager_.saveCardConfigs(configs);  // the card controller rebuilds the stack
    return key;
}

bool PluginManager::updateCard(const std::string& key, const std::string* cardName, const PluginSettings& cardSettings) {
    std::string connectionId, cardId;
    if (!splitKey(key, connectionId, cardId)) return false;
    {
        Locked locked(lock_);
        const auto found = records_.find(connectionId);
        if (found == records_.end()) return false;
        PluginCardRecord* card = nullptr;
        for (PluginCardRecord& candidate : found->second.cards) {
            if (candidate.id == cardId) card = &candidate;
        }
        if (!card) return false;
        if (!cardSettings.empty()) {
            for (const auto& pair : cardSettings) card->settings[pair.first] = pair.second;
            if (!saveRecord(found->second)) return false;
        }
    }
    if (!cardSettings.empty()) queue(CommandKind::ConnectionChanged, connectionId);

    if (cardName) {
        std::vector<CardConfig> configs = configManager_.getCardConfigs();
        bool changed = false;
        for (CardConfig& config : configs) {
            if (config.isPlugin() && key == config.config.c_str() && config.name != cardName->c_str()) {
                config.name = cardName->c_str();
                changed = true;
            }
        }
        if (changed) configManager_.saveCardConfigs(configs);
    }
    return true;
}

std::vector<CardConfig> PluginManager::cardStack() {
    std::vector<CardConfig> configs = configManager_.getCardConfigs();
    std::sort(configs.begin(), configs.end(), [](const CardConfig& a, const CardConfig& b) { return a.order < b.order; });
    return configs;
}

bool PluginManager::saveLayout(const std::vector<std::string>& keys) {
    const std::vector<CardConfig> configs = configManager_.getCardConfigs();
    std::vector<bool> used(configs.size(), false);
    std::vector<CardConfig> layout;
    for (const std::string& key : keys) {
        for (size_t i = 0; i < configs.size(); i++) {
            if (used[i]) continue;
            const std::string candidate = std::string(configs[i].type.c_str()) + "|" + configs[i].config.c_str();
            if (candidate != key) continue;
            used[i] = true;
            CardConfig config = configs[i];
            config.order = static_cast<int>(layout.size());
            layout.push_back(config);
            break;
        }
    }

    // Plugin cards that were left out are deleted for good.
    {
        Locked locked(lock_);
        for (size_t i = 0; i < configs.size(); i++) {
            if (used[i] || !configs[i].isPlugin()) continue;
            std::string connectionId, cardId;
            if (!splitKey(configs[i].config.c_str(), connectionId, cardId)) continue;
            const auto found = records_.find(connectionId);
            if (found == records_.end()) continue;
            PluginConnectionRecord& record = found->second;
            for (auto card = record.cards.begin(); card != record.cards.end(); ++card) {
                if (card->id == cardId) {
                    record.cards.erase(card);
                    break;
                }
            }
            // A connection with shared settings stays, ready for new cards. One
            // that only existed for this card goes with it.
            const auto meta = metas_.find(record.plugin);
            const bool keep = meta != metas_.end() && meta->second.multiCard;
            if (record.cards.empty() && !keep) {
                deleteRecordFiles(connectionId);
                records_.erase(found);
            } else {
                saveRecord(record);
            }
        }
    }
    return configManager_.saveCardConfigs(layout);
}

// ------------------------------------------------------------- plugin files

std::vector<PluginInfo> PluginManager::list() {
    std::vector<PluginInfo> plugins;
    if (!storageReady_) return plugins;
    File dir = LittleFS.open("/plugins");
    if (!dir || !dir.isDirectory()) return plugins;
    for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
        String fileName = file.name();
        const int slash = fileName.lastIndexOf('/');
        if (slash >= 0) fileName = fileName.substring(slash + 1);
        if (!file.isDirectory() && fileName.endsWith(".lua")) {
            PluginInfo info;
            info.name = fileName.substring(0, fileName.length() - 4);
            info.size = file.size();
            if (validName(info.name)) plugins.push_back(info);
        }
        file.close();
    }
    dir.close();
    return plugins;
}

bool PluginManager::exists(const String& name) {
    return storageReady_ && validName(name) && LittleFS.exists(sourcePath(name));
}

bool PluginManager::readSource(const String& name, String& out) {
    if (!exists(name)) return false;
    return readFile(sourcePath(name), out, MAX_SOURCE_BYTES);
}

bool PluginManager::writeSource(const String& name, const uint8_t* data, size_t length, String& error, bool addFirstCard) {
    if (!storageReady_) {
        error = "plugin storage is not available";
        return false;
    }
    if (!validName(name)) {
        error = "plugin names are 1-32 characters: a-z, 0-9, - and _";
        return false;
    }
    if (length == 0) {
        error = "the plugin is empty";
        return false;
    }
    if (length > MAX_SOURCE_BYTES) {
        error = "the plugin is larger than 64 KB";
        return false;
    }
    if (!writeFile(sourcePath(name), data, length)) {
        error = "could not write the file (storage full?)";
        return false;
    }
    {
        Locked locked(lock_);
        logs_.erase(name.c_str());
    }
    queue(CommandKind::PluginChanged, name.c_str(), addFirstCard ? 1 : 0);
    return true;
}

bool PluginManager::remove(const String& name) {
    if (!exists(name)) return false;
    LittleFS.remove(sourcePath(name));

    std::vector<std::string> gone;
    {
        Locked locked(lock_);
        for (auto it = records_.begin(); it != records_.end();) {
            if (it->second.plugin == name.c_str()) {
                gone.push_back(it->first);
                deleteRecordFiles(it->first);
                it = records_.erase(it);
            } else {
                ++it;
            }
        }
        metas_.erase(name.c_str());
        logs_.erase(name.c_str());
    }

    const std::vector<CardConfig> configs = configManager_.getCardConfigs();
    std::vector<CardConfig> kept;
    for (const CardConfig& config : configs) {
        bool drop = false;
        if (config.isPlugin()) {
            const std::string owner = connectionOfKey(config.config);
            for (const std::string& id : gone) drop = drop || id == owner;
        }
        if (!drop) kept.push_back(config);
    }
    if (kept.size() != configs.size()) configManager_.saveCardConfigs(kept);
    return true;
}

// A connection's store is one JSON object on disk. In memory each value stays
// as the JSON text the plugin gave us.
std::map<std::string, std::string> PluginManager::loadStore(const std::string& connectionId) {
    std::map<std::string, std::string> values;
    String text;
    // Twice the write cap: escaping and separators add a little on disk.
    if (!storageReady_ || !deskhog::isValidPluginName(connectionId)) return values;
    if (!readFile(storePath(connectionId), text, 2 * MAX_STORE_BYTES)) return values;
    DynamicJsonDocument doc(4 * MAX_STORE_BYTES);
    if (deserializeJson(doc, text) || !doc.is<JsonObject>()) return values;
    for (JsonPair pair : doc.as<JsonObject>()) {
        String value;
        serializeJson(pair.value(), value);
        values[pair.key().c_str()] = value.c_str();
    }
    return values;
}

bool PluginManager::saveStore(const std::string& connectionId, const std::map<std::string, std::string>& values) {
    if (!storageReady_ || !deskhog::isValidPluginName(connectionId)) return false;
    std::string text = "{";
    bool first = true;
    for (const auto& pair : values) {
        if (!first) text += ",";
        first = false;
        appendJsonString(text, pair.first);
        text += ":";
        text += pair.second;
    }
    text += "}";
    return writeFile(storePath(connectionId), reinterpret_cast<const uint8_t*>(text.data()), text.size());
}

void PluginManager::storageInfo(size_t& usedBytes, size_t& totalBytes) {
    usedBytes = storageReady_ ? LittleFS.usedBytes() : 0;
    totalBytes = storageReady_ ? LittleFS.totalBytes() : 0;
}
