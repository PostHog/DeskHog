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
    _cardPrefs.begin(_cardNamespace, false);
    _servicePrefs.begin(_serviceNamespace, false);
    
}


// Helper method to commit changes to flash
void ConfigManager::commit() {
    _preferences.end();
    _cardPrefs.end();
    _servicePrefs.end();
    
    _preferences.begin(_namespace, false);
    _cardPrefs.begin(_cardNamespace, false);
    _servicePrefs.begin(_serviceNamespace, false);
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







std::vector<CardConfig> ConfigManager::getCardConfigs() {
    std::vector<CardConfig> configs;
    
    // Check if the key exists first to avoid error logs
    if (!_cardPrefs.isKey("config_list")) {
        return configs; // Return empty vector if no card config stored yet
    }
    
    // Get JSON string from preferences
    String jsonString = _cardPrefs.getString("config_list", "[]");
    
    // Parse JSON with larger size for dynamic configs
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, jsonString);
    
    if (error) {
        Serial.printf("Failed to parse card configs JSON: %s\n", error.c_str());
        return configs; // Return empty vector on parse error
    }
    
    // Convert JSON array to vector of CardConfig
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant v : array) {
        JsonObject obj = v.as<JsonObject>();
        if (obj.containsKey("type") && obj.containsKey("order")) {
            CardConfig config;
            config.type = stringToCardType(obj["type"].as<String>());
            config.order = obj["order"].as<int>();
            config.name = obj["name"].as<String>();
            
            // Handle dynamic configuration loading
            if (obj.containsKey("configMap") && obj["configMap"].is<JsonObject>()) {
                JsonObject configMap = obj["configMap"].as<JsonObject>();
                for (JsonPair kv : configMap) {
                    config.setConfig(String(kv.key().c_str()), kv.value().as<String>());
                }
            }
            
            configs.push_back(config);
        }
    }
    
    return configs;
}

bool ConfigManager::saveCardConfigs(const std::vector<CardConfig>& configs) {
    // Create JSON document with larger size for dynamic configs
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();
    
    // Convert vector to JSON array
    for (const CardConfig& config : configs) {
        JsonObject obj = array.createNestedObject();
        obj["type"] = cardTypeToString(config.type);
        obj["order"] = config.order;
        obj["name"] = config.name;
        
        // Handle dynamic configuration storage
        if (config.config.empty()) {
            obj["config"] = "";  // No configuration
        } else {
            auto legacyIt = config.config.find("legacy");
            if (config.config.size() == 1 && legacyIt != config.config.end()) {
                // Legacy single configuration value
                obj["config"] = legacyIt->second;
            } else {
                // Dynamic configuration - store as nested object
                JsonObject configObj = obj.createNestedObject("configMap");
                for (const auto& pair : config.config) {
                    configObj[pair.first] = pair.second;
                }
                // Also store first value as legacy config for backward compatibility
                obj["config"] = config.config.empty() ? "" : config.config.begin()->second;
            }
        }
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

std::vector<ServiceConfig> ConfigManager::getServiceConfigs() {
    std::vector<ServiceConfig> configs;
    
    // Check if the key exists first to avoid error logs
    if (!_servicePrefs.isKey("service_list")) {
        return configs; // Return empty vector if no service config stored yet
    }
    
    // Get JSON string from preferences
    String jsonString = _servicePrefs.getString("service_list", "[]");
    
    // Parse JSON with larger size for dynamic configs
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, jsonString);
    
    if (error) {
        Serial.printf("Failed to parse service configs JSON: %s\n", error.c_str());
        return configs; // Return empty vector on parse error
    }
    
    // Convert JSON array to vector of ServiceConfig
    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant v : array) {
        JsonObject obj = v.as<JsonObject>();
        if (obj.containsKey("type")) {
            ServiceConfig config;
            config.type = stringToServiceType(obj["type"].as<String>());
            
            // Handle dynamic configuration loading
            if (obj.containsKey("config") && obj["config"].is<JsonObject>()) {
                JsonObject configObj = obj["config"].as<JsonObject>();
                for (JsonPair kv : configObj) {
                    config.setConfig(String(kv.key().c_str()), kv.value().as<String>());
                }
            }
            
            configs.push_back(config);
        }
    }
    
    return configs;
}

bool ConfigManager::saveServiceConfigs(const std::vector<ServiceConfig>& configs) {
    // Create JSON document with larger size for dynamic configs
    DynamicJsonDocument doc(4096);
    JsonArray array = doc.to<JsonArray>();
    
    // Convert vector to JSON array
    for (const ServiceConfig& config : configs) {
        JsonObject obj = array.createNestedObject();
        obj["type"] = serviceTypeToString(config.type);
        
        // Handle dynamic configuration storage
        if (!config.config.empty()) {
            JsonObject configObj = obj.createNestedObject("config");
            for (const auto& pair : config.config) {
                configObj[pair.first] = pair.second;
            }
        }
    }
    
    // Serialize to string
    String jsonString;
    if (serializeJson(doc, jsonString) == 0) {
        Serial.println("Failed to serialize service configs to JSON");
        return false;
    }
    
    // Save to preferences
    _servicePrefs.putString("service_list", jsonString);
    
    // Commit changes
    commit();
    
    // Publish event if event queue is available
    if (_eventQueue != nullptr) {
        _eventQueue->publishEvent(EventType::SERVICE_CONFIG_CHANGED, "");
    }
    
    return true;
}

ServiceConfig ConfigManager::getServiceConfig(ServiceType type) {
    std::vector<ServiceConfig> configs = getServiceConfigs();
    for (const ServiceConfig& config : configs) {
        if (config.type == type) {
            return config;
        }
    }
    
    // Return empty config if not found
    ServiceConfig empty;
    empty.type = type;
    return empty;
}