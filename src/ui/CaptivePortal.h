#pragma once

#include <Arduino.h>
#include <vector> // For std::vector

// Attempt to signal ESPAsyncWebServer that WebServer-style definitions are present
// #define _WEBSERVER_H_ // A common guard for WebServer libraries - REMOVING

// Resolve HTTP_DELETE conflict between ESP-IDF and ESPAsyncWebServer
// This might no longer be needed if the above define works, but we'll keep it for now - REMOVING
// #ifdef HTTP_DELETE
// #undef HTTP_DELETE
// #endif

#include <ESPAsyncWebServer.h>
#include "ConfigManager.h"
#include "hardware/WifiInterface.h"
#include "html_portal.h"  // Include generated HTML header
#include "EventQueue.h"   // Include the event queue
#include "config/CardConfig.h"  // Include card configuration structures
// #include "OtaManager.h" // Will be included in .cpp, forward declare here

class OtaManager; // Forward declaration
class CardController; // Forward declaration

// Enum to represent different asynchronous actions the portal can perform
enum class PortalAction {
    NONE,
    SCAN_WIFI,
    SAVE_WIFI,
    SAVE_DEVICE_CONFIG,
    SAVE_INSIGHT,
    DELETE_INSIGHT,
    CHECK_OTA_UPDATE,
    START_OTA_UPDATE
};

// Helper to convert PortalAction to string for JSON (declaration)
const char* portalActionToString(PortalAction action);

/**
 * @class CaptivePortal
 * @brief Web-based configuration interface for device setup
 * 
 * Provides a captive portal interface for:
 * - WiFi network selection and configuration
 * - Device configuration (team ID and API key)
 * - PostHog insight management
 * 
 * Implements standard captive portal detection for Android and Microsoft devices.
 * Caches WiFi scan results to improve responsiveness.
 */
class CaptivePortal {
public:
    /**
     * @brief Constructor
     * @param configManager Reference to configuration storage
     * @param wifiInterface Reference to WiFi management
     * @param eventQueue Reference to event system for state changes
     * @param otaManager Reference to OTA update manager
     * @param cardController Reference to card controller for card definitions
     */
    CaptivePortal(ConfigManager& configManager, WiFiInterface& wifiInterface, EventQueue& eventQueue, OtaManager& otaManager, CardController& cardController);

    /**
     * @brief Initialize the portal
     * 
     * Sets up HTTP routes and starts the web server on port 80.
     * Performs initial WiFi network scan.
     */
    void begin();

    /**
     * @brief Process any pending asynchronous operations.
     * This should be called periodically from a background task.
     */
    void processAsyncOperations();

private:
    AsyncWebServer _server;              ///< Web server instance on port 80
    ConfigManager& _configManager;   ///< Configuration storage reference
    WiFiInterface& _wifiInterface;   ///< WiFi management reference
    EventQueue& _eventQueue;         ///< Event system reference
    String _cachedNetworks;         ///< Cached JSON of available networks
    unsigned long _lastScanTime;     ///< Timestamp of last WiFi scan
    OtaManager& _otaManager;         ///< OTA Update Manager reference
    CardController& _cardController; ///< Card controller reference

    // Action queue structure (internal)
    struct QueuedAction {
        PortalAction action;
        String param1;
        String param2;
    };

    // Max size for the action queue
    static const size_t MAX_ACTION_QUEUE_SIZE = 5;

    // Member variables for asynchronous action handling
    std::vector<QueuedAction> _action_queue; // Action queue
    PortalAction _action_in_progress;
    PortalAction _last_action_completed;
    bool _last_action_was_success;
    String _last_action_message;

    /**
     * @brief Serve the main portal page
     * Sends static HTML from html_portal.h
     */
    void handleRoot(AsyncWebServerRequest *request);
    
    /**
     * @brief Handle network scan request
     * Returns cached results and triggers new scan if >10s old
     */
    void handleScanNetworks(AsyncWebServerRequest *request);
    
    /**
     * @brief Handle WiFi credentials submission
     * Accepts POST with ssid/password, attempts connection
     */
    void handleSaveWifi(AsyncWebServerRequest *request);

    /**
     * @brief Return current device configuration
     * Returns team ID and truncated API key
     */
    void handleGetDeviceConfig(AsyncWebServerRequest *request);

    /**
     * @brief Handle device configuration updates
     * Accepts team ID and API key updates
     */
    void handleSaveDeviceConfig(AsyncWebServerRequest *request);

    /**
     * @brief Return list of configured insights
     * Returns JSON array of insight IDs and titles
     */
    void handleGetInsights(AsyncWebServerRequest *request);

    /**
     * @brief Handle new insight creation
     * Accepts insight ID, publishes INSIGHT_ADDED event
     */
    void handleSaveInsight(AsyncWebServerRequest *request);

    /**
     * @brief Handle insight deletion
     * Accepts JSON with insight ID, publishes INSIGHT_DELETED event
     */
    void handleDeleteInsight(AsyncWebServerRequest *request);

    /**
     * @brief Return list of available card types
     * Returns JSON array of CardDefinition objects
     */
    void handleGetCardDefinitions(AsyncWebServerRequest *request);

    /**
     * @brief Return current card configuration
     * Returns JSON array of CardConfig objects
     */
    void handleGetConfiguredCards(AsyncWebServerRequest *request);

    /**
     * @brief Handle card configuration updates
     * Accepts JSON array of CardConfig objects
     */
    void handleSaveConfiguredCards(AsyncWebServerRequest *request);

    /**
     * @brief Handle captive portal detection
     * Redirects to setup page for Android/Microsoft detection
     */
    void handleCaptivePortal(AsyncWebServerRequest *request);

    /**
     * @brief Handle 404 errors
     * Redirects all unknown URLs to setup page
     */
    void handle404(AsyncWebServerRequest *request);

    /**
     * @brief Scan and format available networks
     * @return JSON string of networks sorted by signal strength
     */
    String getNetworksJson();

    /**
     * @brief Perform WiFi network scan
     * Updates _cachedNetworks and _lastScanTime
     */
    void performWiFiScan();

    /**
     * @brief Handle request to check for firmware updates.
     */
    void handleCheckUpdate(AsyncWebServerRequest *request);

    /**
     * @brief Handle request to start a firmware update.
     */
    void handleStartUpdate(AsyncWebServerRequest *request);

    /**
     * @brief Handle request for OTA update status.
     */
    void handleUpdateStatus(AsyncWebServerRequest *request);

    void handleCorsPreflight(AsyncWebServerRequest *request); // Added declaration for CORS preflight handler

    // New handlers for async action requests and status
    void handleApiStatus(AsyncWebServerRequest *request);
    void handleRequestWifiScan(AsyncWebServerRequest *request);
    void handleRequestSaveWifi(AsyncWebServerRequest *request);
    void handleRequestSaveDeviceConfig(AsyncWebServerRequest *request);
    void handleRequestSaveInsight(AsyncWebServerRequest *request);
    void handleRequestDeleteInsight(AsyncWebServerRequest *request);
    void handleRequestCheckOtaUpdate(AsyncWebServerRequest *request);
    void handleRequestStartOtaUpdate(AsyncWebServerRequest *request);

    /**
     * @brief Common handler to queue an action and store parameters.
     * @param action The PortalAction to queue.
     * @param request The incoming AsyncWebServerRequest, used to extract parameters.
     */
    void requestAction(PortalAction action, AsyncWebServerRequest *request);
};