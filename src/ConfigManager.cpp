#include "ConfigManager.h"
#include "SystemController.h"

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
    
    _preferences.begin(_namespace, false);
    _insightsPrefs.begin(_insightsNamespace, false);
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

bool ConfigManager::saveInsight(const String& id, const String& title) {
    if (id.length() == 0 || id.length() > MAX_INSIGHT_ID_LENGTH) {
        return false;
    }
    // Store the insight content
    _insightsPrefs.putString(id.c_str(), title);

    // Update the ID list
    auto ids = getAllInsightIds();
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
        ids.push_back(id);
        updateIdList(ids);
    }
    
    // Commit changes
    commit();

    return true;
}

String ConfigManager::getInsight(const String& id) {
    return _insightsPrefs.getString(id.c_str(), "");
}

void ConfigManager::deleteInsight(const String& id) {
    if (_insightsPrefs.isKey(id.c_str())) {
        _insightsPrefs.remove(id.c_str());
        
        // Update the ID list
        auto ids = getAllInsightIds();
        ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
        updateIdList(ids);
        
        // Commit changes
        commit();
    }
}

std::vector<String> ConfigManager::getAllInsightIds() {
    std::vector<String> ids;
    
    // We'll maintain a special key that stores all insight IDs
    String idList = _insightsPrefs.getString("_id_list", "");
    
    if (idList.length() > 0) {
        // Split the ID list on commas
        int start = 0;
        int end = idList.indexOf(',');
        while (end >= 0) {
            ids.push_back(idList.substring(start, end));
            start = end + 1;
            end = idList.indexOf(',', start);
        }
        // Add the last ID if there is one
        if (start < idList.length()) {
            ids.push_back(idList.substring(start));
        }
    }
    
    return ids;
}

void ConfigManager::updateIdList(const std::vector<String>& ids) {
    String idList;
    for (size_t i = 0; i < ids.size(); i++) {
        idList += ids[i];
        if (i < ids.size() - 1) {
            idList += ",";
        }
    }
    _insightsPrefs.putString("_id_list", idList);
    
    // Commit changes
    commit();
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

bool ConfigManager::setBaseUrl(const String& baseUrl) {
    if (baseUrl.length() == 0 || baseUrl.length() > MAX_BASE_URL_LENGTH) {
        SystemController::setApiState(ApiState::API_CONFIG_INVALID);
        return false;
    }

    if (!baseUrl.endsWith(PROJECTS_ENDPOINT)) {
        SystemController::setApiState(ApiState::API_CONFIG_INVALID);
        return false;
    }

    _preferences.putString(_baseUrlKey, baseUrl);
    
    // Commit changes
    commit();
    
    updateApiConfigurationState();
    return true;
}

String ConfigManager::getBaseUrl() {
    String baseUrl = String(DEFAULT_BASE_URL) + String(PROJECTS_ENDPOINT);   
    if (!_preferences.isKey(_baseUrlKey)) {
        return baseUrl;
    }
    return _preferences.getString(_baseUrlKey, baseUrl);
}

void ConfigManager::clearBaseUrl() {
    _preferences.remove(_baseUrlKey);
    
    // Commit changes
    commit();
    
    SystemController::setApiState(ApiState::API_AWAITING_CONFIG);
}