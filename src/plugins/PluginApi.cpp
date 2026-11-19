#include "plugins/PluginApi.h"
#include "PixelRuns.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <atomic>
#include <cmath>
#include <memory>

#include "DeskManners.h"
#include "plugins/PluginManager.h"

namespace {

const char kPluginsPrefix[] = "/api/plugins";

void addCors(AsyncWebServerResponse* response) {
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    response->addHeader("Access-Control-Expose-Headers", "X-Width, X-Height");
}

void sendJson(AsyncWebServerRequest* request, int code, const JsonDocument& doc) {
    String body;
    serializeJson(doc, body);
    AsyncWebServerResponse* response = request->beginResponse(code, "application/json", body);
    addCors(response);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

void sendError(AsyncWebServerRequest* request, int code, const String& message) {
    DynamicJsonDocument doc(512);
    doc["ok"] = false;
    doc["error"] = message;
    sendJson(request, code, doc);
}

void sendOk(AsyncWebServerRequest* request, const char* field = nullptr, const String& value = String()) {
    DynamicJsonDocument doc(256);
    doc["ok"] = true;
    if (field) doc[field] = value;
    sendJson(request, 200, doc);
}

bool answerPreflight(AsyncWebServerRequest* request) {
    if (request->method() != HTTP_OPTIONS) return false;
    AsyncWebServerResponse* response = request->beginResponse(204);
    addCors(response);
    request->send(response);
    return true;
}

// The body arrives in pieces; collect them in one block the request owns.
// The web server frees _tempObject when the request ends.
void collectBody(AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
    if (total > PluginManager::MAX_SOURCE_BYTES) return;  // answered with 413 later
    if (index == 0 && !request->_tempObject) request->_tempObject = malloc(total + 1);
    if (!request->_tempObject || index + len > total) return;
    memcpy(static_cast<uint8_t*>(request->_tempObject) + index, data, len);
}

/** Parse the request body as JSON. Answers the request itself when that fails. */
bool readJson(AsyncWebServerRequest* request, JsonDocument& doc) {
    const size_t length = request->contentLength();
    if (!request->_tempObject || length == 0 || length > PluginManager::MAX_SOURCE_BYTES) {
        sendError(request, 400, "send a JSON body, labelled application/json");
        return false;
    }
    if (deserializeJson(doc, static_cast<const char*>(request->_tempObject), length)) {
        sendError(request, 400, "the body is not valid JSON");
        return false;
    }
    return true;
}

String param(AsyncWebServerRequest* request, const char* name) {
    return request->hasParam(name) ? request->getParam(name)->value() : String();
}

PluginSettings settingsFrom(JsonVariantConst source) {
    PluginSettings settings;
    if (!source.is<JsonObjectConst>()) return settings;
    for (JsonPairConst pair : source.as<JsonObjectConst>()) {
        if (!pair.value().isNull()) settings[pair.key().c_str()] = pair.value().as<String>().c_str();
    }
    return settings;
}

void fieldsToJson(JsonArray target, const std::vector<deskhog::LuaPlugin::ConfigField>& fields) {
    for (const auto& field : fields) {
        JsonObject item = target.createNestedObject();
        item["key"] = field.key.c_str();
        item["label"] = field.label.c_str();
        item["type"] = field.type.c_str();
        item["default"] = field.defaultValue.c_str();
        if (field.secret) item["secret"] = true;
        if (!field.options.empty()) {
            JsonArray options = item.createNestedArray("options");
            for (const std::string& option : field.options) options.add(option.c_str());
        }
        // The plugin lists what to pick from: see /api/connections/choices.
        if (!field.choices.empty()) item["choices"] = true;
    }
}

// Saved settings, minus the secrets: those are only reported as "set".
void settingsToJson(JsonObject target, const PluginSettings& settings,
                    const std::vector<deskhog::LuaPlugin::ConfigField>* schema) {
    JsonObject values = target.createNestedObject("settings");
    JsonArray secretsSet = target.createNestedArray("secretsSet");
    for (const auto& pair : settings) {
        bool secret = false;
        if (schema) {
            for (const auto& field : *schema) secret = secret || (field.key == pair.first && field.secret);
        }
        if (!secret) {
            values[pair.first.c_str()] = pair.second.c_str();
        } else if (!pair.second.empty()) {
            secretsSet.add(pair.first.c_str());
        }
    }
}

void systemToJson(JsonObject system, PluginManager& manager) {
    const PluginSystemInfo info = manager.systemInfo();
    system["plugin_cards"] = info.cards;
    system["heap_free"] = info.heapFree;
    system["heap_lowest"] = info.heapLowest;
    system["psram_free"] = info.psramFree;
    system["ui_stack_unused"] = info.uiStackUnused;
    system["http_stack_unused"] = info.httpStackUnused;
    system["lvgl_heap_total"] = info.lvglHeapTotal;
    system["lvgl_heap_peak"] = info.lvglHeapPeak;
}

void storageToJson(JsonObject storage, PluginManager& manager) {
    size_t used = 0, total = 0;
    manager.storageInfo(used, total);
    storage["used"] = used;
    storage["total"] = total;
}

// ------------------------------------------------------------------ device

void batteryToJson(JsonObject target, int percent, float volts, bool charging) {
    target["percent"] = percent;
    target["volts"] = std::round(volts * 100) / 100.0;  // 4.2, not 4.19999981
    target["charging"] = charging;
}

// The device's settings as GET /api/device gives them, and how it is doing.
void deviceToJson(JsonObject device, DeskManners& manners) {
    const deskhog::DeviceSettings settings = manners.settings();
    device["name"] = String(settings.name.c_str());
    JsonObject screen = device.createNestedObject("screen");
    screen["day"] = settings.screenDay;
    screen["night"] = settings.screenNight;
    JsonObject led = device.createNestedObject("led");
    led["day"] = settings.ledDay;
    led["night"] = settings.ledNight;
    JsonObject night = device.createNestedObject("night");
    night["enabled"] = settings.nightEnabled;
    night["from"] = String(deskhog::formatClock(settings.nightFrom).c_str());
    night["until"] = String(deskhog::formatClock(settings.nightUntil).c_str());
    device.createNestedObject("rotate")["seconds"] = settings.rotateSeconds;
    device["battery"] = settings.battery;

    const DeskManners::Status now = manners.status();
    JsonObject status = device.createNestedObject("status");
    status["night"] = now.night;
    status["manual"] = now.manual;
    deskhog::BatteryStatus battery;
    if (manners.battery(battery)) {
        batteryToJson(status.createNestedObject("battery"), battery.percent, battery.volts, battery.charging);
    } else {
        status["battery"] = nullptr;
    }
    const BatteryGauge::Reading gauge = manners.gauge();
    if (gauge.found) {
        batteryToJson(status.createNestedObject("gauge"), gauge.percent, gauge.volts, gauge.charging);
    } else {
        status["gauge"] = nullptr;
    }
}

// The groups a POST body may nest settings in: {"screen": {"day": 80}} is "screen.day".
bool isSettingsGroup(const char* key) {
    return !strcmp(key, "screen") || !strcmp(key, "led") || !strcmp(key, "night") || !strcmp(key, "rotate");
}

/** One setting of a POST body as the text applyDeviceSettings reads. False, with `error` set, for a key that
 *  does not exist or a value of the wrong JSON kind. */
bool settingText(const std::string& key, JsonVariantConst value, std::string& text, std::string& error) {
    switch (deskhog::deviceSettingKind(key)) {
        case deskhog::SettingKind::Boolean:
            if (value.is<bool>()) {
                text = value.as<bool>() ? "true" : "false";
                return true;
            }
            error = key + " must be true or false";
            return false;
        case deskhog::SettingKind::WholeNumber:
            // A boolean is not a whole number, and neither is 80.0.
            if (value.is<long>() && !value.is<bool>() && !value.is<double>()) {
                text = std::to_string(value.as<long>());
                return true;
            }
            error = key + " must be a whole number";
            return false;
        case deskhog::SettingKind::Text:
            if (value.is<const char*>()) {
                text = value.as<const char*>();
                return true;
            }
            error = key + " must be text";
            return false;
        case deskhog::SettingKind::Unknown:
            break;
    }
    error = "unknown setting " + key;
    return false;
}

/** A POST body's settings as dotted pairs. `preview` is left out; `status` is not a setting. */
bool flattenSettings(JsonObjectConst body, std::vector<std::pair<std::string, std::string>>& out,
                     std::string& error) {
    for (JsonPairConst pair : body) {
        const std::string key = pair.key().c_str();
        if (key == "preview") continue;
        std::string text;
        if (isSettingsGroup(key.c_str()) && pair.value().is<JsonObjectConst>()) {
            for (JsonPairConst inner : pair.value().as<JsonObjectConst>()) {
                const std::string name = key + "." + inner.key().c_str();
                if (!settingText(name, inner.value(), text, error)) return false;
                out.emplace_back(name, text);
            }
        } else {
            // Only the plain keys are top-level settings: a dotted key or a group that is not an object is not.
            if (key.find('.') != std::string::npos || isSettingsGroup(key.c_str())) {
                error = "unknown setting " + key;
                return false;
            }
            if (!settingText(key, pair.value(), text, error)) return false;
            out.emplace_back(key, text);
        }
    }
    return true;
}

void sendDevice(AsyncWebServerRequest* request, DeskManners& manners) {
    DynamicJsonDocument doc(1024);
    deviceToJson(doc.to<JsonObject>(), manners);
    sendJson(request, 200, doc);
}

// GET /api/device, and POST with any part of the same object. A POST changes
// all of it or nothing, and answers with the whole object as GET gives it.
void handleDevice(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DeskManners* manners = manager.deskManners();
    if (!manners) return sendError(request, 503, "the device is still starting");
    if (request->method() == HTTP_GET) return sendDevice(request, *manners);
    if (request->method() != HTTP_POST) return sendError(request, 405, "GET or POST /api/device");

    DynamicJsonDocument doc(2048);
    if (!readJson(request, doc)) return;
    if (!doc.is<JsonObject>()) return sendError(request, 400, "send a JSON object of the settings to change");
    const JsonObjectConst body = doc.as<JsonObjectConst>();

    // While a slider on the web page moves, the device shows those levels for a few seconds.
    int preview = 0;
    if (body.containsKey("preview")) {
        const char* which = body["preview"] | "";
        preview = !strcmp(which, "night") ? 2 : !strcmp(which, "day") ? 1 : 0;
        if (!preview) return sendError(request, 400, "preview is night or day");
    }

    std::vector<std::pair<std::string, std::string>> changes;
    std::string error;
    if (!flattenSettings(body, changes, error)) return sendError(request, 400, error.c_str());
    if (!changes.empty()) {
        const std::string oldName = manners->settings().name;
        if (!manners->change(changes, error)) return sendError(request, 400, error.c_str());
        // mDNS restarts on the network task, not here.
        if (manners->settings().name != oldName) manager.deviceRenamed();
    }
    if (preview) manners->preview(preview == 2);
    sendDevice(request, *manners);
}

// Restarting from here would cut off the answer, so a small task waits a moment first.
void handleRestart(AsyncWebServerRequest* request, PluginManager&) {
    if (answerPreflight(request)) return;
    if (request->method() != HTTP_POST) return sendError(request, 405, "POST /api/restart");
    sendOk(request);
    static std::atomic<bool> restarting{false};
    if (restarting.exchange(true)) return;
    xTaskCreate(
        [](void*) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        },
        "restart", 4096, nullptr, 1, nullptr);
}

// ------------------------------------------------------------------- state

void handleState(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    const std::map<std::string, PluginMeta> metas = manager.metas();
    const std::vector<PluginConnectionRecord> connections = manager.connections();
    const std::map<std::string, PluginConnectionState> states = manager.states();

    DynamicJsonDocument doc(32768);

    JsonArray plugins = doc.createNestedArray("plugins");
    for (const auto& pair : metas) {
        const PluginMeta& meta = pair.second;
        JsonObject item = plugins.createNestedObject();
        item["name"] = pair.first.c_str();
        item["title"] = meta.title.c_str();
        if (!meta.description.empty()) item["description"] = meta.description.c_str();
        if (!meta.version.empty()) item["version"] = meta.version.c_str();
        if (!meta.author.empty()) item["author"] = meta.author.c_str();
        char hash[9];
        snprintf(hash, sizeof(hash), "%08x", (unsigned)meta.hash);
        item["hash"] = String(hash);
        item["multiCard"] = meta.multiCard;
        if (!meta.error.empty()) item["error"] = meta.error.c_str();
        if (meta.message) item["message"] = true;  // it takes messages pushed to a connection
        fieldsToJson(item.createNestedArray("config"), meta.config);
        JsonArray kinds = item.createNestedArray("kinds");
        for (const auto& kind : meta.kinds) {
            JsonObject kindItem = kinds.createNestedObject();
            kindItem["key"] = kind.key.c_str();
            kindItem["name"] = kind.name.c_str();
            if (kind.message) kindItem["message"] = true;  // and this kind of card takes them too
            fieldsToJson(kindItem.createNestedArray("config"), kind.config);
        }
    }

    JsonArray connectionList = doc.createNestedArray("connections");
    for (const PluginConnectionRecord& record : connections) {
        const auto meta = metas.find(record.plugin);
        const auto state = states.find(record.id);
        JsonObject item = connectionList.createNestedObject();
        item["id"] = record.id.c_str();
        item["plugin"] = record.plugin.c_str();
        item["name"] = record.name.c_str();
        item["cards"] = record.cards.size();
        settingsToJson(item, record.settings, meta != metas.end() ? &meta->second.config : nullptr);
        if (state == states.end()) {
            item["state"] = "stopped";
        } else {
            item["state"] = state->second.failed ? "failed" : "running";
            item["memory"] = state->second.memory;
            if (state->second.failed) item["error"] = state->second.error.c_str();
        }
    }

    JsonArray cards = doc.createNestedArray("cards");
    for (const CardConfig& config : manager.cardStack()) {
        JsonObject item = cards.createNestedObject();
        item["type"] = config.type;
        item["config"] = config.config;
        item["name"] = config.name;
        item["key"] = config.type + "|" + config.config;
        if (!config.isPlugin()) continue;

        std::string connectionId, cardId;
        if (!PluginManager::splitKey(config.config.c_str(), connectionId, cardId)) continue;
        // String, not c_str(): ArduinoJson keeps a bare pointer to C strings, and
        // these two are gone before the document is sent.
        item["connection"] = String(connectionId.c_str());
        item["card"] = String(cardId.c_str());
        for (const PluginConnectionRecord& record : connections) {
            if (record.id != connectionId) continue;
            item["plugin"] = record.plugin.c_str();
            const PluginCardRecord* card = record.card(cardId);
            if (card) {
                item["kind"] = card->kind.c_str();
                item["front"] = card->mayInterrupt;  // "May come to the front"
                item["rotate"] = card->rotate;       // auto-rotate stops at it
                JsonObject values = item.createNestedObject("settings");
                for (const auto& pair : card->settings) values[pair.first.c_str()] = pair.second.c_str();
            }
        }
        const auto state = states.find(connectionId);
        if (state != states.end()) {
            const auto cardState = state->second.cards.find(cardId);
            if (cardState != state->second.cards.end()) {
                item["frames"] = cardState->second.frames;
                item["onScreen"] = cardState->second.onScreen;
                if (!cardState->second.title.empty()) item["title"] = cardState->second.title.c_str();
            }
        }
    }

    // What plugins said with notify() that nobody has seen yet: it shows when
    // someone arrives at the card.
    JsonArray notifications = doc.createNestedArray("notifications");
    for (const PluginNotification& waiting : manager.notifications()) {
        JsonObject item = notifications.createNestedObject();
        item["card"] = String(waiting.key.c_str());
        item["text"] = String(waiting.text.c_str());
        item["count"] = waiting.count;
        item["age"] = waiting.ageMs / 1000;
    }

    if (manager.deskManners()) deviceToJson(doc.createNestedObject("device"), *manager.deskManners());

    // String, not c_str(): the two are temporaries.
    JsonObject timezone = doc.createNestedObject("timezone");
    timezone["posix"] = String(manager.timeZone().c_str());
    timezone["name"] = String(manager.timeZoneName().c_str());

    JsonObject directory = doc.createNestedObject("directory");
    directory["source"] = manager.directorySource();
    directory["default"] = PluginManager::defaultDirectorySource();

    storageToJson(doc.createNestedObject("storage"), manager);
    systemToJson(doc.createNestedObject("system"), manager);
    sendJson(request, 200, doc);
}

// The device only remembers where plugins come from. The web page and the
// command line tool do the fetching: they have the internet connection, the
// memory and proper certificate checks.
void handleDirectory(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(1024);
    if (!readJson(request, doc)) return;
    String error;
    if (!manager.setDirectorySource(doc["source"] | "", error)) return sendError(request, 400, error);
    sendOk(request, "source", manager.directorySource());
}

// The time zone plugins tell local time by. The web page looks up the POSIX
// string for the place someone picks; the device has no list of its own.
void handleTimeZone(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(1024);
    if (!readJson(request, doc)) return;
    String error;
    if (!manager.setTimeZone(doc["posix"] | "", doc["name"] | "", error)) return sendError(request, 400, error);
    sendOk(request, "posix", manager.timeZone().c_str());
}

// ------------------------------------------------------------------- cards

void handleAddCard(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(8192);
    if (!readJson(request, doc)) return;
    String error;
    const std::string key = manager.addCard(doc["plugin"] | "", doc["connection"] | "", doc["connectionName"] | "",
                                            settingsFrom(doc["connectionSettings"]), doc["kind"] | "", doc["name"] | "",
                                            settingsFrom(doc["settings"]), error, doc["front"] | true,
                                            doc["rotate"] | true);
    if (key.empty()) return sendError(request, 400, error);
    sendOk(request, "key", key.c_str());
}

void handleUpdateCard(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(8192);
    if (!readJson(request, doc)) return;
    const std::string key = param(request, "key").c_str();
    std::string name;
    const bool hasName = doc["name"].is<const char*>();
    if (hasName) name = doc["name"].as<const char*>();
    // "front": may the card come to the front by itself when its plugin calls notify()?
    const bool hasFront = doc["front"].is<bool>();
    const bool front = doc["front"] | true;
    // "rotate": does auto-rotate stop at the card?
    const bool hasRotate = doc["rotate"].is<bool>();
    const bool rotate = doc["rotate"] | true;
    if (!manager.updateCard(key, hasName ? &name : nullptr, settingsFrom(doc["settings"]), hasFront ? &front : nullptr,
                            hasRotate ? &rotate : nullptr)) {
        return sendError(request, 404, "no such card");
    }
    sendOk(request);
}

void handleLayout(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(8192);
    if (!readJson(request, doc)) return;
    if (!doc.is<JsonArray>()) return sendError(request, 400, "send the list of card keys in their new order");
    std::vector<std::string> keys;
    for (JsonVariantConst item : doc.as<JsonArrayConst>()) keys.push_back(item.as<String>().c_str());
    if (!manager.saveLayout(keys)) return sendError(request, 500, "could not save the cards");
    sendOk(request);
}

void handleScreen(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    auto pixels = std::make_shared<std::vector<uint8_t>>();
    int width = 0, height = 0;
    // "since" is the version of the picture the page already has. Sending 63 KB
    // it does not need, every second, would cost the card on screen its frame rate.
    uint32_t frames = 0;
    const bool hasSince = request->hasParam("since");
    const uint32_t since = hasSince ? static_cast<uint32_t>(strtoul(param(request, "since").c_str(), nullptr, 10)) : 0;
    if (!manager.copyCardPixels(param(request, "key").c_str(), *pixels, width, height, frames, hasSince ? &since : nullptr)) {
        return sendError(request, 404, "that card is not on the device right now");
    }
    // A web page asks for the picture packed; the command line tool takes it as it is.
    bool packed = false;
    if (request->hasParam("packed") && !pixels->empty()) {
        auto runs = std::make_shared<std::vector<uint8_t>>();
        if (deskhog::packPixelRuns(*pixels, *runs)) {
            pixels = runs;
            packed = true;
        }
    }
    if (pixels->empty()) {
        AsyncWebServerResponse* unchanged = request->beginResponse(204);
        addCors(unchanged);
        unchanged->addHeader("X-Frames", String(frames));
        request->send(unchanged);
        return;
    }
    AsyncWebServerResponse* response = request->beginResponse(
        "application/octet-stream", pixels->size(), [pixels](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
            if (index >= pixels->size()) return 0;
            const size_t count = std::min(maxLen, pixels->size() - index);
            memcpy(buffer, pixels->data() + index, count);
            return count;
        });
    addCors(response);
    response->addHeader("Cache-Control", "no-store");
    response->addHeader("X-Width", String(width));
    response->addHeader("X-Height", String(height));
    response->addHeader("X-Frames", String(frames));
    if (packed) response->addHeader("X-Packed", "runs");
    request->send(response);
}

void handlePress(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    const String button = request->hasParam("button") ? param(request, "button") : String("center");
    const int index = button == "down" ? 0 : button == "center" ? 1 : button == "up" ? 2 : -1;
    if (index < 0) return sendError(request, 400, "button is up, center or down");
    manager.press(param(request, "key").c_str(), static_cast<uint8_t>(index));
    sendOk(request);
}

void handleShow(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    manager.show(param(request, "key").c_str());
    sendOk(request);
}

// ---------------------------------------------------------------- messages

// The body of a pushed message, as it came: text or JSON. The web server only
// hands over a body it did not take for a form, so say what it is:
// Content-Type: text/plain or application/json.
std::string messageBody(AsyncWebServerRequest* request) {
    if (request->_tempObject) {
        return std::string(static_cast<const char*>(request->_tempObject), request->contentLength());
    }
    // Sent as a form after all (curl -d does that): the server split it at
    // every & and =. Put it back together as well as that goes.
    std::string body;
    for (size_t i = 0; i < request->params(); i++) {
        const AsyncWebParameter* part = request->getParam(i);
        if (!part->isPost() || part->isFile()) continue;
        if (!body.empty()) body += '&';
        body += part->name().c_str();
        if (part->value().length()) body += std::string("=") + part->value().c_str();
    }
    return body;
}

// POST /api/cards/message?key=<c>:<n> and POST /api/connections/message?id=<c>.
// This runs on the web server's task. The plugin runs on the UI task, so the
// message is only queued here; the plugin gets it a moment later.
void handleMessage(AsyncWebServerRequest* request, PluginManager& manager, bool toCard) {
    if (answerPreflight(request)) return;
    if (request->method() != HTTP_POST) return sendError(request, 405, "POST the message as the request body");
    if (request->contentLength() > PluginManager::MAX_MESSAGE_BYTES) {
        return sendError(request, 413, "the message is larger than 4 KB");
    }
    std::string connectionId, cardId;
    if (toCard) {
        if (!PluginManager::splitKey(param(request, "key").c_str(), connectionId, cardId)) {
            return sendError(request, 404, "no such card");
        }
    } else {
        connectionId = param(request, "id").c_str();
    }
    String error;
    const int status = manager.sendMessage(connectionId, cardId, messageBody(request), error);
    if (status != 200) return sendError(request, status, error);
    sendOk(request);
}

void handleCardMessage(AsyncWebServerRequest* request, PluginManager& manager) { handleMessage(request, manager, true); }

void handleConnectionMessage(AsyncWebServerRequest* request, PluginManager& manager) {
    handleMessage(request, manager, false);
}

// ------------------------------------------------------------------ choices

void appendJsonText(String& out, const std::string& value) {
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

// GET /api/connections/choices?id=<c>&field=<key>[&kind=<kind>][&settings=<JSON object>]
//
// `settings` is what the form that asks holds right now, saved or not; the
// plugin's function gets it as ctx.settings. It rides in the address so that
// asking again is the very same request.
//
// The list comes from the plugin, which may have to ask the network first, and
// plugins only run on the UI task. Neither this task nor the UI task may sit
// and wait for that. So the first request starts the work and is answered 202
// {"pending":true} at once; the same request, repeated, gets the list when it
// is there. The page asks a few times a second.
void handleChoices(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    std::vector<deskhog::Choice> choices;
    String error;
    PluginSettings form;
    const String formText = param(request, "settings");
    if (formText.length() > 0) {
        DynamicJsonDocument doc(4096);
        if (formText.length() > 1024 || deserializeJson(doc, formText) || !doc.is<JsonObject>()) {
            return sendError(request, 400, "settings is a JSON object of at most 1 KB");
        }
        form = settingsFrom(doc.as<JsonVariantConst>());
    }
    const int status = manager.choices(param(request, "id").c_str(), param(request, "kind").c_str(),
                                       param(request, "field").c_str(), form, choices, error);
    if (status == 202) {
        DynamicJsonDocument doc(128);
        doc["ok"] = true;
        doc["pending"] = true;
        return sendJson(request, 202, doc);
    }
    if (status != 200) return sendError(request, status, error);

    // Written by hand: a long list would not fit a JSON document of a size that is safe to reserve.
    String body = "{\"ok\":true,\"choices\":[";
    for (size_t i = 0; i < choices.size(); i++) {
        if (i) body += ',';
        body += "{\"value\":";
        appendJsonText(body, choices[i].value);
        body += ",\"label\":";
        appendJsonText(body, choices[i].label);
        body += '}';
    }
    body += "]}";
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", body);
    addCors(response);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
}

// ------------------------------------------------------------- connections

void handleAddConnection(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(8192);
    if (!readJson(request, doc)) return;
    String error;
    const std::string id =
        manager.createConnection(doc["plugin"] | "", doc["name"] | "", settingsFrom(doc["settings"]), error);
    if (id.empty()) return sendError(request, 400, error);
    sendOk(request, "id", id.c_str());
}

void handleUpdateConnection(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    DynamicJsonDocument doc(8192);
    if (!readJson(request, doc)) return;
    std::string name;
    const bool hasName = doc["name"].is<const char*>();
    if (hasName) name = doc["name"].as<const char*>();
    if (!manager.updateConnection(param(request, "id").c_str(), hasName ? &name : nullptr, settingsFrom(doc["settings"]))) {
        return sendError(request, 404, "no such connection");
    }
    sendOk(request);
}

void handleRemoveConnection(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    if (!manager.removeConnection(param(request, "id").c_str())) return sendError(request, 404, "no such connection");
    sendOk(request);
}

// ----------------------------------------------------------- plugin files

void handlePluginList(AsyncWebServerRequest* request, PluginManager& manager) {
    const std::map<std::string, PluginMeta> metas = manager.metas();
    DynamicJsonDocument doc(8192);
    JsonArray plugins = doc.createNestedArray("plugins");
    for (const PluginInfo& info : manager.list()) {
        JsonObject item = plugins.createNestedObject();
        item["name"] = info.name;
        item["size"] = info.size;
        const auto meta = metas.find(info.name.c_str());
        item["title"] = meta != metas.end() ? String(meta->second.title.c_str()) : info.name;
        if (meta != metas.end() && !meta->second.error.empty()) item["error"] = meta->second.error.c_str();
    }
    storageToJson(doc.createNestedObject("storage"), manager);
    systemToJson(doc.createNestedObject("system"), manager);
    sendJson(request, 200, doc);
}

// How a plugin is doing, across all of its connections.
void handlePluginStatus(AsyncWebServerRequest* request, PluginManager& manager, const String& name) {
    const std::map<std::string, PluginMeta> metas = manager.metas();
    const std::map<std::string, PluginConnectionState> states = manager.states();
    const auto meta = metas.find(name.c_str());

    DynamicJsonDocument doc(12288);
    doc["name"] = name;
    doc["title"] = meta != metas.end() ? String(meta->second.title.c_str()) : name;

    String state = "no card";
    String error = meta != metas.end() ? meta->second.error.c_str() : "";
    size_t memory = 0;
    uint32_t frames = 0;
    bool onScreen = false;
    JsonArray cards = doc.createNestedArray("cards");
    for (const PluginConnectionRecord& record : manager.connections()) {
        if (record.plugin != name.c_str()) continue;
        const auto running = states.find(record.id);
        if (running == states.end()) continue;
        if (state != "failed") state = running->second.failed ? "failed" : "running";
        if (running->second.failed) error = running->second.error.c_str();
        memory += running->second.memory;
        for (const auto& card : running->second.cards) {
            frames += card.second.frames;
            onScreen = onScreen || card.second.onScreen;
            cards.add(String((record.id + ":" + card.first).c_str()));
        }
    }
    if (error.length() && state == "no card") state = "failed";
    doc["state"] = state;
    doc["error"] = error;
    doc["memory"] = memory;
    doc["frames"] = frames;
    doc["on_screen"] = onScreen;
    JsonArray log = doc.createNestedArray("log");
    for (const String& line : manager.logLines(name)) log.add(line);
    sendJson(request, 200, doc);
}

void handlePlugins(AsyncWebServerRequest* request, PluginManager& manager) {
    if (answerPreflight(request)) return;
    const WebRequestMethodComposite method = request->method();

    // Split "/api/plugins/<name>/<action>".
    String rest = request->url().substring(strlen(kPluginsPrefix));
    while (rest.startsWith("/")) rest = rest.substring(1);
    while (rest.endsWith("/")) rest = rest.substring(0, rest.length() - 1);
    String name = rest;
    String action;
    const int slash = rest.indexOf('/');
    if (slash >= 0) {
        name = rest.substring(0, slash);
        action = rest.substring(slash + 1);
    }
    if (name.endsWith(".lua")) name = name.substring(0, name.length() - 4);  // so `curl -T weather.lua` just works

    if (name.isEmpty()) {
        if (method == HTTP_GET) return handlePluginList(request, manager);
        return sendError(request, 405, "use /api/plugins/<name>");
    }
    if (!deskhog::isValidPluginName(std::string(name.c_str()))) {
        return sendError(request, 400, "plugin names are 1-32 characters: a-z, 0-9, - and _");
    }

    if (action.isEmpty() && (method == HTTP_PUT || method == HTTP_POST)) {
        const size_t length = request->contentLength();
        if (length > PluginManager::MAX_SOURCE_BYTES) return sendError(request, 413, "the plugin is larger than 64 KB");
        if (!request->_tempObject) {
            return sendError(request, 400, "send the Lua source as the request body, labelled application/octet-stream");
        }
        String error;
        // "?card=no": just install it. Otherwise a plugin that no card shows yet gets a first card.
        const bool addFirstCard = !(request->hasParam("card") && param(request, "card") == "no");
        if (!manager.writeSource(name, static_cast<const uint8_t*>(request->_tempObject), length, error, addFirstCard)) {
            return sendError(request, 400, error);
        }
        return sendOk(request, "name", name);
    }

    if (!manager.exists(name)) return sendError(request, 404, "no plugin named " + name);

    if (action.isEmpty() && method == HTTP_GET) {
        String source;
        if (!manager.readSource(name, source)) return sendError(request, 500, "could not read the plugin");
        AsyncWebServerResponse* response = request->beginResponse(200, "text/plain; charset=utf-8", source);
        addCors(response);
        response->addHeader("Cache-Control", "no-store");
        return request->send(response);
    }
    if (action.isEmpty() && method == HTTP_DELETE) {
        manager.remove(name);
        return sendOk(request, "name", name);
    }
    if (action == "status" && method == HTTP_GET) return handlePluginStatus(request, manager, name);

    sendError(request, 404, "unknown plugin API call");
}

}  // namespace

void registerPluginRoutes(AsyncWebServer& server, PluginManager& manager) {
    using Handler = void (*)(AsyncWebServerRequest*, PluginManager&);
    const auto route = [&server, &manager](const char* uri, Handler handler) {
        server.on(uri, HTTP_ANY, [&manager, handler](AsyncWebServerRequest* request) { handler(request, manager); },
                  nullptr, collectBody);
    };
    route("/api/state", handleState);
    route("/api/directory", handleDirectory);
    route("/api/timezone", handleTimeZone);
    route("/api/device", handleDevice);
    route("/api/restart", handleRestart);
    route("/api/cards/add", handleAddCard);
    route("/api/cards/update", handleUpdateCard);
    route("/api/cards/layout", handleLayout);
    route("/api/cards/screen", handleScreen);
    route("/api/cards/press", handlePress);
    route("/api/cards/show", handleShow);
    route("/api/cards/message", handleCardMessage);
    route("/api/connections/add", handleAddConnection);
    route("/api/connections/update", handleUpdateConnection);
    route("/api/connections/remove", handleRemoveConnection);
    route("/api/connections/message", handleConnectionMessage);
    route("/api/connections/choices", handleChoices);
    route("/api/plugins*", handlePlugins);
}
