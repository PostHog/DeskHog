#include "ConfigManager.h"
#include "SystemController.h"
#include <ArduinoJson.h>

ConfigManager::ConfigManager() {
    // Constructor
}

ConfigManager::ConfigManager(EventQueue& eventQueue) {
    _eventQueue = &eventQueue;
}

void ConfigManager::setEventQueue(EventQueue* queue) {
    _eventQueue = queue;
}

void ConfigManager::begin() {
    // Initialize preferences
    _preferences.begin(_namespace, false);
    _insightsPrefs.begin(_insightsNamespace, false);
    _cardPrefs.begin(_cardNamespace, false);
    
    // Check initial API configuration state
    updateApiConfigurationState();
}

// Private helper to check and update API configuration state
void ConfigManager::updateApiConfigurationState() {
    if (!_preferences.isKey(_teamIdKey) || getTeamId() == NO_TEAM_ID) {
        SystemController::setApiState(ApiState::API_AWAITING_CONFIG);
        return;
    }
    
    if (!_preferences.isKey(_apiKeyKey) || getApiKey().isEmpty()) {
        SystemController::setApiState(ApiState::API_AWAITING_CONFIG);
        return;
    }
    
    // Both team ID and API key are set
    SystemController::setApiState(ApiState::API_CONFIGURED);
}

// Helper method to commit changes to flash
void ConfigManager::commit() {
    _preferences.end();
    _insightsPrefs.end();
    _cardPrefs.end();
    
    _preferences.begin(_namespace, false);
    _insightsPrefs.begin(_insightsNamespace, false);
    _cardPrefs.begin(_cardNamespace, false);
}

bool ConfigManager::saveWiFiCredentials(const String& ssid, const String& password) {
    if (ssid.length() == 0 || ssid.length() > MAX_SSID_LENGTH) {
        return false;
    }

    if (password.length() > MAX_PASSWORD_LENGTH) {
        return false;
    }

    // Save credentials
    _preferences.putString(_ssidKey, ssid);
    _preferences.putString(_passwordKey, password);
    _preferences.putBool(_hasCredentialsKey, true);
    
    // Commit changes
    commit();
    
    // Publish event if event queue is available
    if (_eventQueue != nullptr) {
        _eventQueue->publishEvent(EventType::WIFI_CREDENTIALS_FOUND, "");
    }
    
    return true;
}

bool ConfigManager::getWiFiCredentials(String& ssid, String& password) {
    if (!hasWiFiCredentials()) {
        return false;
    }

    // Retrieve credentials
    ssid = _preferences.getString(_ssidKey, "");
    password = _preferences.getString(_passwordKey, "");
    
    return true;
}

void ConfigManager::clearWiFiCredentials() {
    _preferences.remove(_ssidKey);
    _preferences.remove(_passwordKey);
    _preferences.putBool(_hasCredentialsKey, false);
    
    // Commit changes
    commit();
    
    // Publish event if event queue is available
    if (_eventQueue != nullptr) {
        _eventQueue->publishEvent(EventType::NEED_WIFI_CREDENTIALS, "");
    }
}

bool ConfigManager::hasWiFiCredentials() {
    return _preferences.getBool(_hasCredentialsKey, false);
}

bool ConfigManager::checkWiFiCredentialsAndPublish() {
    bool hasCredentials = hasWiFiCredentials();
    
    if (_eventQueue != nullptr) {
        if (hasCredentials) {
            _eventQueue->publishEvent(EventType::WIFI_CREDENTIALS_FOUND, "");
        } else {
            _eventQueue->publishEvent(EventType::NEED_WIFI_CREDENTIALS, "");
        }
    }
    
    return hasCredentials;
}


void ConfigManager::setTeamId(int teamId) {
    _preferences.putInt(_teamIdKey, teamId);
    
    // Commit changes
    commit();
    
    updateApiConfigurationState();
}

int ConfigManager::getTeamId() {
    if (!_preferences.isKey(_teamIdKey)) {
        return NO_TEAM_ID;
    }
    return _preferences.getInt(_teamIdKey);
}

void ConfigManager::setRegion(String region) {
    _preferences.putString(_regionKey, region);
    
    // Commit changes
    commit();
    
    updateApiConfigurationState();
}

String ConfigManager::getRegion() {
    if (!_preferences.isKey(_regionKey)) {
        return "us";
    }
    return _preferences.getString(_regionKey);
}

void ConfigManager::clearTeamId() {
    _preferences.remove(_teamIdKey);
    
    // Commit changes
    commit();
    
    SystemController::setApiState(ApiState::API_AWAITING_CONFIG);
}

bool ConfigManager::setApiKey(const String& apiKey) {
    if (apiKey.length() == 0 || apiKey.length() > MAX_API_KEY_LENGTH) {
        SystemController::setApiState(ApiState::API_CONFIG_INVALID);
        return false;
    }

    _preferences.putString(_apiKeyKey, apiKey);
    
    // Commit changes
    commit();
    
    updateApiConfigurationState();
    return true;
}

String ConfigManager::getApiKey() {
    return _preferences.getString(_apiKeyKey, "");
}

void ConfigManager::clearApiKey() {
    _preferences.remove(_apiKeyKey);
    
    // Commit changes
    commit();
    
    SystemController::setApiState(ApiState::API_AWAITING_CONFIG);
}

std::vector<CardConfig> ConfigManager::getCardConfigs() {
    std::vector<CardConfig> configs;
    
    // Check if the key exists first to avoid error logs
    if (!_cardPrefs.isKey("config_list")) {
        return configs; // Return empty vector if no card config stored yet
    }
    
    // Get JSON string from preferences
    String jsonString = _cardPrefs.getString("config_list", "[]");
    
    // Parse JSON
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonString);
    
    if (error) {
        Serial.printf("Failed to parse card configs JSON: %s\n", error.c_str());
        return configs; // Return empty vector on parse error
    }
    
    // Convert JSON array to vector of CardConfig
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant v : array) {
        JsonObject obj = v.as<JsonObject>();
        if (obj.containsKey("type") && obj.containsKey("config") && obj.containsKey("order")) {
            CardConfig config;
            config.type = stringToCardType(obj["type"].as<String>());
            config.config = obj["config"].as<String>();
            config.order = obj["order"].as<int>();
            config.name = obj["name"].as<String>();
            configs.push_back(config);
        }
    }
    
    return configs;
}

bool ConfigManager::saveCardConfigs(const std::vector<CardConfig>& configs) {
    // Create JSON document
    DynamicJsonDocument doc(2048);
    JsonArray array = doc.to<JsonArray>();
    
    // Convert vector to JSON array
    for (const CardConfig& config : configs) {
        JsonObject obj = array.createNestedObject();
        obj["type"] = cardTypeToString(config.type);
        obj["config"] = config.config;
        obj["order"] = config.order;
        obj["name"] = config.name;
    }
    
    // Serialize to string
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        Serial.println("Failed to serialize card configs to JSON");
        return false;
    }
    
    // Save to preferences
    _cardPrefs.putString("config_list", jsonString);
    
    // Commit changes
    commit();
    
    // Publish event if event queue is available
    if (_eventQueue != nullptr) {
        _eventQueue->publishEvent(EventType::CARD_CONFIG_CHANGED, "");
    }
    
    return true;
}