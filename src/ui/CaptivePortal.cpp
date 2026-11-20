#include "ui/CaptivePortal.h"
#include "ConfigManager.h"
#include "hardware/WifiInterface.h"
#include "EventQueue.h"
#include "plugins/PluginApi.h" // Plugin install/list/status routes
#include "plugins/PluginManager.h"
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
        default: return "UNKNOWN_ACTION"; // Good practice for a default
    }
}

// Constructor
CaptivePortal::CaptivePortal(ConfigManager& configManager, WiFiInterface& wifiInterface, EventQueue& eventQueue, PluginManager& pluginManager)
    : _server(80),
      _configManager(configManager),
      _wifiInterface(wifiInterface),
      _eventQueue(eventQueue),
      _pluginManager(pluginManager),
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

    // Setup page
    _server.on("/", HTTP_GET, std::bind(&CaptivePortal::handleRoot, this, std::placeholders::_1));

    // New API status endpoint
    // Serial.println("Registering /api/status..."); // DEBUG REMOVED
    _server.on("/api/status", HTTP_GET, std::bind(&CaptivePortal::handleApiStatus, this, std::placeholders::_1));

    // New async action triggering endpoints
    // Serial.println("Registering /api/actions/start-wifi-scan..."); // DEBUG REMOVED
    _server.on("/api/actions/start-wifi-scan", HTTP_POST, std::bind(&CaptivePortal::handleRequestWifiScan, this, std::placeholders::_1));
    _server.on("/api/actions/save-wifi", HTTP_POST, std::bind(&CaptivePortal::handleRequestSaveWifi, this, std::placeholders::_1));

    // Cards, connections and plugins
    registerPluginRoutes(_server, _pluginManager);

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
                        _eventQueue.publishEvent(EventType::WIFI_CREDENTIALS_FOUND);
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

    doc["firmware_version"] = CURRENT_FIRMWARE_VERSION;

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
        }

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
