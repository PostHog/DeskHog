#include "HomeAssistantClient.h"
#include "../ConfigManager.h"

HomeAssistantClient::HomeAssistantClient(ConfigManager& config, EventQueue& eventQueue) 
    : _config(config)
    , _eventQueue(eventQueue)
    , has_active_request(false)
    , last_refresh_check(0) {
    _http.setReuse(true);
}

void HomeAssistantClient::requestEntityState(const String& entity_id) {
    // Add to queue for immediate fetch
    QueuedRequest request = {
        .entity_id = entity_id,
        .retry_count = 0
    };
    request_queue.push(request);
    
    // Add to our set of known entities for future refreshes
    requested_entities.insert(entity_id);
}

bool HomeAssistantClient::callService(const String& domain, const String& service, const String& entity_id, const String& data) {
    if (!isReady() || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    String url = buildServiceUrl(domain, service);
    String payload = "{\"entity_id\":\"" + entity_id + "\"";
    
    if (!data.isEmpty()) {
        // Remove the closing brace and add the data
        payload = payload.substring(0, payload.length() - 1);
        payload += "," + data + "}";
    } else {
        payload += "}";
    }
    
    _http.begin(url);
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("Authorization", "Bearer " + _config.getHomeAssistantApiKey());
    
    int httpCode = _http.POST(payload);
    bool success = (httpCode == HTTP_CODE_OK || httpCode == 200);
    
    if (!success) {
        Serial.printf("Service call failed: %s.%s for %s, HTTP: %d\n", 
                     domain.c_str(), service.c_str(), entity_id.c_str(), httpCode);
    } else {
        Serial.printf("Service call successful: %s.%s for %s\n", 
                     domain.c_str(), service.c_str(), entity_id.c_str());
    }
    
    _http.end();
    return success;
}

bool HomeAssistantClient::isReady() const {
    return SystemController::isSystemFullyReady() && 
           !_config.getHomeAssistantUrl().isEmpty() && 
           !_config.getHomeAssistantApiKey().isEmpty();
}

void HomeAssistantClient::process() {
    if (!isReady()) {
        return;
    }

    // Process any queued requests
    if (!has_active_request) {
        processQueue();
    }

    // Check for needed refreshes
    if (!has_active_request) {
        unsigned long now = millis();
        if (now - last_refresh_check >= REFRESH_INTERVAL) {
            last_refresh_check = now;
            checkRefreshes();
        }
    }
}

void HomeAssistantClient::onSystemStateChange(SystemState state) {
    if (!SystemController::isSystemFullyReady() == false) {
        // Clear any active request when system becomes not ready
        has_active_request = false;
    }
}

void HomeAssistantClient::processQueue() {
    if (request_queue.empty()) {
        return;
    }

    QueuedRequest request = request_queue.front();
    String response;
    
    if (fetchEntityState(request.entity_id, response)) {
        // Publish to the event system
        publishEntityStateEvent(request.entity_id, response);
        request_queue.pop();
    } else {
        // Handle failure - retry if under max attempts
        if (request.retry_count < MAX_RETRIES) {
            // Update retry count and push back to end of queue
            request.retry_count++;
            Serial.printf("Request for entity %s failed, retrying (%d/%d)...\n", 
                          request.entity_id.c_str(), request.retry_count, MAX_RETRIES);
            
            // Remove from front and add to back with incremented retry count
            request_queue.pop();
            request_queue.push(request);
            
            // Add delay before next attempt
            delay(RETRY_DELAY);
        } else {
            // Max retries reached, drop request
            Serial.printf("Max retries reached for entity %s, dropping request\n", 
                         request.entity_id.c_str());
            request_queue.pop();
        }
    }
}

void HomeAssistantClient::checkRefreshes() {
    if (requested_entities.empty()) {
        return;
    }
    
    // Pick one entity to refresh
    String refresh_id;
    
    // This cycles through entities in a round-robin fashion since sets are ordered
    static auto it = requested_entities.begin();
    if (it == requested_entities.end()) {
        it = requested_entities.begin();
    }
    
    if (it != requested_entities.end()) {
        refresh_id = *it;
        ++it;
    } else {
        // Reset if we're at the end
        it = requested_entities.begin();
        if (it != requested_entities.end()) {
            refresh_id = *it;
            ++it;
        }
    }
    
    if (!refresh_id.isEmpty()) {
        String response;
        if (fetchEntityState(refresh_id, response)) {
            // Publish to the event system
            publishEntityStateEvent(refresh_id, response);
        }
    }
}

String HomeAssistantClient::buildEntityStateUrl(const String& entity_id) const {
    String url = _config.getHomeAssistantUrl();
    if (!url.endsWith("/")) {
        url += "/";
    }
    url += "api/states/";
    url += entity_id;
    return url;
}

String HomeAssistantClient::buildServiceUrl(const String& domain, const String& service) const {
    String url = _config.getHomeAssistantUrl();
    if (!url.endsWith("/")) {
        url += "/";
    }
    url += "api/services/";
    url += domain;
    url += "/";
    url += service;
    return url;
}

bool HomeAssistantClient::fetchEntityState(const String& entity_id, String& response) {
    if (!isReady() || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    unsigned long start_time = millis();
    has_active_request = true;
    
    String url = buildEntityStateUrl(entity_id);
    Serial.printf("Fetching entity state for %s from %s\n", entity_id.c_str(), url.c_str());
    
    _http.begin(url);
    _http.addHeader("Authorization", "Bearer " + _config.getHomeAssistantApiKey());
    _http.addHeader("Content-Type", "application/json");
    
    int httpCode = _http.GET();
    
    bool success = false;
    
    if (httpCode == HTTP_CODE_OK) {
        unsigned long network_time = millis() - start_time;
        Serial.printf("Network fetch time for %s: %lu ms\n", entity_id.c_str(), network_time);
        
        start_time = millis();
        
        // Get content length for allocation
        size_t contentLength = _http.getSize();
        
        // Pre-allocate in PSRAM if content is large
        if (contentLength > 8192) { // 8KB threshold
            // Force allocation in PSRAM for large responses
            response = String();
            response.reserve(contentLength);
            Serial.printf("Pre-allocated %u bytes in PSRAM for large response\n", contentLength);
        }
        
        response = _http.getString();
        unsigned long string_time = millis() - start_time;
        Serial.printf("Response processing time: %lu ms (size: %u bytes)\n", string_time, response.length());
        
        success = true;
    } else {
        // Handle HTTP errors
        Serial.print("HTTP GET failed for entity ");
        Serial.print(entity_id);
        Serial.print(", error: ");
        Serial.println(httpCode);
    }
    
    _http.end();
    has_active_request = false;
    return success;
}

void HomeAssistantClient::publishEntityStateEvent(const String& entity_id, const String& response) {
    // Check if response is empty or invalid
    if (response.length() == 0) {
        Serial.printf("Empty response for entity %s\n", entity_id.c_str());
        return;
    }
    
    // Publish the event with the raw JSON response
    _eventQueue.publishEvent(EventType::HA_ENTITY_STATE_RECEIVED, entity_id, response);
    
    // Log for debugging
    Serial.printf("Published entity state data for %s\n", entity_id.c_str());
}
