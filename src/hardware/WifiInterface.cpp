#include "WifiInterface.h"
#include "ui/ProvisioningCard.h"

WiFiInterface* WiFiInterface::_instance = nullptr;
WiFiStateCallback WiFiInterface::_stateCallback = nullptr;



WiFiInterface::WiFiInterface(ConfigManager& configManager, EventQueue& eventQueue)
    : _configManager(configManager),
      _eventQueue(&eventQueue),
      _state(WiFiState::DISCONNECTED),
      _apSSID("DeskHog"),
      _apPassword(""),
      _apIP(192, 168, 4, 1),
      _dnsServer(nullptr),
      _ui(nullptr),
      _lastStatusCheck(0),
      _connectionStartTime(0),
      _connectionTimeout(0),
      _attemptingNewConnectionAfterPortal(false) {
    
    _instance = this;
}

void WiFiInterface::setEventQueue(EventQueue* queue) {
    _eventQueue = queue;
}

void WiFiInterface::handleWiFiCredentialEvent(const Event& event) {
    if (event.type == EventType::WIFI_CREDENTIALS_FOUND) {
        Serial.println("WiFi credentials found event received, preparing to connect and stop AP on success.");
        _attemptingNewConnectionAfterPortal = true; // Set flag
        connectToStoredNetwork(30000); // 30 second timeout
    } else if (event.type == EventType::NEED_WIFI_CREDENTIALS) {
        Serial.println("Need WiFi credentials event received");
        startAccessPoint();
    }
}

void WiFiInterface::updateState(WiFiState newState) {
    if (_state == newState) return; // No change
    
    _state = newState;
    
    // Trigger legacy callback for backward compatibility
    if (_stateCallback) {
        _stateCallback(_state);
    }
    
    // Publish appropriate event
    if (_eventQueue != nullptr) {
        switch (newState) {
            case WiFiState::CONNECTING:
                _eventQueue->publishEvent(EventType::WIFI_CONNECTING, "");
                break;
            case WiFiState::CONNECTED:
                _eventQueue->publishEvent(EventType::WIFI_CONNECTED, "");
                break;
            case WiFiState::DISCONNECTED:
                if (!_configManager.hasWiFiCredentials()) {
                    _eventQueue->publishEvent(EventType::NEED_WIFI_CREDENTIALS, "");
                }
                break;
            case WiFiState::AP_MODE:
                _eventQueue->publishEvent(EventType::WIFI_AP_STARTED, "");
                break;
        }
    }
}

void WiFiInterface::onStateChange(WiFiStateCallback callback) {
    _stateCallback = callback;
    // Call immediately with current state if we have an instance
    if (_instance) {
        callback(_instance->_state);
    }
}

void WiFiInterface::begin() {
    // Set up WiFi event handlers
    WiFi.onEvent(onWiFiEvent);
    
    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    
    // Enable WiFi power save mode
    // WIFI_PS_MIN_MODEM: Minimum modem power saving mode (recommended for most applications)
    // This reduces power consumption when WiFi is idle but maintains connectivity
    WiFi.setSleep(WIFI_PS_MIN_MODEM);
    Serial.println("WiFi power save mode enabled (MIN_MODEM)");
    
    // Subscribe to WiFi credential events if event queue is available
    if (_eventQueue != nullptr) {
        _eventQueue->subscribe([this](const Event& event) {
            if (event.type == EventType::WIFI_CREDENTIALS_FOUND || 
                event.type == EventType::NEED_WIFI_CREDENTIALS) {
                this->handleWiFiCredentialEvent(event);
            }
        });
    }
}

bool WiFiInterface::connectToStoredNetwork(unsigned long timeout) {
    if (!_configManager.getWiFiCredentials(_ssid, _password)) {
        return false;
    }
    
    Serial.print("Connecting to WiFi: ");
    Serial.println(_ssid);
    
    updateState(WiFiState::CONNECTING);
    _connectionStartTime = millis();
    _connectionTimeout = timeout;
    
    if (_ui) {
        _ui->updateConnectionStatus("Connecting");
    }
    
    // Start connection attempt
    WiFi.begin(_ssid.c_str(), _password.c_str());
    
    return true;
}

void WiFiInterface::startAccessPoint() {
    WiFi.mode(WIFI_AP);
    
    // Create unique AP SSID based on MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    _apSSID = "DeskHog_" + String(mac[2], HEX) + String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
    
    // Start access point
    WiFi.softAPConfig(_apIP, _apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(_apSSID.c_str(), _apPassword.c_str());
    
    // Start DNS server for captive portal
    if (!_dnsServer) {
        _dnsServer = new DNSServer();
        if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
            _dnsServer->start(53, "*", _apIP);
        }
    }
    
    updateState(WiFiState::AP_MODE);
    
    if (_ui) {
        _ui->showQRCode();
    }
    
    Serial.print("AP started with SSID: ");
    Serial.println(_apSSID);
    Serial.print("AP IP address: ");
    Serial.println(getAPIPAddress());
}

void WiFiInterface::stopAccessPoint() {
    Serial.println("WiFiInterface: Stopping Access Point components...");
    if (_dnsServer) {
        _dnsServer->stop();
        delete _dnsServer;
        _dnsServer = nullptr;
        Serial.println("WiFiInterface: DNS server stopped.");
    }
    
    if (WiFi.softAPgetStationNum() > 0 || WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        if (WiFi.softAPdisconnect(true)) {
            Serial.println("WiFiInterface: Soft AP disconnected (softAPdisconnect returned true).");
        } else {
            Serial.println("WiFiInterface: softAPdisconnect returned false (might mean AP was not active or no stations were connected).");
        }
    } else {
        Serial.println("WiFiInterface: Soft AP was not considered active (no stations or not in AP/AP_STA mode).");
    }

    if (WiFi.getMode() == WIFI_AP_STA) {
        Serial.println("WiFiInterface: Mode was AP_STA, setting to WIFI_STA.");
        WiFi.mode(WIFI_STA);
    } else if (WiFi.getMode() == WIFI_AP) {
        Serial.println("WiFiInterface: Mode was AP, setting to WIFI_STA.");
        WiFi.mode(WIFI_STA);
    }
    Serial.println("WiFiInterface: Access Point components stopped.");
}

void WiFiInterface::process() {
    if (_state == WiFiState::AP_MODE && _dnsServer) {
        _dnsServer->processNextRequest();
    }
    
    if (_state == WiFiState::CONNECTING) {
        unsigned long now = millis();
        
        if (now - _connectionStartTime >= _connectionTimeout) {
            Serial.println("WiFi connection timeout");
            WiFi.disconnect();
            updateState(WiFiState::DISCONNECTED);
            
            if (_ui) {
                _ui->updateConnectionStatus("Connection failed: timeout");
            }
            
            if (_eventQueue != nullptr) {
                _eventQueue->publishEvent(EventType::WIFI_CONNECTION_FAILED, "");
            }
            
            startAccessPoint();
        }
    }
    
    if (_state == WiFiState::CONNECTED && millis() - _lastStatusCheck > 5000) {
        _lastStatusCheck = millis();
        
        if (_ui) {
            _ui->updateSignalStrength(getSignalStrength());
        }
    }

    if (_attemptingNewConnectionAfterPortal) {
        Serial.println("STA successfully connected after portal config, stopping AP.");
        stopAccessPoint();
        _attemptingNewConnectionAfterPortal = false; // Reset flag
    }
}

WiFiState WiFiInterface::getState() const {
    return _state;
}

String WiFiInterface::getIPAddress() const {
    if (_state == WiFiState::CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "";
}

int WiFiInterface::getSignalStrength() const {
    if (_state == WiFiState::CONNECTED) {
        // Convert RSSI to percentage (typical range: -100 to -30)
        int rssi = WiFi.RSSI();
        if (rssi <= -100) {
            return 0;
        } else if (rssi >= -50) {
            return 100;
        } else {
            return 2 * (rssi + 100);
        }
    }
    return 0;
}

String WiFiInterface::getAPIPAddress() const {
    if (_state == WiFiState::AP_MODE) {
        return WiFi.softAPIP().toString();
    }
    return "";
}

String WiFiInterface::getSSID() const {
    if (_state == WiFiState::CONNECTED) {
        return _ssid;
    } else if (_state == WiFiState::AP_MODE) {
        return _apSSID;
    }
    return "";
}

void WiFiInterface::setUI(ProvisioningCard* ui) {
    _ui = ui;
}

void WiFiInterface::onWiFiEvent(WiFiEvent_t event) {
    if (!_instance) return;
    
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("WiFi connected");
            _instance->updateState(WiFiState::CONNECTED);
            break;
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.print("WiFi connected, IP address: ");
            Serial.println(WiFi.localIP());
            if (_instance->_ui) {
                _instance->_ui->updateConnectionStatus("Connected");
                _instance->_ui->updateIPAddress(WiFi.localIP().toString());
            }

            if (_instance->_attemptingNewConnectionAfterPortal) {
                Serial.println("STA successfully connected after portal config, stopping AP.");
                _instance->stopAccessPoint();
                _instance->_attemptingNewConnectionAfterPortal = false; // Reset flag
            }
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("WiFi disconnected");
            if (_instance->_state == WiFiState::CONNECTED) {
                _instance->updateState(WiFiState::DISCONNECTED);
                
                if (_instance->_ui) {
                    _instance->_ui->updateConnectionStatus("Disconnected");
                }
            }
            break;
            
        default:
            break;
    }
}

// New method implementations for network scanning
void WiFiInterface::scanNetworks() {
    Serial.println("WiFiInterface: Starting WiFi scan...");
    // WiFi.scanNetworks will return the number of networks found, or -1 for failure, -2 if scan is already in progress.
    // Using a blocking scan here.
    // The true parameter for show_hidden is often useful.
    _lastScanResultCount = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true);
    
    if (_lastScanResultCount == WIFI_SCAN_FAILED) { // WIFI_SCAN_FAILED is typically -1
        Serial.println("WiFiInterface: Scan failed to start or failed during execution.");
    } else if (_lastScanResultCount == WIFI_SCAN_RUNNING) { // WIFI_SCAN_RUNNING is typically -2
        Serial.println("WiFiInterface: Scan already in progress (should not happen with blocking scan, but good to check).");
        // If this happens with a blocking scan, it's unexpected. You might not want to overwrite _lastScanResultCount.
    } else if (_lastScanResultCount == 0) {
        Serial.println("WiFiInterface: Scan complete. No networks found.");
    } else {
        Serial.printf("WiFiInterface: Scan complete. %d networks found.\n", _lastScanResultCount);
    }
}

std::vector<WiFiInterface::NetworkInfo> WiFiInterface::getScannedNetworks() const {
    std::vector<WiFiInterface::NetworkInfo> networks;
    if (_lastScanResultCount <= 0) { // No networks found or scan hasn't run/failed
        Serial.println("WiFiInterface::getScannedNetworks: No scan results to return.");
        return networks; // Return empty vector
    }

    networks.reserve(_lastScanResultCount); // Pre-allocate memory for efficiency

    for (int16_t i = 0; i < _lastScanResultCount; ++i) {
        WiFiInterface::NetworkInfo net; // Explicitly scope NetworkInfo
        net.ssid = WiFi.SSID(i);
        net.rssi = WiFi.RSSI(i);
        net.encryptionType = WiFi.encryptionType(i); // This is wifi_auth_mode_t
        // Example of how you might get other info if added to struct:
        // net.channel = WiFi.channel(i);
        // net.isHidden = WiFi.isHidden(i); // Note: isHidden() might not be standard on all ESP WiFi libraries
        networks.push_back(net);
    }
    // It's often good practice to delete the scan results to free memory if they are not needed again soon.
    // However, if another part of the UI might refresh the list without an immediate rescan, you might not delete.
    // WiFi.scanDelete(); 
    // If you do delete, you should probably reset _lastScanResultCount:
    // const_cast<WiFiInterface*>(this)->_lastScanResultCount = 0; // Reset if scan results are deleted.

    return networks;
}

// Implementation for getCurrentSsid
String WiFiInterface::getCurrentSsid() const {
    if (_state == WiFiState::CONNECTED) {
        return WiFi.SSID(); // WiFi.SSID() returns the SSID of the current network connection
    }
    return String(); // Return empty string if not connected
}

// Implementation for isConnected
bool WiFiInterface::isConnected() const {
    // Rely on the internal _state which is updated by WiFi events
    return _state == WiFiState::CONNECTED;
}