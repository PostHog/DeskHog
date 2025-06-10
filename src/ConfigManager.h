#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "EventQueue.h"
#include "config/CardConfig.h"

/**
 * @class ConfigManager
 * @brief Manages persistent configuration storage for the device
 * 
 * Features:
 * - Secure storage of WiFi credentials
 * - PostHog API configuration (team ID and API key)
 * - Insight configuration management
 * - Event-based state change notifications
 * - Thread-safe operations
 * 
 * Uses ESP32's non-volatile storage (NVS) through Preferences library
 * with size limits enforced for all stored values.
 */
class ConfigManager {
public:
    static const int NO_TEAM_ID = -1;  // Sentinel value for no team ID

    /**
     * @brief Default constructor
     */
    ConfigManager();

    /**
     * @brief Constructor with event queue integration
     * @param eventQueue Reference to the event queue for state change notifications
     */
    ConfigManager(EventQueue& eventQueue);

    /**
     * @brief Initialize the configuration system
     */
    void begin();

    /**
     * @brief Set the event queue for state change notifications
     * @param queue Pointer to the event queue
     */
    void setEventQueue(EventQueue* queue);

    /**
     * @brief Store WiFi credentials in persistent storage
     * @param ssid Network SSID
     * @param password Network password
     * @return true if saved successfully, false otherwise
     */
    bool saveWiFiCredentials(const String& ssid, const String& password);

    /**
     * @brief Retrieve stored WiFi credentials
     * @param ssid Reference to store the SSID
     * @param password Reference to store the password
     * @return true if credentials exist and were retrieved, false otherwise
     */
    bool getWiFiCredentials(String& ssid, String& password);

    /**
     * @brief Remove stored WiFi credentials
     */
    void clearWiFiCredentials();

    /**
     * @brief Check if WiFi credentials are stored
     * @return true if credentials exist, false otherwise
     */
    bool hasWiFiCredentials();

    /**
     * @brief Check WiFi credentials and publish status event
     * @return true if credentials exist, false otherwise
     */
    bool checkWiFiCredentialsAndPublish();

    /**
     * @brief Store team identifier
     * @param teamId The team identifier to store
     */
    void setTeamId(int teamId);

    /**
     * @brief Retrieve stored team identifier
     * @return The team ID or NO_TEAM_ID if not set
     */
    int getTeamId();

    /**
     * @brief Remove stored team identifier
     */
    void clearTeamId();

    /**
     * @brief Store API key
     * @param apiKey The API key to store
     * @return true if saved successfully, false otherwise
     */
    bool setApiKey(const String& apiKey);

    /**
     * @brief Retrieve stored API key
     * @return The API key or empty string if not set
     */
    String getApiKey();

    /**
     * @brief Remove stored API key
     */
    void clearApiKey();

    /**
     * @brief Store insight configuration
     * @param id Unique insight identifier
     * @param title Insight title/configuration
     * @return true if saved successfully, false otherwise
     */
    bool saveInsight(const String& id, const String& title);

    /**
     * @brief Retrieve stored insight configuration
     * @param id Insight identifier to retrieve
     * @return The insight configuration or empty string if not found
     */
    String getInsight(const String& id);

    /**
     * @brief Remove stored insight configuration
     * @param id Insight identifier to remove
     */
    void deleteInsight(const String& id);

    /**
     * @brief Get all stored insight identifiers
     * @return Vector of insight IDs
     */
    std::vector<String> getAllInsightIds();

    /**
     * @brief Get all configured cards from persistent storage
     * @return Vector of CardConfig objects representing enabled cards
     */
    std::vector<CardConfig> getCardConfigs();

    /**
     * @brief Save card configurations to persistent storage
     * @param configs Vector of CardConfig objects to save
     * @return true if saved successfully, false otherwise
     */
    bool saveCardConfigs(const std::vector<CardConfig>& configs);

private:
    /**
     * @brief Updates the internal list of insight IDs in preferences
     * 
     * Maintains a special "_id_list" key in preferences that stores all insight IDs
     * as a comma-separated string. This list is used by getAllInsightIds() to track
     * which insights are currently stored.
     * 
     * @param ids Vector of insight IDs to store
     * @note Calls commit() after updating the list
     */
    void updateIdList(const std::vector<String>& ids);
    
    /**
     * @brief Validates and updates the API configuration state
     * 
     * Checks if both team ID and API key are properly set in preferences.
     * Updates the system state via SystemController to one of:
     * - API_AWAITING_CONFIG: if either team ID or API key is missing
     * - API_CONFIGURED: if both team ID and API key are properly set
     */
    void updateApiConfigurationState();

    /**
     * @brief Ensures preferences changes are persisted to flash
     * 
     * Closes and reopens both preference namespaces to ensure changes
     * are written to flash storage. Required after any preference modifications
     * to ensure changes survive power cycles.
     */
    void commit();

    // Preferences instances for persistent storage
    Preferences _preferences;      ///< Main preferences storage instance
    Preferences _insightsPrefs;   ///< Separate storage for insight data
    Preferences _cardPrefs;       ///< Separate storage for card configurations

    // Namespace constants for preferences organization
    const char* _namespace = "wifi_config";        ///< Namespace for WiFi and general config
    const char* _insightsNamespace = "insights";   ///< Namespace for insight data
    const char* _cardNamespace = "cards";          ///< Namespace for card configurations

    // Storage keys for WiFi configuration
    const char* _ssidKey = "ssid";                ///< Key for stored WiFi SSID
    const char* _passwordKey = "password";         ///< Key for stored WiFi password
    const char* _hasCredentialsKey = "has_creds"; ///< Key for WiFi credentials presence flag

    // Storage keys for API configuration
    const char* _teamIdKey = "team_id";           ///< Key for stored team ID
    const char* _apiKeyKey = "api_key";           ///< Key for stored API key

    // Storage size limits
    /** @brief Maximum length for WiFi SSID (per IEEE 802.11 spec) */
    static const size_t MAX_SSID_LENGTH = 32;
    /** @brief Maximum length for WiFi password (per WPA2 spec) */
    static const size_t MAX_PASSWORD_LENGTH = 64;
    /** @brief Maximum length for insight configuration data */
    static const size_t MAX_INSIGHT_LENGTH = 1024;
    /** @brief Maximum length for API key */
    static const size_t MAX_API_KEY_LENGTH = 64;
    /** @brief Maximum length for insight identifier */
    static const size_t MAX_INSIGHT_ID_LENGTH = 64;

    // Event system
    EventQueue* _eventQueue = nullptr;  ///< Optional event queue for state notifications
};