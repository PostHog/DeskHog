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

    // PostHog used to be built into the firmware, with its settings stored here.
    // It is a plugin now. These remain only so that settings saved by older
    // firmware can be moved over to the plugin once.

    /** @return the PostHog project (team) ID older firmware saved, or NO_TEAM_ID */
    int getTeamId();
    /** @return the PostHog region older firmware saved ("us" if none) */
    String getRegion();
    /** @return the PostHog API key older firmware saved, or "" */
    String getApiKey();
    /** Forget the PostHog settings older firmware saved. */
    void clearPostHogSettings();


    /**
     * @brief Get all configured cards from persistent storage
     * @return Vector of CardConfig objects representing enabled cards
     */
    /** Where to look for plugins to install, if the user chose somewhere else than the default. */
    String getPluginDirectory();
    void setPluginDirectory(const String& source);
    /**
     * The device's time zone: a POSIX TZ string such as "CET-1CEST,M3.5.0,M10.5.0/3",
     * and the name it was picked by on the web page ("Europe/Brussels"). Both are ""
     * until someone picks one, which means UTC.
     */
    String getTimeZone();
    String getTimeZoneName();
    void setTimeZone(const String& posix, const String& name);
    std::vector<CardConfig> getCardConfigs();

    /**
     * @brief Save card configurations to persistent storage
     * @param configs Vector of CardConfig objects to save
     * @return true if saved successfully, false otherwise
     */
    bool saveCardConfigs(const std::vector<CardConfig>& configs);

private:

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
    Preferences _cardPrefs;       ///< Separate storage for card configurations

    // Namespace constants for preferences organization
    const char* _namespace = "wifi_config";        ///< Namespace for WiFi and general config
    const char* _cardNamespace = "cards";          ///< Namespace for card configurations

    // Storage keys for WiFi configuration
    const char* _ssidKey = "ssid";                ///< Key for stored WiFi SSID
    const char* _passwordKey = "password";         ///< Key for stored WiFi password
    const char* _hasCredentialsKey = "has_creds"; ///< Key for WiFi credentials presence flag

    // Storage keys for API configuration
    const char* _teamIdKey = "team_id";           ///< Key for stored team ID
    const char* _apiKeyKey = "api_key";           ///< Key for stored API key
    const char* _regionKey = "region";           ///< Key for stored region


    // Storage size limits
    /** @brief Maximum length for WiFi SSID (per IEEE 802.11 spec) */
    static const size_t MAX_SSID_LENGTH = 32;
    /** @brief Maximum length for WiFi password (per WPA2 spec) */
    static const size_t MAX_PASSWORD_LENGTH = 64;
    /** @brief Maximum length for insight configuration data */
    /** @brief Maximum length for API key */
    /** @brief Maximum length for insight identifier */

    // Event system
    EventQueue* _eventQueue = nullptr;  ///< Optional event queue for state notifications
};