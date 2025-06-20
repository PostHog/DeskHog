#include "PostHogClient.h"
#include "../ConfigManager.h"



PostHogClient::PostHogClient(ConfigManager& config, EventQueue& eventQueue) 
    : _config(config)
    , _eventQueue(eventQueue)
    , has_active_request(false)
    , last_refresh_check(0) {
    // Configure secure client for HTTPS
    _secureClient.setInsecure(); // TODO: get proper cert baked into the firmware to verify these connections
    _http.setReuse(true);
    
    // Subscribe to force refresh events
    _eventQueue.subscribe([this](const Event& event) {
        if (event.type == EventType::INSIGHT_FORCE_REFRESH) {
            this->requestInsightData(event.insightId, true);
        }
    });
}

String PostHogClient::buildBaseUrl() const {
    return "https://" + _config.getRegion() + ".posthog.com/api/projects/";
}

void PostHogClient::requestInsightData(const String& insight_id, bool forceRefresh) {
    // Add to queue for immediate fetch
    QueuedRequest request = {
        .insight_id = insight_id,
        .retry_count = 0,
        .force_refresh = forceRefresh
    };
    request_queue.push(request);
    
    // Add to our set of known insights for future refreshes
    requested_insights.insert(insight_id);
}

bool PostHogClient::isReady() const {
    return SystemController::isSystemFullyReady() && 
           _config.getTeamId() != ConfigManager::NO_TEAM_ID && 
           _config.getApiKey().length() > 0;
}

void PostHogClient::process() {
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

void PostHogClient::onSystemStateChange(SystemState state) {
    if (!SystemController::isSystemFullyReady() == false) {
        // Clear any active request when system becomes not ready
        has_active_request = false;
    }
}

void PostHogClient::processQueue() {
    if (request_queue.empty()) {
        return;
    }

    QueuedRequest request = request_queue.front();
    String response;
    
    if (fetchInsight(request.insight_id, response, request.force_refresh)) {
        // Publish to the event system
        publishInsightDataEvent(request.insight_id, response);
        request_queue.pop();
    } else {
        // Handle failure - retry if under max attempts
        if (request.retry_count < MAX_RETRIES) {
            // Update retry count and push back to end of queue
            request.retry_count++;
            Serial.printf("Request for insight %s failed, retrying (%d/%d)...\n", 
                          request.insight_id.c_str(), request.retry_count, MAX_RETRIES);
            
            // Remove from front and add to back with incremented retry count
            request_queue.pop();
            request_queue.push(request);
            
            // Add delay before next attempt
            delay(RETRY_DELAY);
        } else {
            // Max retries reached, drop request
            Serial.printf("Max retries reached for insight %s, dropping request\n", 
                         request.insight_id.c_str());
            request_queue.pop();
        }
    }
}

void PostHogClient::checkRefreshes() {
    if (requested_insights.empty()) {
        return;
    }
    
    // Pick one insight to refresh
    String refresh_id;
    
    // This cycles through insights in a round-robin fashion since sets are ordered
    static auto it = requested_insights.begin();
    if (it == requested_insights.end()) {
        it = requested_insights.begin();
    }
    
    if (it != requested_insights.end()) {
        refresh_id = *it;
        ++it;
    } else {
        // Reset if we're at the end
        it = requested_insights.begin();
        if (it != requested_insights.end()) {
            refresh_id = *it;
            ++it;
        }
    }
    
    if (!refresh_id.isEmpty()) {
        String response;
        if (fetchInsight(refresh_id, response)) {
            // Publish to the event system
            publishInsightDataEvent(refresh_id, response);
        }
    }
}

String PostHogClient::buildInsightUrl(const String& insight_id, const char* refresh_mode) const {
    String url = buildBaseUrl();
    url += String(_config.getTeamId());
    url += "/insights/?refresh=";
    url += refresh_mode;
    url += "&short_id=";
    url += insight_id;
    url += "&personal_api_key=";
    url += _config.getApiKey();
    return url;
}

bool PostHogClient::fetchInsight(const String& insight_id, String& response, bool forceRefresh) {
    if (!isReady() || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    unsigned long start_time = millis();
    has_active_request = true;
    
    bool success = false;
    bool needsRefresh = false;
    
    // If force refresh is requested, go straight to blocking mode
    if (forceRefresh) {
        String url = buildInsightUrl(insight_id, "blocking");
        Serial.printf("Force refreshing insight %s\n", insight_id.c_str());
        
        _http.begin(_secureClient, url);
        int httpCode = _http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            unsigned long network_time = millis() - start_time;
            Serial.printf("Force refresh network time for %s: %lu ms\n", insight_id.c_str(), network_time);
            
            // Get content length for allocation
            size_t contentLength = _http.getSize();
            
            // Pre-allocate in PSRAM if content is large
            if (contentLength > 8192) { // 8KB threshold
                response = String();
                response.reserve(contentLength);
                Serial.printf("Pre-allocated %u bytes in PSRAM for force refresh response\n", contentLength);
            }
            
            response = _http.getString();
            success = true;
        } else {
            Serial.printf("HTTP GET (force refresh) failed for %s, error: %d\n", insight_id.c_str(), httpCode);
        }
        
        _http.end();
        has_active_request = false;
        return success;
    }
    
    // Normal flow: First, try to get cached data
    String url = buildInsightUrl(insight_id, "force_cache");
    
    _http.begin(_secureClient, url);
    int httpCode = _http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        unsigned long network_time = millis() - start_time;
        Serial.printf("Network fetch time for %s: %lu ms\n", insight_id.c_str(), network_time);
        
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
        
        // Quick check if we need to refresh (look for null result)
        if (response.indexOf("\"result\":null") >= 0 || 
            response.indexOf("\"result\":[]") >= 0) {
            needsRefresh = true;
        } else {
            success = true;
        }
    } else {
        // Handle HTTP errors
        Serial.print("HTTP GET failed, error: ");
        Serial.println(httpCode);
    }
    
    _http.end();
    
    // If we need to refresh, make a second request with blocking
    if (needsRefresh) {
        url = buildInsightUrl(insight_id, "blocking");
        
        unsigned long refresh_start = millis();
        _http.begin(_secureClient, url);
        httpCode = _http.GET();
        
        if (httpCode == HTTP_CODE_OK) {
            unsigned long refresh_network = millis() - refresh_start;
            Serial.printf("Refresh network time: %lu ms\n", refresh_network);
            
            refresh_start = millis();
            
            // Get content length for allocation
            size_t contentLength = _http.getSize();
            
            // Pre-allocate in PSRAM if content is large
            if (contentLength > 8192) { // 8KB threshold
                // Force allocation in PSRAM for large responses
                response = String();
                response.reserve(contentLength);
                Serial.printf("Pre-allocated %u bytes in PSRAM for refresh response\n", contentLength);
            }
            
            response = _http.getString();
            unsigned long refresh_string = millis() - refresh_start;
            Serial.printf("Refresh string time: %lu ms (size: %u bytes)\n", refresh_string, response.length());
            
            success = true;
        } else {
            // Handle HTTP errors
            Serial.print("HTTP GET (blocking) failed, error: ");
            Serial.println(httpCode);
        }
        
        _http.end();
    }
    
    has_active_request = false;
    return success;
}

void PostHogClient::publishInsightDataEvent(const String& insight_id, const String& response) {
    // Check if response is empty or invalid
    if (response.length() == 0) {
        Serial.printf("Empty response for insight %s\n", insight_id.c_str());
        return;
    }
    
    // Publish the event with the raw JSON response
    _eventQueue.publishEvent(EventType::INSIGHT_DATA_RECEIVED, insight_id, response);
    
    // Log for debugging
    Serial.printf("Published raw JSON data for %s\n", insight_id.c_str());
} 