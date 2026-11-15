#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
// #include <WebServer.h> // Removed this include
#include "ConfigManager.h"
#include "EventQueue.h"
#include <functional>
#include <vector> // Ensure vector is included for std::vector

// Forward declarations
class ProvisioningCard;

// WiFi connection states
enum class WiFiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AP_MODE
};

// State change callback type
typedef std::function<void(WiFiState)> WiFiStateCallback;

// Removed NetworkInfo struct from here

class WiFiInterface {
public:
    // Nested NetworkInfo struct definition
    struct NetworkInfo {
        String ssid;
        int32_t rssi;
        uint8_t encryptionType; // Corresponds to wifi_auth_mode_t like WIFI_AUTH_OPEN
        // bool isHidden; // Optional
        // int32_t channel; // Optional
    };

    // Constructor with EventQueue
    WiFiInterface(ConfigManager& configManager, EventQueue& eventQueue);

    // Initialize the WiFi manager
    void begin();

    // Connect to WiFi using stored credentials
    bool connectToStoredNetwork(unsigned long timeout = 30000);

    // Start access point mode for provisioning
    void startAccessPoint();

    // Stop access point mode
    void stopAccessPoint();

    // Process WiFi events (call in loop)
    void process();

    // Get current WiFi state
    WiFiState getState() const;

    // Get current IP address (empty if not connected)
    String getIPAddress() const;

    // Get current SSID when connected as a station
    String getCurrentSsid() const;

    // Check if currently connected to a WiFi network as a station
    bool isConnected() const;

    // Get WiFi signal strength (0-100%, 0 if not connected)
    int getSignalStrength() const;

    // Get access point IP address
    String getAPIPAddress() const;
    
    // Get SSID
    String getSSID() const;

    // Set UI reference for callbacks
    void setUI(ProvisioningCard* ui);
    
    // Register for state changes
    static void onStateChange(WiFiStateCallback callback);
    
    // Set the event queue (if not set in constructor)
    void setEventQueue(EventQueue* queue);
    
    // Handle WiFi credential events
    void handleWiFiCredentialEvent(const Event& event);

    // New methods for network scanning
    void scanNetworks(); // Initiates a WiFi scan
    std::vector<NetworkInfo> getScannedNetworks() const; // Gets the results of the last scan

private:
    // Config manager reference
    ConfigManager& _configManager;
    
    // Event queue reference
    EventQueue* _eventQueue = nullptr;

    // WiFi state
    WiFiState _state;

    // Stored credentials
    String _ssid;
    String _password;

    // AP mode settings
    String _apSSID;
    String _apPassword;
    IPAddress _apIP;

    // DNS server for captive portal
    DNSServer* _dnsServer;
    
    // UI reference for callbacks
    ProvisioningCard* _ui;

    // Last time we checked WiFi status
    unsigned long _lastStatusCheck;
    
    // Connection attempt start time
    unsigned long _connectionStartTime;
    
    // Timeout for connection attempt
    unsigned long _connectionTimeout;

    // Variable to store the number of networks found by the last scan
    int16_t _lastScanResultCount = -1; 
    
    // Flag to indicate we are connecting after portal submission
    bool _attemptingNewConnectionAfterPortal = false;
    unsigned long _apStartedAt = 0;  ///< When the setup hotspot was last started, for retrying the saved network
    
    // WiFi event handlers
    static void onWiFiEvent(WiFiEvent_t event);
    static WiFiInterface* _instance;
    
    // State change callback
    static WiFiStateCallback _stateCallback;
    
    // Update WiFi state and publish event
    void updateState(WiFiState newState);
};