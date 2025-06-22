#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "EventQueue.h"
#include "config/CardConfig.h"
#include "config/ServiceConfig.h"

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

    /**
     * @brief Get all configured services from persistent storage
     * @return Vector of ServiceConfig objects representing configured services
     */
    std::vector<ServiceConfig> getServiceConfigs();

    /**
     * @brief Save service configurations to persistent storage
     * @param configs Vector of ServiceConfig objects to save
     * @return true if saved successfully, false otherwise
     */
    bool saveServiceConfigs(const std::vector<ServiceConfig>& configs);

    /**
     * @brief Get configuration for a specific service type
     * @param type The service type to retrieve
     * @return ServiceConfig for the requested type, or disabled config if not found
     */
    ServiceConfig getServiceConfig(ServiceType type);

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
    Preferences _servicePrefs;    ///< Separate storage for service configurations

    // Namespace constants for preferences organization
    const char* _namespace = "wifi_config";        ///< Namespace for WiFi and general config
    const char* _cardNamespace = "cards";          ///< Namespace for card configurations
    const char* _serviceNamespace = "services";    ///< Namespace for service configurations

    // Storage keys for WiFi configuration
    const char* _ssidKey = "ssid";                ///< Key for stored WiFi SSID
    const char* _passwordKey = "password";         ///< Key for stored WiFi password
    const char* _hasCredentialsKey = "has_creds"; ///< Key for WiFi credentials presence flag



    // Storage size limits
    /** @brief Maximum length for WiFi SSID (per IEEE 802.11 spec) */
    static const size_t MAX_SSID_LENGTH = 32;
    /** @brief Maximum length for WiFi password (per WPA2 spec) */
    static const size_t MAX_PASSWORD_LENGTH = 64;
    /** @brief Maximum length for insight configuration data */
    static const size_t MAX_INSIGHT_LENGTH = 1024;
    /** @brief Maximum length for insight identifier */
    static const size_t MAX_INSIGHT_ID_LENGTH = 64;

    // Event system
    EventQueue* _eventQueue = nullptr;  ///< Optional event queue for state notifications
};