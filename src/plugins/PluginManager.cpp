#include "plugins/PluginManager.h"
#include "SourceHash.h"
#include "TimeZone.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>

#include "ConfigManager.h"
#include "DeskManners.h"
#include "EventQueue.h"
#include "HttpRedirect.h"
#include "bundled_plugins.h"
#include "esp_heap_caps.h"
#include "config/CardConfig.h"
#include "hardware/DisplayInterface.h"
#include "hardware/Input.h"
#include "hardware/NeoPixelController.h"
#include "plugins/ImageDecode.h"
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

// The certificates of the authorities the internet runs on, which come with
// the ESP-IDF libraries. https requests are checked against them.
extern const uint8_t caBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t caBundleEnd[] asm("_binary_x509_crt_bundle_end");

// How much of an answer's headers is kept for the plugin.
const size_t kMaxHeaderBytes = 4096;

/**
 * A network client that also remembers the headers of the answer. HTTPClient
 * only keeps headers it was told about in advance; plugins get them all
 * (res.headers). It reads the header lines one byte at a time through read()
 * and the body in blocks, so read() sees exactly the headers.
 */
template <class Base>
class HeaderTap : public Base {
public:
    using Base::connect;

    /**
     * HTTPClient connects through this one, also for each redirect. It reports
     * every failure to connect as "connection refused"; the real reason is kept
     * here (see failure()).
     */
    int connect(const char* host, uint16_t port, int32_t timeout) override {
        failure_.clear();
        IPAddress address;
        // hostByName() answers 1 when it found the address and an error number,
        // not 0, when it did not. NetworkClientSecure tests it for 0 and would
        // go on to connect to 0.0.0.0 until the timeout.
        if (Network.hostByName(host, address) != 1) {
            failure_ = "could not resolve host";
            return 0;
        }
        const uint32_t startedMs = millis();
        const int connected = Base::connect(host, port, timeout);
        if (!connected && timeout > 0 && millis() - startedMs >= static_cast<uint32_t>(timeout)) {
            failure_ = "connection timed out";
        }
        return connected;
    }

    /** Why the last connect() failed, or empty if this client cannot tell. */
    const std::string& failure() const { return failure_; }

    size_t write(const uint8_t* buffer, size_t size) override {
        // A request goes out: what comes back next is a new set of headers.
        // (After a redirect that is the answer that counts.)
        headers_.clear();
        open_ = true;
        return Base::write(buffer, size);
    }

    int read() override {
        const int c = Base::read();
        if (open_ && c >= 0) {
            headers_ += static_cast<char>(c);
            const size_t size = headers_.size();
            const bool blankLine = size >= 4 && headers_.compare(size - 4, 4, "\r\n\r\n") == 0;
            if (blankLine || size >= kMaxHeaderBytes) open_ = false;
        }
        return c;
    }

    /** Split what was read into name and value pairs. The status line is skipped. */
    void headersInto(std::vector<std::pair<std::string, std::string>>& out) const {
        size_t at = headers_.find('\n');
        while (at != std::string::npos && at + 1 < headers_.size()) {
            const size_t start = at + 1;
            at = headers_.find('\n', start);
            if (at == std::string::npos) break;  // a line that was cut off
            std::string line = headers_.substr(start, at - start);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t colon = line.find(':');
            if (colon == std::string::npos || colon == 0) continue;
            size_t value = colon + 1;
            while (value < line.size() && line[value] == ' ') value++;
            out.emplace_back(line.substr(0, colon), line.substr(value));
        }
    }

protected:
    std::string failure_;

private:
    std::string headers_;
    bool open_ = false;
};

/**
 * The https client. It checks the server's certificate against the bundle on
 * every connection, and knows why a connection failed.
 *
 * Two things in NetworkClientSecure (arduino-esp32 3.2.0) make this necessary:
 *  - stop() wipes the whole TLS context, including the bundle that
 *    setCACertBundle() attached. A client is stopped before it connects to
 *    where a redirect points, so that second connection had no authorities to
 *    check against and failed with MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED: every
 *    https address that redirects was "refused". The bundle is attached again
 *    before each connect.
 *  - A failed connect() calls stop() before it returns, which throws away what
 *    the certificate check found. stop() is where it can still be read.
 */
class SecureClient : public HeaderTap<NetworkClientSecure> {
public:
    using HeaderTap<NetworkClientSecure>::connect;

    /** insecure: accept any certificate (the plugin asked for it). */
    void checkCertificates(bool check) {
        check_ = check;
        if (!check) setInsecure();
    }

    int connect(const char* host, uint16_t port, int32_t timeout) override {
        if (check_) setCACertBundle(caBundleStart, static_cast<size_t>(caBundleEnd - caBundleStart));
        connecting_ = true;
        verifyFlags_ = kNoVerifyResult;
        const int connected = HeaderTap<NetworkClientSecure>::connect(host, port, timeout);
        connecting_ = false;
        if (!connected && failure_.empty()) failure_ = tlsFailure();
        return connected;
    }

    void stop() override {
        if (connecting_) verifyFlags_ = mbedtls_ssl_get_verify_result(&sslclient->ssl_ctx);
        HeaderTap<NetworkClientSecure>::stop();
    }

private:
    static const uint32_t kNoVerifyResult = 0xFFFFFFFF;  // what mbedTLS answers before it has checked

    /** The reason behind the mbedTLS error the failed handshake left. */
    std::string tlsFailure() {
        const int error = sslclient->last_error;
        if (error >= -1) return "";  // no TCP connection: HTTPClient's "connection refused" is right
        // The certificate check itself said no (the X.509 errors, -0x2080 to
        // -0x3000), or the handshake stopped over the certificate.
        const int code = -error;
        const bool certificate = (code >= 0x2080 && code <= 0x3000) || error == MBEDTLS_ERR_SSL_BAD_CERTIFICATE ||
                                 error == MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED;
        if (certificate) {
            if (verifyFlags_ != kNoVerifyResult) {
                if (verifyFlags_ & MBEDTLS_X509_BADCERT_CN_MISMATCH) return "certificate is for another host";
                if (verifyFlags_ & MBEDTLS_X509_BADCERT_EXPIRED) return "certificate has expired";
            }
            return "certificate not trusted";
        }
        char text[48];
        snprintf(text, sizeof(text), "secure connection failed (TLS error -0x%04X)", code);
        return text;
    }

    bool check_ = true;
    bool connecting_ = false;
    uint32_t verifyFlags_ = kNoVerifyResult;
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
//    "cards":[{"id":"1","kind":"city","settings":{"city":"Antwerp"}}]}
// A card that may not come to the front by itself also has "front":false, and
// one that auto-rotate skips has "rotate":false.

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
        if (!card.mayInterrupt) item["front"] = false;
        if (!card.rotate) item["rotate"] = false;
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
        card.mayInterrupt = item["front"] | true;
        card.rotate = item["rotate"] | true;
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
    {
        const String zone = configManager_.getTimeZone();
        deskhog::TimeZone parsed;
        if (zone.length() && parsed.parse(zone.c_str())) {
            timeZone_ = zone.c_str();
            timeZoneName_ = configManager_.getTimeZoneName().c_str();
        }
    }
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

    // TLS handshakes need a deep stack: with 8 KB, a handshake left only 3 KB
    // unused, so it had 12 KB. Checking the server's certificate chain and
    // decoding pictures (gfx.fetchImage) happen on this stack too, so it has
    // 16 KB now. The stack has to be internal RAM: lwIP asserts that a task
    // opening a TCP connection does not run on a PSRAM stack.
    xTaskCreatePinnedToCore(httpTaskEntry, "pluginHttp", 16384, this, 1, &httpTask_, 0);
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
    // So tools can find the device as http://deskhog.local, or by the name it was given
    const std::string name = deviceName();
    if (MDNS.begin(name.c_str())) MDNS.addService("http", "tcp", 80);
    // The device's own clock stays on UTC. Plugins get local time from
    // time.date() and time.format(), which apply the time zone themselves.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

// Network task. mDNS answers to the new name at once; the router learns it
// the next time the device connects.
void PluginManager::renameNetworkServices() {
    const std::string name = deviceName();
    WiFi.setHostname(name.c_str());
    WiFi.STA.setHostname(name.c_str());
    if (!networkServicesStarted_) return;  // starting them will use the new name
    MDNS.end();
    if (MDNS.begin(name.c_str())) MDNS.addService("http", "tcp", 80);
}

std::string PluginManager::deviceName() {
    return manners_ ? manners_->settings().name : deskhog::DeviceSettings().name;
}

bool PluginManager::battery(deskhog::BatteryStatus& status) { return manners_ && manners_->battery(status); }

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
        std::string timeZone() override { return "UTC0"; }
        deskhog::NetStatus netStatus() override { return deskhog::NetStatus(); }
        bool battery(deskhog::BatteryStatus&) override { return false; }
        std::string deviceName() override { return "deskhog"; }
        uint32_t entropy() override { return 0; }
        void notify(const std::string&, const deskhog::Notice&) override {}
        void choicesDone(int, const std::vector<deskhog::Choice>&, const std::string&) override {}
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
            meta.message = probe.takesMessages();
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
                running->lua().httpResponse(result->requestId, std::move(result->response));
                break;
            }
        }
        delete result;  // the connection may be gone by now; then the answer is simply dropped
    }

    runAttention(now);

    for (auto it = running_.begin(); it != running_.end();) {
        PluginConnection& running = **it;
        for (PluginCard* card : running.cards()) running.lua().setVisible(card->cardId(), card->onScreen());
        running.lua().tick(now);
        for (PluginCard* card : running.cards()) {
            if (card->onScreen() || card->framesWanted()) card->paintIfNeeded();
        }
        if (running.cleanUp(now)) {
            attention_.forget(running.id() + ":");
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

// ---------------------------------------------------------------- attention

// What becomes of the notify() calls of plugins: which card comes to the
// front and when, which card shows a banner, and the LED. The rules are in
// Attention.h. Runs on the UI task with the LVGL mutex held.
void PluginManager::runAttention(uint32_t now) {
    std::string onScreen;
    for (PluginCard* card : cards_) {
        if (card->onScreen()) onScreen = card->key();
    }
    // A held button means someone is busy with the card on screen, however
    // long ago the press was. Nothing takes the screen away mid-hold.
    if (!onScreen.empty()) {
        for (int i = 0; i <= Input::BUTTON_UP; i++) {
            if (buttonHeld(i)) {
                attention_.busy(now);
                break;
            }
        }
    }

    const deskhog::Attention::Actions actions =
        attention_.tick(onScreen, now, [this](const std::string& key) { return mayInterrupt(key); });
    for (const std::string& key : actions.repaint) {
        PluginCard* card = runningCard(key);
        if (card && card->connection()) card->connection()->lua().repaint(card->cardId());
    }
    if (!actions.show.empty()) {
        PluginCard* card = runningCard(actions.show);
        if (card && showCard_) {
            showCard_(card->getCard());
            if (onBanner_) onBanner_(deskhog::Attention::kBannerMs);
        }
    }
    // A banner went up over the card on screen: light the screen while it shows.
    if (onBanner_ && !onScreen.empty() && attention_.banner(onScreen) &&
        std::find(actions.repaint.begin(), actions.repaint.end(), onScreen) != actions.repaint.end()) {
        onBanner_(deskhog::Attention::kBannerMs);
    }
    if (actions.led) {
        led_.setOverride(static_cast<uint8_t>(actions.ledRgb >> 16), static_cast<uint8_t>(actions.ledRgb >> 8),
                         static_cast<uint8_t>(actions.ledRgb), actions.ledMs);
    }
}

bool PluginManager::mayInterrupt(const std::string& key) {
    std::string connectionId, cardId;
    if (!splitKey(key, connectionId, cardId)) return false;
    Locked locked(lock_);
    const auto found = records_.find(connectionId);
    if (found == records_.end()) return false;
    const PluginCardRecord* card = found->second.card(cardId);
    return card && card->mayInterrupt;
}

bool PluginManager::cardRotates(const std::string& key) {
    std::string connectionId, cardId;
    if (!splitKey(key, connectionId, cardId)) return false;
    Locked locked(lock_);
    const auto found = records_.find(connectionId);
    if (found == records_.end()) return false;
    const PluginCardRecord* card = found->second.card(cardId);
    return card && card->rotate;
}

void PluginManager::requestAttention(const std::string& key, const deskhog::Notice& notice) {
    attention_.request(key, notice, mayInterrupt(key), millis());
}

void PluginManager::pressUsed() { attention_.busy(millis()); }

bool PluginManager::buttonHeld(uint8_t button) {
    if (button > Input::BUTTON_UP) return false;
    if (manners_ && manners_->swallowing(button)) return false;
    return buttons[button].isPressed();
}

std::vector<PluginNotification> PluginManager::notifications() {
    Locked locked(lock_);
    return notifications_;
}

void PluginManager::publishStates(uint32_t now) {
    std::vector<PluginNotification> waiting;
    for (const deskhog::Attention::Pending& pending : attention_.pending()) {
        PluginNotification notification;
        notification.key = pending.key;
        notification.text = pending.text;
        notification.count = pending.count;
        notification.ageMs = now - pending.sinceMs;
        waiting.push_back(notification);
    }

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
    notifications_.swap(waiting);
    // Lists of choices nobody came back for.
    for (auto it = choiceJobs_.begin(); it != choiceJobs_.end();) {
        it = now - it->second.atMs > 60000 ? choiceJobs_.erase(it) : std::next(it);
    }
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
            if (card && onMovedByHand_) onMovedByHand_();
            break;
        }
        case CommandKind::Show: {
            PluginCard* card = runningCard(command.target);
            if (card && showCard_) showCard_(card->getCard());
            if (card && onMovedByHand_) onMovedByHand_();
            break;
        }
        case CommandKind::Repaint:
            for (auto& running : running_) running->lua().repaintAll();
            break;
        case CommandKind::Message: {
            // A connection that stopped since the message was accepted simply misses it.
            PluginConnection* running = runningConnection(command.target);
            if (running) running->lua().message(command.card, command.body);
            break;
        }
        case CommandKind::Choices:
            startChoices(command);
            break;
    }
    display_->giveMutex();
}

// ------------------------------------------------------- messages, choices

int PluginManager::sendMessage(const std::string& connectionId, const std::string& cardId, const std::string& body,
                               String& error) {
    if (body.size() > MAX_MESSAGE_BYTES) {
        error = "the message is larger than 4 KB";
        return 413;
    }
    Locked locked(lock_);
    const auto found = records_.find(connectionId);
    if (found == records_.end()) {
        error = cardId.empty() ? "no such connection" : "no such card";
        return 404;
    }
    const PluginConnectionRecord& record = found->second;
    const auto meta = metas_.find(record.plugin);
    bool takes = false;
    if (meta != metas_.end() && cardId.empty()) {
        takes = meta->second.message;
    } else if (meta != metas_.end()) {
        const PluginCardRecord* card = record.card(cardId);
        if (!card) {
            error = "no such card";
            return 404;
        }
        // No kind on record means the plugin's first one, as when the card is created.
        for (size_t i = 0; i < meta->second.kinds.size(); i++) {
            const auto& kind = meta->second.kinds[i];
            if (kind.key == card->kind || (card->kind.empty() && i == 0)) takes = kind.message;
        }
    }
    if (!takes) {
        error = cardId.empty() ? "this plugin has no message() function" : "this card's plugin has no message() function for it";
        return 404;
    }
    const auto state = states_.find(connectionId);
    if (state == states_.end() || state->second.failed) {
        error = state == states_.end() ? "the connection is not running: none of its cards is on the device"
                                       : "the plugin crashed, so it cannot take messages";
        return 409;
    }
    size_t waiting = 0;
    size_t waitingBytes = 0;
    for (const Command& command : commands_) {
        if (command.kind != CommandKind::Message) continue;
        waitingBytes += command.body.size();
        if (command.target == connectionId) waiting++;
    }
    if (waiting >= MAX_QUEUED_MESSAGES || waitingBytes + body.size() > MAX_QUEUED_MESSAGE_BYTES) {
        error = "too many messages are waiting for this plugin. Try again in a moment";
        return 429;
    }
    Command command{CommandKind::Message, connectionId};
    command.card = cardId;
    command.body = body;
    commands_.push_back(std::move(command));
    return 200;
}

int PluginManager::choices(const std::string& connectionId, const std::string& kind, const std::string& field,
                           const PluginSettings& form, std::vector<deskhog::Choice>& out, String& error) {
    Locked locked(lock_);
    const auto found = records_.find(connectionId);
    if (found == records_.end()) {
        error = "no such connection";
        return 404;
    }
    const auto meta = metas_.find(found->second.plugin);
    // Only a function that a setting names can be run this way, never any other.
    std::string function;
    if (meta != metas_.end()) {
        const std::vector<deskhog::LuaPlugin::ConfigField>* fields = kind.empty() ? &meta->second.config : nullptr;
        for (const auto& candidate : meta->second.kinds) {
            if (!kind.empty() && candidate.key == kind) fields = &candidate.config;
        }
        if (fields) {
            for (const auto& candidate : *fields) {
                if (candidate.key == field) function = candidate.choices;
            }
        }
    }
    if (function.empty()) {
        error = "that setting has no list of choices";
        return 404;
    }

    // A list belongs to the form it was made for: the same question with other
    // values in the form is another list.
    std::string jobKey = connectionId + "/" + function + "/" + kind;
    for (const auto& pair : form) jobKey += "/" + pair.first + "=" + pair.second;
    const auto job = choiceJobs_.find(jobKey);
    if (job != choiceJobs_.end()) {
        if (!job->second.done) return 202;
        // Handed over once: asking again fetches a fresh list.
        const bool good = job->second.error.empty();
        if (good) {
            out.swap(job->second.choices);
        } else {
            error = job->second.error.c_str();
        }
        choiceJobs_.erase(job);
        return good ? 200 : 502;
    }
    if (choiceJobs_.size() >= MAX_CHOICE_JOBS) {
        error = "too many lists are being fetched. Try again in a moment";
        return 429;
    }
    choiceJobs_[jobKey].atMs = millis();
    Command command{CommandKind::Choices, connectionId};
    command.body = function;
    command.job = jobKey;
    command.context.kind = kind;
    command.context.settings = form;
    commands_.push_back(std::move(command));
    return 202;
}

// UI task, with the LVGL mutex held.
void PluginManager::startChoices(const Command& command) {
    const std::string& connectionId = command.target;
    PluginConnection* running = runningConnection(connectionId);
    if (!running) {
        // None of the connection's cards is on the device, so its program is
        // not running. That is the usual case for the very thing choices are
        // for: picking what the first card should show. Start the program
        // without a card. It stays for a moment, and the card that is added
        // next finds it already loaded.
        PluginConnectionRecord record;
        const size_t freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (!connection(connectionId, record)) {
            finishChoices(command.job, std::vector<deskhog::Choice>(), "no such connection");
            return;
        }
        if (running_.size() >= MAX_CONNECTIONS || freePsram < 400 * 1024) {
            finishChoices(command.job, std::vector<deskhog::Choice>(), "the device is out of memory for this");
            return;
        }
        running_.emplace_back(new PluginConnection(*this, connectionId, nextRuntimeId_++, cardWidth_, cardHeight_));
        running = running_.back().get();
        running->start(&record);
    }
    running->askChoices(command.body, command.job, command.context);
}

void PluginManager::finishChoices(const std::string& jobKey, const std::vector<deskhog::Choice>& choices,
                                  const std::string& error) {
    Locked locked(lock_);
    const auto job = choiceJobs_.find(jobKey);
    if (job == choiceJobs_.end() || job->second.done) return;
    job->second.done = true;
    job->second.choices = choices;
    job->second.error = error;
    job->second.atMs = millis();
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
        if (renameWanted_.exchange(false)) renameNetworkServices();
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

    HeaderTap<NetworkClient> plainClient;
    SecureClient secureClient;
    // Unless the plugin asked otherwise (`insecure = true`, for a box on the
    // local network with a certificate it made itself), the server has to show
    // a certificate that one of the well-known authorities vouches for, and
    // that is made out to this host name. Without the check traffic is still
    // encrypted, but the device cannot tell who it is talking to.
    secureClient.checkCertificates(!request.insecure);
    // Without this a host that stalls mid-handshake would hold up every
    // plugin's requests for two minutes.
    secureClient.setHandshakeTimeout(request.timeoutMs / 1000 + 1);

    // Redirects are followed here, by the rules in HttpRedirect.h, with a new
    // HTTPClient for each address. HTTPClient's own following gives up on a
    // `Location` that is only a path, and cannot go from http to https.
    std::string url = request.url;
    std::string method = request.method;
    const std::string firstOrigin = deskhog::urlOrigin(url);
    std::unique_ptr<HTTPClient> client;
    bool secure = false;
    int code = 0;
    for (int redirects = 0;; redirects++) {
        secure = url.compare(0, 8, "https://") == 0;
        client.reset(new HTTPClient);
        client->setReuse(false);
        client->setConnectTimeout((int32_t)request.timeoutMs);
        client->setTimeout((uint16_t)(request.timeoutMs > 60000 ? 60000 : request.timeoutMs));
        client->setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        client->setUserAgent("DeskHog");

        const String address(url.c_str());
        const bool began = secure ? client->begin(secureClient, address) : client->begin(plainClient, address);
        if (!began) {
            response.error = "bad url";
            return;
        }
        // A token or a cookie is for the host the plugin named, not for
        // wherever that host sends the request on to.
        const bool sameHost = deskhog::urlOrigin(url) == firstOrigin;
        for (const auto& header : request.headers) {
            if (sameHost || !deskhog::isCredentialHeader(header.first)) {
                client->addHeader(header.first.c_str(), header.second.c_str());
            }
        }

        const bool carriesBody = method != "GET" && method != "HEAD" && !request.body.empty();
        code = carriesBody ? client->sendRequest(method.c_str(),
                                                 reinterpret_cast<uint8_t*>(const_cast<char*>(request.body.data())),
                                                 request.body.size())
                           : client->sendRequest(method.c_str());
        if (code <= 0) {
            response.error = HTTPClient::errorToString(code).c_str();
            // HTTPClient calls every failure to connect "connection refused". The
            // client knows whether it was the name, the certificate or a timeout.
            if (code == HTTPC_ERROR_CONNECTION_REFUSED) {
                const std::string& reason = secure ? secureClient.failure() : plainClient.failure();
                if (!reason.empty()) response.error = reason;
            }
            client->end();
            return;
        }
        if (redirects >= deskhog::kMaxRedirects ||
            !deskhog::followRedirect(code, method, url, client->getLocation().c_str())) {
            break;
        }
        client->end();
    }
    HTTPClient& http = *client;
    const bool headRequest = method == "HEAD";
    if (secure) {
        secureClient.headersInto(response.headers);
    } else {
        plainClient.headersInto(response.headers);
    }

    // An answer with no body (HEAD, 204, or Content-Length: 0) is complete
    // already. Reading it would report "not connected" once the server has hung up.
    const int expected = http.getSize();  // -1 when the server did not say
    if (expected == 0 || code == 204 || code == 304 || headRequest) {
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

    // gfx.fetchImage: the plugin gets pixels, not the file. Decoding happens
    // here, on the network task, so a large picture does not stall the screen.
    if (request.imageMaxWidth > 0 && code >= 200 && code < 300) {
        std::string file;
        file.swap(response.body);
        decodeImage(reinterpret_cast<uint8_t*>(&file[0]), file.size(), request.imageMaxWidth, request.imageMaxHeight,
                    response.image, response.imageWidth, response.imageHeight, response.error);
    }
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

// ---------------------------------------------------------------- time zone

std::string PluginManager::timeZone() {
    Locked locked(lock_);
    return timeZone_;
}

std::string PluginManager::timeZoneName() {
    Locked locked(lock_);
    return timeZoneName_;
}

bool PluginManager::setTimeZone(const String& posix, const String& name, String& error) {
    deskhog::TimeZone parsed;
    if (posix.length() > 64 || name.length() > 64) {
        error = "that time zone is too long (64 characters at most)";
        return false;
    }
    if (posix.length() && !parsed.parse(posix.c_str())) {
        error = "that is not a POSIX time zone (they look like CET-1CEST,M3.5.0,M10.5.0/3)";
        return false;
    }
    configManager_.setTimeZone(posix, name);
    {
        Locked locked(lock_);
        timeZone_ = posix.length() ? posix.c_str() : "UTC0";
        timeZoneName_ = posix.length() ? name.c_str() : "";
    }
    queue(CommandKind::Repaint, "");  // every clock is wrong now
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
                                   const PluginSettings& cardSettings, String& error, bool mayInterrupt,
                                   bool rotate) {
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
        card.mayInterrupt = mayInterrupt;
        card.rotate = rotate;
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

bool PluginManager::updateCard(const std::string& key, const std::string* cardName, const PluginSettings& cardSettings,
                               const bool* mayInterrupt, const bool* rotate) {
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
        // The host's own settings take effect at once: no need to restart the plugin for them.
        const bool frontChanged = mayInterrupt && card->mayInterrupt != *mayInterrupt;
        if (frontChanged) card->mayInterrupt = *mayInterrupt;
        const bool rotateChanged = rotate && card->rotate != *rotate;
        if (rotateChanged) card->rotate = *rotate;
        if (!cardSettings.empty() || frontChanged || rotateChanged) {
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
