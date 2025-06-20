#include "ui/CaptivePortal.h"
#include "ConfigManager.h"
#include "hardware/WifiInterface.h"
#include "EventQueue.h"
#include "OtaManager.h" // Required for OtaManager interaction
#include "ui/CardController.h" // Required for CardController interaction
#include "html_portal.h"  // For portal HTML
#include <ArduinoJson.h>  // For JSON responses
#include <pgmspace.h> // For PROGMEM
#include <vector> // For std::vector (action queue)

// Max size for the action queue
const size_t MAX_ACTION_QUEUE_SIZE = 5; // Define a reasonable limit

// Action queue structure
struct QueuedAction { // Defined globally in this .cpp file
    PortalAction action;
    String param1;
    String param2;
};

// Forward declaration if QueuedAction is used before full definition within the class
// struct QueuedAction; // Not strictly needed if defined before first use or within class scope directly

// Definition for portalActionToString
const char* portalActionToString(PortalAction action) {
    switch (action) {
        case PortalAction::NONE: return "NONE";
        case PortalAction::SCAN_WIFI: return "SCAN_WIFI";
        case PortalAction::SAVE_WIFI: return "SAVE_WIFI";
        case PortalAction::SAVE_DEVICE_CONFIG: return "SAVE_DEVICE_CONFIG";
        case PortalAction::CHECK_OTA_UPDATE: return "CHECK_OTA_UPDATE";
        case PortalAction::START_OTA_UPDATE: return "START_OTA_UPDATE";
        default: return "UNKNOWN_ACTION"; // Good practice for a default
    }
}

// Constructor
CaptivePortal::CaptivePortal(ConfigManager& configManager, WiFiInterface& wifiInterface, EventQueue& eventQueue, OtaManager& otaManager, CardController& cardController)
    : _server(80),
      _configManager(configManager),
      _wifiInterface(wifiInterface),
      _eventQueue(eventQueue),
      _otaManager(otaManager), // Initialize the OtaManager reference
      _cardController(cardController), // Initialize the CardController reference
      _lastScanTime(0),
      _action_in_progress(PortalAction::NONE),
      _last_action_completed(PortalAction::NONE),
      _last_action_was_success(false),
      _last_action_message(""),
      _action_queue() { // Initialize the action queue vector (assuming _action_queue is a member declared in CaptivePortal.h)
}

void CaptivePortal::begin() {
    // Serial.println("CaptivePortal::begin() - START"); // DEBUG REMOVED

    // Perform initial WiFi scan
    performWiFiScan();

    // CORS Preflight handler for all relevant routes
    // Needs to be defined before the actual GET/POST handlers for the same paths.
    _server.on("/save-wifi", HTTP_OPTIONS, std::bind(&CaptivePortal::handleCorsPreflight, this, std::placeholders::_1));
    _server.on("/save-device-config", HTTP_OPTIONS, std::bind(&CaptivePortal::handleCorsPreflight, this, std::placeholders::_1));
    _server.on("/start-update", HTTP_OPTIONS, std::bind(&CaptivePortal::handleCorsPreflight, this, std::placeholders::_1));
    // _server.on("/check-update", HTTP_OPTIONS, std::bind(&CaptivePortal::handleCorsPreflight, this, std::placeholders::_1)); // Usually GET, but if POST later
    // _server.on("/update-status", HTTP_OPTIONS, std::bind(&CaptivePortal::handleCorsPreflight, this, std::placeholders::_1)); // Usually GET

    // Setup page
    _server.on("/", HTTP_GET, std::bind(&CaptivePortal::handleRoot, this, std::placeholders::_1));

    // New API status endpoint
    // Serial.println("Registering /api/status..."); // DEBUG REMOVED
    _server.on("/api/status", HTTP_GET, std::bind(&CaptivePortal::handleApiStatus, this, std::placeholders::_1));

    // New async action triggering endpoints
    // Serial.println("Registering /api/actions/start-wifi-scan..."); // DEBUG REMOVED
    _server.on("/api/actions/start-wifi-scan", HTTP_POST, std::bind(&CaptivePortal::handleRequestWifiScan, this, std::placeholders::_1));
    _server.on("/api/actions/save-wifi", HTTP_POST, std::bind(&CaptivePortal::handleRequestSaveWifi, this, std::placeholders::_1));
    _server.on("/api/actions/save-device-config", HTTP_POST, std::bind(&CaptivePortal::handleRequestSaveDeviceConfig, this, std::placeholders::_1));
    // Serial.println("Registering /api/actions/check-ota-update..."); // DEBUG REMOVED
    _server.on("/api/actions/check-ota-update", HTTP_POST, std::bind(&CaptivePortal::handleRequestCheckOtaUpdate, this, std::placeholders::_1));
    _server.on("/api/actions/start-ota-update", HTTP_POST, std::bind(&CaptivePortal::handleRequestStartOtaUpdate, this, std::placeholders::_1));

    // WiFi actions
    _server.on("/scan-networks", HTTP_GET, std::bind(&CaptivePortal::handleScanNetworks, this, std::placeholders::_1));

    // Device config actions
    _server.on("/get-device-config", HTTP_GET, std::bind(&CaptivePortal::handleGetDeviceConfig, this, std::placeholders::_1));


    // Card management actions
    _server.on("/api/cards/definitions", HTTP_GET, std::bind(&CaptivePortal::handleGetCardDefinitions, this, std::placeholders::_1));
    _server.on("/api/cards/configured", HTTP_GET, std::bind(&CaptivePortal::handleGetConfiguredCards, this, std::placeholders::_1));
    _server.on("/api/cards/configured", HTTP_POST, 
              std::bind(&CaptivePortal::handleSaveConfiguredCards, this, std::placeholders::_1),
              NULL,
              [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
                  // Store body data as a parameter for later processing
                  request->_tempObject = malloc(len + 1);
                  if(request->_tempObject != NULL){
                      memcpy(request->_tempObject, data, len);
                      ((char*)request->_tempObject)[len] = 0;
                  }
              });

    // OTA Update actions
    _server.on("/check-update", HTTP_GET, std::bind(&CaptivePortal::handleCheckUpdate, this, std::placeholders::_1));
    _server.on("/start-update", HTTP_POST, std::bind(&CaptivePortal::handleStartUpdate, this, std::placeholders::_1));
    _server.on("/update-status", HTTP_GET, std::bind(&CaptivePortal::handleUpdateStatus, this, std::placeholders::_1));

    // Captive portal detection URLs
    _server.on("/generate_204", HTTP_GET, std::bind(&CaptivePortal::handleCaptivePortal, this, std::placeholders::_1)); // Android
    _server.on("/fwlink", HTTP_GET, std::bind(&CaptivePortal::handleCaptivePortal, this, std::placeholders::_1));         // Microsoft
    _server.on("/connecttest.txt", HTTP_GET, std::bind(&CaptivePortal::handleCaptivePortal, this, std::placeholders::_1)); // Microsoft
    _server.on("/hotspot-detect.html", HTTP_GET, std::bind(&CaptivePortal::handleCaptivePortal, this, std::placeholders::_1)); // Apple
    _server.on("/mobile/status.php", HTTP_GET, std::bind(&CaptivePortal::handleCaptivePortal, this, std::placeholders::_1)); // Some Androids
    _server.on("/ncsi.txt", HTTP_GET, std::bind(&CaptivePortal::handleCaptivePortal, this, std::placeholders::_1)); // NCSI

    // Not found handler
    _server.onNotFound(std::bind(&CaptivePortal::handle404, this, std::placeholders::_1));

    // Serial.println("CaptivePortal _server.begin() called."); // DEBUG REMOVED
    _server.begin();
    // Serial.println("CaptivePortal _server.begin() FINISHED. Server should be running."); // DEBUG REMOVED
    Serial.println("Captive Portal HTTP server started (Async Refactor).");
}

void CaptivePortal::handleCorsPreflight(AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    request->send(response);
}

void CaptivePortal::handleRoot(AsyncWebServerRequest *request) {
    // Use PORTAL_HTML and its size directly from html_portal.h
    // Note: beginResponse_P expects uint8_t*, PORTAL_HTML is char*. Casting might be needed
    // or prefer beginResponse if types are an issue.
    // For now, let's try with a cast if the compiler insists, otherwise direct usage.
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", 
                                                               (const uint8_t*)PORTAL_HTML, 
                                                               sizeof(PORTAL_HTML) - 1);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
}

void CaptivePortal::performWiFiScan() {
    Serial.println("Starting WiFi Scan...");
    _wifiInterface.scanNetworks(); // Assuming this is synchronous or handles its own async
    _cachedNetworks = getNetworksJson();
    _lastScanTime = millis();
    Serial.println("WiFi Scan Complete. Cached networks.");
}

String CaptivePortal::getNetworksJson() {
    DynamicJsonDocument doc(2048);
    JsonArray networksArray = doc.to<JsonArray>();
    std::vector<WiFiInterface::NetworkInfo> networks = _wifiInterface.getScannedNetworks();
    for (const auto& net : networks) { 
        JsonObject netObj = networksArray.createNestedObject(); 
        netObj["ssid"] = net.ssid;
        netObj["rssi"] = net.rssi;
        netObj["encrypted"] = net.encryptionType != WIFI_AUTH_OPEN; // Corrected: net.encryptionType
    }
    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

void CaptivePortal::handleScanNetworks(AsyncWebServerRequest *request) {
    // If cache is older than 10 seconds, request a new scan.
    // The actual scan is done by processAsyncOperations.
    // Client polls /api/status for results including the network list.
    if (millis() - _lastScanTime > 10000) { // Still check cache age to decide if a new scan *request* is needed
        requestAction(PortalAction::SCAN_WIFI, request); // This will respond with 202 if queued, or 429 if queue full
    } else {
        // Cache is fresh, return current cached networks immediately.
        // This maintains some responsiveness if a recent scan is available.
        // Alternatively, could also make this path queue an action, and client always polls.
        // For now, let's return cached if fresh, and queue if stale.
        AsyncWebServerResponse *response = request->beginResponse(200, "application/json", _cachedNetworks);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    }
}

void CaptivePortal::handleSaveWifi(AsyncWebServerRequest *request) {
    String ssid;
    String password;
    bool success = false;

    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
        ssid = request->getParam("ssid", true)->value();
        password = request->getParam("password", true)->value();
        
        Serial.printf("Received WiFi credentials for SSID: %s\n", ssid.c_str());
        if (_configManager.saveWiFiCredentials(ssid, password)) {
            // Trigger WiFi connection attempt via EventQueue or directly
            _eventQueue.publishEvent(EventType::WIFI_CREDENTIALS_FOUND, ssid);
            success = true;
        }
    }

    DynamicJsonDocument doc(256); // Switched to DynamicJsonDocument
    doc["success"] = success;
    String responseJson;
    serializeJson(doc, responseJson);
    
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void CaptivePortal::handleGetDeviceConfig(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256); // Switched to DynamicJsonDocument
    doc["teamId"] = _configManager.getTeamId();
    String apiKey = _configManager.getApiKey();
    // Send a truncated or placeholder API key for security if it's set
    doc["apiKey"] = apiKey.length() > 0 ? "********" + apiKey.substring(apiKey.length() - 4) : "";

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void CaptivePortal::handleSaveDeviceConfig(AsyncWebServerRequest *request) {
    bool success = false;
    if (request->hasParam("teamId", true) && request->hasParam("apiKey", true)) {
        int teamId = request->getParam("teamId", true)->value().toInt();
        String apiKey = request->getParam("apiKey", true)->value();

        _configManager.setTeamId(teamId);
        // Only update API key if it's not the placeholder
        if (apiKey.indexOf("********") == -1) {
             _configManager.setApiKey(apiKey);
        }
        success = true;
        // No direct EventType mapping for API_CONFIG_UPDATED, so removing for now.
        // Consider if a different existing event is appropriate or if one needs to be added to EventQueue.h
    }

    DynamicJsonDocument doc(256); // Switched to DynamicJsonDocument
    doc["success"] = success;
    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}



void CaptivePortal::handleCaptivePortal(AsyncWebServerRequest *request) {
    if (_wifiInterface.isConnected()) { // Check if ESP32 STA is connected to an upstream WiFi
        String url = request->url();
        Serial.printf("CaptivePortal: WiFi connected, handling OS detection URL: %s\n", url.c_str());

        if (url.indexOf("generate_204") != -1) {
            request->send(204); // HTTP 204 No Content for Android/Chrome detection
        } else if (url.indexOf("hotspot-detect.html") != -1) {
            // Apple expects a page containing "Success"
            request->send(200, "text/html", "<!DOCTYPE HTML><HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
        } else if (url.indexOf("connecttest.txt") != -1) {
            // Microsoft NCSI connecttest.txt
            request->send(200, "text/plain", "Microsoft Connect Test");
        } else if (url.indexOf("ncsi.txt") != -1) {
            // Microsoft NCSI ncsi.txt - often expects specific content or just a 200/204
            request->send(200, "text/plain", "Microsoft NCSI"); // Or send(204)
        } else {
            // For other unhandled but common captive portal detection URLs (like fwlink, mobile/status.php)
            // a generic success response might work.
            Serial.printf("CaptivePortal: Unhandled OS detection URL while WiFi connected: %s. Sending generic 204.\n", url.c_str());
            request->send(204); 
        }
    } else {
        // WiFi STA not connected, ESP32 is in 'portal setup' mode. Redirect to the portal UI.
        Serial.printf("CaptivePortal: WiFi not connected, redirecting OS detection URL %s to / (portal UI)\n", request->url().c_str());
        request->redirect("/");
    }
}

void CaptivePortal::handle404(AsyncWebServerRequest *request) {
    // For any other request, also redirect to the root page in AP mode
    // Or, if WiFi is connected, could send a 404.
    // Assuming this is primarily for AP mode config.
    Serial.printf("CaptivePortal: 404 for %s, redirecting to /\n", request->url().c_str());
    request->redirect("/");
}

// OTA Update Handlers (interacting with OtaManager)
void CaptivePortal::handleCheckUpdate(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512); // Switched to DynamicJsonDocument
    UpdateInfo lastCheck = _otaManager.getLastCheckResult(); // Get previous check result first
    
    doc["current_firmware_version"] = lastCheck.currentVersion; // Use version from OtaManager
    
    bool checkInitiated = _otaManager.checkForUpdate(); // Attempt to start a new check
    
    doc["check_initiated"] = checkInitiated;
    if (checkInitiated) {
        doc["initial_status_message"] = "Update check started. Polling for results...";
    } else {
        // Could be already checking, or WiFi not ready, etc. OtaManager handles details.
        // Get current status to reflect why it might not have initiated.
        UpdateStatus currentStatus = _otaManager.getStatus();
        doc["initial_status_message"] = currentStatus.message; // Reflect current status message
    }

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void CaptivePortal::handleStartUpdate(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256); // Switched to DynamicJsonDocument
    UpdateInfo lastCheck = _otaManager.getLastCheckResult(); // Need this for the download URL

    bool success = false;
    String message = "No update information available or update not available.";

    if (lastCheck.updateAvailable && !lastCheck.downloadUrl.isEmpty()) {
        if (_otaManager.beginUpdate(lastCheck.downloadUrl)) {
            success = true;
            message = "Update process initiated.";
        } else {
            // beginUpdate failed, get status to explain why
            UpdateStatus currentStatus = _otaManager.getStatus();
            message = "Failed to start update: " + currentStatus.message;
        }
    } else if (!lastCheck.updateAvailable) {
        message = "No update available to start.";
    } else if (lastCheck.downloadUrl.isEmpty()) {
        message = "Update available, but download URL is missing.";
    }
    
    doc["success"] = success;
    doc["message"] = message;

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void CaptivePortal::handleUpdateStatus(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512); // Switched to DynamicJsonDocument
    UpdateStatus status = _otaManager.getStatus();
    UpdateInfo lastCheck = _otaManager.getLastCheckResult(); // Also get last check results

    doc["status_code"] = static_cast<int>(status.status);
    doc["status_message"] = status.message;
    doc["progress"] = status.progress;

    // Information from the last check result (UpdateInfo)
    // This is what portal.js expects for displaying available versions etc.
    // The names in portal.js are like `available_firmware_version_info`
    doc["current_firmware_version_info"] = lastCheck.currentVersion;
    doc["is_update_available_info"] = lastCheck.updateAvailable;
    doc["available_firmware_version_info"] = lastCheck.availableVersion;
    doc["release_notes_info"] = lastCheck.releaseNotes;
    doc["error_message_info"] = lastCheck.error; // Error specifically from the check process

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

// --- New method to process async operations ---
// This should be called periodically from a task (e.g., portalTaskFunction in main.cpp)
void CaptivePortal::processAsyncOperations() {
    
    if (_action_in_progress != PortalAction::NONE && !_action_queue.empty()) {
        // This condition implies an action was set as _action_in_progress by requestAction,
        // but processAsyncOperations hasn't completed it yet (or another action is still truly in progress from a previous cycle).
        // We only want to start a new action from the queue if the *portal itself* is not mid-processing an action from a *previous* call to processAsyncOperations.
        // The _action_in_progress set by requestAction is more of an optimistic "request pending" state for the UI.
        // The true gate for starting new work here is if the *actual processing machinery* is idle.
        // For now, the original logic is: if _action_in_progress is NOT NONE (set by previous cycle of this func), return.
        // If it WAS set by requestAction but this function is now running, we should process from queue.
        // Let's refine the gate: only proceed if no *true* async op from this function is pending.
        // The current _action_in_progress set by requestAction should NOT prevent dequeuing.
        // The real check should be if a previous call to this function set _action_in_progress and hasn't cleared it.
        // However, the current code structure IS: if _action_in_progress is anything but NONE, return.
        // This is the part that needs to align with the immediate _action_in_progress update in requestAction.

        // If _action_in_progress was set by requestAction(), it signals a *request* is pending.
        // The actual processing of that request starts here by dequeuing.
        // The original `if (_action_in_progress != PortalAction::NONE) { return; }` was too simple
        // if requestAction() also sets _action_in_progress immediately.

        // Corrected logic: if there is something in the queue, we process it, 
        // regardless of _action_in_progress set by requestAction, as that's just a UI hint.
        // The _action_in_progress will be properly managed by *this* function upon completion.
    }

    if (!_action_queue.empty()) {
        Serial.println("DEBUG: Action queue is not empty, processing next action.");
        QueuedAction current_queued_action = _action_queue.front();
        _action_queue.erase(_action_queue.begin()); // Dequeue

        PortalAction actionToProcess = current_queued_action.action;

        Serial.printf("Processing action from queue: %s (Enum Value: %d)\n", portalActionToString(actionToProcess), static_cast<int>(actionToProcess));

        bool currentActionSuccess = false;
        String currentActionMessage = "Action failed or not implemented.";

        Serial.println("DEBUG: CaptivePortal - About to enter action processing switch statement.");
        switch (actionToProcess) {
            case PortalAction::SCAN_WIFI:
                performWiFiScan(); 
                currentActionSuccess = true;
                currentActionMessage = "WiFi scan completed.";
                break;
            case PortalAction::SAVE_WIFI: {
                String ssid = current_queued_action.param1; // Use from QueuedAction
                String password = current_queued_action.param2; // Use from QueuedAction
                if (!ssid.isEmpty()) {
                    Serial.printf("Saving WiFi credentials for SSID: %s\n", ssid.c_str());
                    if (_configManager.saveWiFiCredentials(ssid, password)) {
                        _eventQueue.publishEvent(EventType::WIFI_CREDENTIALS_FOUND, ssid);
                        currentActionSuccess = true;
                        currentActionMessage = "WiFi credentials saved for " + ssid;
                    } else {
                        currentActionMessage = "Failed to save WiFi credentials.";
                    }
                } else {
                    currentActionMessage = "SSID cannot be empty for SAVE_WIFI.";
                }
                // _pending_param1 = ""; _pending_param2 = ""; // Not needed now
                break;
            }
            case PortalAction::SAVE_DEVICE_CONFIG: {
                String teamIdStr = current_queued_action.param1; // Use from QueuedAction
                String apiKey = current_queued_action.param2;    // Use from QueuedAction
                String region = current_queued_action.param3;   // Use from QueuedAction
                if (!teamIdStr.isEmpty()) {
                    _configManager.setTeamId(teamIdStr.toInt());
                    if (!apiKey.isEmpty() && apiKey.indexOf("********") == -1) {
                        _configManager.setApiKey(apiKey);
                    }
                    if(!region.isEmpty()) {
                        _configManager.setRegion(region);
                    }
                    currentActionSuccess = true;
                    currentActionMessage = "Device configuration saved.";
                } else {
                    currentActionMessage = "Team ID cannot be empty.";
                }
                 // _pending_param1 = ""; _pending_param2 = ""; // Not needed now
                break;
            }
            case PortalAction::CHECK_OTA_UPDATE:
                Serial.println("DEBUG: Case PortalAction::CHECK_OTA_UPDATE reached in processAsyncOperations.");
                if (_otaManager.checkForUpdate()) { 
                    currentActionSuccess = true; 
                    currentActionMessage = "OTA update check successfully initiated with OtaManager.";
                } else {
                    currentActionSuccess = false; // Explicitly false
                    UpdateStatus status = _otaManager.getStatus();
                    currentActionMessage = "Portal failed to initiate OTA update check with OtaManager. OtaManager status: " + status.message;
                }
                break;
            case PortalAction::START_OTA_UPDATE: {
                Serial.println("DEBUG: CaptivePortal - *** EXECUTION HAS REACHED START_OTA_UPDATE CASE ***"); // New very first line
                Serial.println("DEBUG: CaptivePortal - START_OTA_UPDATE case entered.");
                UpdateInfo lastCheck = _otaManager.getLastCheckResult();
                Serial.println("DEBUG: CaptivePortal - Got lastCheck result from OtaManager.");
                Serial.printf("DEBUG: CaptivePortal - lastCheck.updateAvailable: %d, downloadUrl empty: %d, URL: %s\n", lastCheck.updateAvailable, lastCheck.downloadUrl.isEmpty(), lastCheck.downloadUrl.c_str());

                if (lastCheck.updateAvailable && !lastCheck.downloadUrl.isEmpty()) {
                    Serial.println("DEBUG: CaptivePortal - Conditions met to call _otaManager.beginUpdate().");
                    bool updateBegun = _otaManager.beginUpdate(lastCheck.downloadUrl);
                    Serial.printf("DEBUG: CaptivePortal - _otaManager.beginUpdate() returned: %s\n", updateBegun ? "true" : "false");
                    if (updateBegun) { 
                        currentActionSuccess = true;
                        currentActionMessage = "OTA update process started. Poll /api/status for progress.";
                        Serial.println("DEBUG: CaptivePortal - OTA update process reported as started by beginUpdate.");
                    } else {
                        Serial.println("DEBUG: CaptivePortal - _otaManager.beginUpdate() returned false. Getting OtaManager status...");
                        UpdateStatus status = _otaManager.getStatus();
                        Serial.println("DEBUG: CaptivePortal - Got OtaManager status after beginUpdate failed.");
                        currentActionMessage = "Failed to start OTA update: " + status.message;
                        Serial.printf("DEBUG: CaptivePortal - OTA beginUpdate failed. Portal's message: %s\n", currentActionMessage.c_str());
                    }
                } else if (!lastCheck.updateAvailable) {
                    currentActionSuccess = false; // Ensure success is false
                    currentActionMessage = "No OTA update available to start.";
                    Serial.println("DEBUG: CaptivePortal - No OTA update available according to lastCheck.");
                } else { // This implies updateAvailable is true, but downloadUrl is empty
                    currentActionSuccess = false; // Ensure success is false
                    currentActionMessage = "OTA update available, but download URL is missing.";
                    Serial.println("DEBUG: CaptivePortal - OTA update available, but download URL is missing according to lastCheck.");
                }
                Serial.println("DEBUG: CaptivePortal - Exiting START_OTA_UPDATE case logic.");
                break;
            }
            case PortalAction::NONE:
                // Should not happen
                break;
        }
        
        _last_action_completed = actionToProcess;
        _last_action_was_success = currentActionSuccess;
        _last_action_message = currentActionMessage;
        _action_in_progress = PortalAction::NONE; // Mark PORTAL as done with this action processing cycle.
        Serial.printf("Finished processing action: %s, Success: %d, Msg: %s\n", portalActionToString(actionToProcess), currentActionSuccess, currentActionMessage.c_str());
    }
}

// --- New /api/status endpoint ---
void CaptivePortal::handleApiStatus(AsyncWebServerRequest *request) {
    // Serial.println("/api/status HANDLER CALLED"); // Removed to reduce log spam
    // REMOVE SIMPLIFIED TEST BLOCK
    // request->send(200, "text/plain", "API Status OK - Simplified"); 
    // return; 

    // RESTORE ORIGINAL FULL LOGIC
    DynamicJsonDocument doc(4096); 

    JsonObject portalObj = doc.createNestedObject("portal");
    portalObj["action_in_progress"] = portalActionToString(_action_in_progress);
    portalObj["last_action_completed"] = portalActionToString(_last_action_completed);
    portalObj["last_action_status"] = _last_action_was_success ? "SUCCESS" : (_last_action_completed != PortalAction::NONE ? "ERROR" : "NONE");
    portalObj["last_action_message"] = _last_action_message;

    // Add specific OTA request status message from the portal's perspective
    String portalOtaRequestMsg = "";
    if (_action_in_progress == PortalAction::CHECK_OTA_UPDATE) {
        portalOtaRequestMsg = "Portal: OTA update check request is pending execution.";
    } else if (_action_in_progress == PortalAction::START_OTA_UPDATE) {
        portalOtaRequestMsg = "Portal: OTA update start request is pending execution.";
    } else {
        // Check if the *last completed* action by the portal was an OTA dispatch
        if (_last_action_completed == PortalAction::CHECK_OTA_UPDATE || _last_action_completed == PortalAction::START_OTA_UPDATE) {
            if (_last_action_was_success) {
                portalOtaRequestMsg = "Portal: Successfully dispatched '" + String(portalActionToString(_last_action_completed)) + "' to OtaManager. Current OtaManager status follows.";
            } else {
                portalOtaRequestMsg = "Portal: Failed to dispatch '" + String(portalActionToString(_last_action_completed)) + "'. Error: " + _last_action_message;
            }
        }
    }
    if (!portalOtaRequestMsg.isEmpty()) {
        portalObj["portal_ota_action_message"] = portalOtaRequestMsg;
    }

    JsonObject wifiObj = doc.createNestedObject("wifi");
    wifiObj["is_scanning"] = (_action_in_progress == PortalAction::SCAN_WIFI); 
    if (_lastScanTime > 0 && !_cachedNetworks.isEmpty()){ 
        DynamicJsonDocument cachedNetDoc(4096);
        DeserializationError error = deserializeJson(cachedNetDoc, _cachedNetworks);
        if (!error) {
            wifiObj["networks"] = cachedNetDoc.as<JsonArray>(); 
        } else {
            Serial.printf("Failed to parse cached networks JSON in handleApiStatus: %s\n", error.c_str());
            wifiObj.createNestedArray("networks");
        }
    } else {
        wifiObj.createNestedArray("networks");
    }
    wifiObj["last_scan_time"] = _lastScanTime;
    wifiObj["connected_ssid"] = _wifiInterface.getCurrentSsid(); 
    wifiObj["ip_address"] = _wifiInterface.getIPAddress();
    wifiObj["is_connected"] = _wifiInterface.isConnected(); 

    JsonObject deviceConfigObj = doc.createNestedObject("device_config");
    deviceConfigObj["team_id"] = _configManager.getTeamId();
    String apiKey = _configManager.getApiKey();
    deviceConfigObj["api_key_display"] = apiKey.length() > 0 ? "********" + apiKey.substring(apiKey.length() - 4) : "";
    deviceConfigObj["api_key_present"] = apiKey.length() > 0;
    deviceConfigObj["region"] = _configManager.getRegion();


    JsonObject otaObj = doc.createNestedObject("ota");
    UpdateStatus status = _otaManager.getStatus();
    UpdateInfo lastCheck = _otaManager.getLastCheckResult();
    otaObj["status_code"] = static_cast<int>(status.status);
    otaObj["status_message"] = status.message;
    otaObj["progress"] = status.progress;
    otaObj["current_firmware_version"] = lastCheck.currentVersion; 
    otaObj["update_available"] = lastCheck.updateAvailable;     
    otaObj["available_version"] = lastCheck.availableVersion;  
    otaObj["release_notes"] = lastCheck.releaseNotes;        
    otaObj["error_message"] = lastCheck.error;               

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
}

void CaptivePortal::handleRequestWifiScan(AsyncWebServerRequest *request) {
    requestAction(PortalAction::SCAN_WIFI, request);
}

void CaptivePortal::handleRequestSaveWifi(AsyncWebServerRequest *request) {
    requestAction(PortalAction::SAVE_WIFI, request);
}

void CaptivePortal::handleRequestSaveDeviceConfig(AsyncWebServerRequest *request) {
    requestAction(PortalAction::SAVE_DEVICE_CONFIG, request);
}


void CaptivePortal::handleRequestCheckOtaUpdate(AsyncWebServerRequest *request) {
    requestAction(PortalAction::CHECK_OTA_UPDATE, request);
}

void CaptivePortal::handleRequestStartOtaUpdate(AsyncWebServerRequest *request) {
    requestAction(PortalAction::START_OTA_UPDATE, request);
}

void CaptivePortal::requestAction(PortalAction action, AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(128);
    if (_action_queue.size() >= MAX_ACTION_QUEUE_SIZE) {
        doc["status"] = "queue_full";
        doc["message"] = "Action queue is full. Please try again later.";
        String responseJson;
        serializeJson(doc, responseJson);
        AsyncWebServerResponse *response = request->beginResponse(429, "application/json", responseJson); // 429 Too Many Requests
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    } else {
        QueuedAction new_action;
        new_action.action = action;

        // _pending_param1 = ""; // Clear previous pending params // No longer global like this
        // _pending_param2 = "";

        // Populate parameters for the action based on its type
        if (action == PortalAction::SAVE_WIFI) {
            if (request->hasParam("ssid", true)) new_action.param1 = request->getParam("ssid", true)->value();
            if (request->hasParam("password", true)) new_action.param2 = request->getParam("password", true)->value();
        } else if (action == PortalAction::SAVE_DEVICE_CONFIG) {
            if (request->hasParam("teamId", true)) new_action.param1 = request->getParam("teamId", true)->value();
            if (request->hasParam("apiKey", true)) new_action.param2 = request->getParam("apiKey", true)->value();
            if (request->hasParam("region", true)) new_action.param3 = request->getParam("region", true)->value();
        }
        // For actions like SCAN_WIFI, CHECK_OTA_UPDATE, START_OTA_UPDATE, params are not from request body initially.

        _action_queue.push_back(new_action);

        // Immediately update portal state to reflect action is pending
        _action_in_progress = action;
        _last_action_completed = PortalAction::NONE; // Clear previous completed action for this new pending one
        _last_action_was_success = false; // Default until processed
        _last_action_message = "Action '" + String(portalActionToString(action)) + "' received and is pending.";

        doc["status"] = "queued";
        doc["message"] = "Action '" + String(portalActionToString(action)) + "' queued.";
        String responseJson;
        serializeJson(doc, responseJson);
        AsyncWebServerResponse *response = request->beginResponse(202, "application/json", responseJson); // 202 Accepted
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
    }
}

void CaptivePortal::handleGetCardDefinitions(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    JsonArray definitionsArray = doc.to<JsonArray>();

    // Get card definitions from CardController
    std::vector<CardDefinition> definitions = _cardController.getCardDefinitions();
    
    for (const CardDefinition& def : definitions) {
        JsonObject defObj = definitionsArray.createNestedObject();
        defObj["id"] = cardTypeToString(def.type);
        defObj["name"] = def.name;
        defObj["allowMultiple"] = def.allowMultiple;
        defObj["needsConfigInput"] = def.needsConfigInput;
        defObj["configInputLabel"] = def.configInputLabel;
        defObj["description"] = def.uiDescription;
    }

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void CaptivePortal::handleGetConfiguredCards(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    JsonArray cardsArray = doc.to<JsonArray>();

    // Get configured cards from ConfigManager
    std::vector<CardConfig> cardConfigs = _configManager.getCardConfigs();
    for (const CardConfig& config : cardConfigs) {
        JsonObject cardObj = cardsArray.createNestedObject();
        cardObj["type"] = cardTypeToString(config.type);
        cardObj["config"] = config.config;
        cardObj["order"] = config.order;
        cardObj["name"] = config.name;
    }

    String responseJson;
    serializeJson(doc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}

void CaptivePortal::handleSaveConfiguredCards(AsyncWebServerRequest *request) {
    bool success = false;
    String message = "Failed to save card configuration";

    // Check if we have body data stored by the body handler
    if (request->_tempObject != NULL) {
        String body = String((char*)request->_tempObject);
        free(request->_tempObject); // Clean up the allocated memory
        request->_tempObject = NULL;
        
        Serial.printf("Received card config body: %s\n", body.c_str());
        
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, body);
        
        if (!error && doc.is<JsonArray>()) {
            JsonArray cardsArray = doc.as<JsonArray>();
            std::vector<CardConfig> cardConfigs;
            
            // Parse each card configuration
            for (JsonVariant v : cardsArray) {
                JsonObject obj = v.as<JsonObject>();
                if (obj.containsKey("type") && obj.containsKey("order")) {
                    CardConfig config;
                    config.type = stringToCardType(obj["type"].as<String>());
                    config.config = obj.containsKey("config") ? obj["config"].as<String>() : "";
                    config.order = obj["order"].as<int>();
                    config.name = obj.containsKey("name") ? obj["name"].as<String>() : "";
                    cardConfigs.push_back(config);
                }
            }
            
            // Save to ConfigManager
            if (_configManager.saveCardConfigs(cardConfigs)) {
                success = true;
                message = "Card configuration saved successfully";
                Serial.printf("Successfully saved %d card configurations\n", cardConfigs.size());
            } else {
                message = "Failed to save to storage";
            }
        } else {
            message = "Invalid JSON format";
            Serial.printf("JSON parse error: %s\n", error.c_str());
        }
    } else {
        message = "No configuration data provided";
        Serial.println("No body data received in card config save");
    }

    DynamicJsonDocument responseDoc(256);
    responseDoc["success"] = success;
    responseDoc["message"] = message;
    
    String responseJson;
    serializeJson(responseDoc, responseJson);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", responseJson);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
}
