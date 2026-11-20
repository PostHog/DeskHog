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

class PluginManager; // Forward declaration

// Enum to represent different asynchronous actions the portal can perform
enum class PortalAction {
    NONE,
    SCAN_WIFI,
    SAVE_WIFI
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
 * - Plugins, connections and cards
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
     * @param pluginManager Reference to the Lua plugin manager
     */
    CaptivePortal(ConfigManager& configManager, WiFiInterface& wifiInterface, EventQueue& eventQueue, PluginManager& pluginManager);

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
    PluginManager& _pluginManager;   ///< Lua plugin manager reference

    // Action queue structure (internal)
    struct QueuedAction {
        PortalAction action;
        String param1;
        String param2;
        String param3;
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


    // New handlers for async action requests and status
    void handleApiStatus(AsyncWebServerRequest *request);
    void handleRequestWifiScan(AsyncWebServerRequest *request);
    void handleRequestSaveWifi(AsyncWebServerRequest *request);

    /**
     * @brief Common handler to queue an action and store parameters.
     * @param action The PortalAction to queue.
     * @param request The incoming AsyncWebServerRequest, used to extract parameters.
     */
    void requestAction(PortalAction action, AsyncWebServerRequest *request);
};