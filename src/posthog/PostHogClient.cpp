#include "PostHogClient.h"
#include "../ConfigManager.h"
#include "../AsyncHTTPClient.h"



PostHogClient::PostHogClient(ConfigManager& config, EventQueue& eventQueue) 
    : _config(config)
    , _eventQueue(eventQueue)
    , _asyncHttpClient(std::make_unique<AsyncHTTPClient>(eventQueue))
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
    // Add to our set of known insights for future refreshes
    requested_insights.insert(insight_id);
    
    // Get cached data for progressive loading
    String cachedData = getCachedData(insight_id);
    if (!cachedData.isEmpty() && !forceRefresh) {
        // Immediately show cached data
        Serial.printf("[PostHogClient] Showing cached data for %s\n", insight_id.c_str());
        publishInsightDataEvent(insight_id, cachedData);
        
        // Still fetch fresh data in background
        makeAsyncInsightRequest(insight_id, false);
    } else {
        // No cache or force refresh - show loading state and fetch
        _eventQueue.publishEvent(EventType::INSIGHT_NETWORK_STATE_CHANGED, insight_id, "loading");
        makeAsyncInsightRequest(insight_id, forceRefresh);
    }
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

    // Process async HTTP client
    _asyncHttpClient->process();

    // Legacy queue processing for fallback
    if (!has_active_request) {
        processQueue();
    }

    // Check for needed refreshes
    unsigned long now = millis();
    if (now - last_refresh_check >= REFRESH_INTERVAL) {
        last_refresh_check = now;
        checkRefreshes();
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
        // Use async request for automatic refreshes (non-blocking)
        Serial.printf("[PostHogClient] Auto-refreshing insight %s\\n", refresh_id.c_str());
        makeAsyncInsightRequest(refresh_id, false); // Use cache first
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
    
    // Cache the response for future progressive loading
    cacheInsightData(insight_id, response);
    
    // Publish the event with the raw JSON response
    _eventQueue.publishEvent(EventType::INSIGHT_DATA_RECEIVED, insight_id, response);
    
    // Log for debugging
    Serial.printf("Published raw JSON data for %s\n", insight_id.c_str());
}

void PostHogClient::makeAsyncInsightRequest(const String& insight_id, bool forceRefresh) {
    if (!isReady() || WiFi.status() != WL_CONNECTED) {
        handleInsightError(insight_id, "System not ready or WiFi disconnected", 0);
        return;
    }
    
    String url = buildInsightUrl(insight_id, forceRefresh ? "blocking" : "force_cache");
    
    AsyncHTTPClient::RequestConfig config;
    config.url = url;
    config.method = AsyncHTTPClient::Method::GET;
    config.timeout = 30000; // 30 seconds
    config.maxRetries = 3;
    
    // Success callback
    config.onSuccess = [this, insight_id](const String& response, int statusCode) {
        this->handleInsightSuccess(insight_id, response, statusCode);
    };
    
    // Error callback
    config.onError = [this, insight_id](const String& error, int statusCode) {
        this->handleInsightError(insight_id, error, statusCode);
    };
    
    // Make the async request
    String requestId = _asyncHttpClient->request(config);
    if (requestId.isEmpty()) {
        handleInsightError(insight_id, "Failed to queue HTTP request", 0);
    } else {
        Serial.printf("[PostHogClient] Started async request %s for insight %s\\n", 
                      requestId.c_str(), insight_id.c_str());
    }
}

void PostHogClient::handleInsightSuccess(const String& insight_id, const String& data, int statusCode) {
    // This is called on the UI thread via AsyncHTTPClient
    Serial.printf("[PostHogClient] Async request succeeded for %s (HTTP %d, %d bytes)\\n", 
                  insight_id.c_str(), statusCode, data.length());
    
    if (statusCode == 200) {
        // Check if we need to retry with blocking refresh
        if (data.indexOf("\\\"result\\\":null") >= 0 || data.indexOf("\\\"result\\\":[]") >= 0) {
            Serial.printf("[PostHogClient] Cache miss for %s, retrying with blocking refresh\\n", insight_id.c_str());
            makeAsyncInsightRequest(insight_id, true); // Force refresh
            return;
        }
        
        // Success - publish data and update UI state
        publishInsightDataEvent(insight_id, data);
        _eventQueue.publishEvent(EventType::INSIGHT_NETWORK_STATE_CHANGED, insight_id, "success");
    } else {
        handleInsightError(insight_id, "HTTP " + String(statusCode), statusCode);
    }
}

void PostHogClient::handleInsightError(const String& insight_id, const String& error, int statusCode) {
    // This is called on the UI thread via AsyncHTTPClient
    Serial.printf("[PostHogClient] Async request failed for %s: %s (HTTP %d)\\n", 
                  insight_id.c_str(), error.c_str(), statusCode);
    
    // Publish error events
    _eventQueue.publishEvent(EventType::INSIGHT_DATA_ERROR, insight_id, error);
    _eventQueue.publishEvent(EventType::INSIGHT_NETWORK_STATE_CHANGED, insight_id, "error");
}

String PostHogClient::getCachedData(const String& insight_id) const {
    auto it = _insightCache.find(insight_id);
    if (it != _insightCache.end() && isCacheValid(insight_id)) {
        return it->second;
    }
    return "";
}

void PostHogClient::cacheInsightData(const String& insight_id, const String& data) {
    _insightCache[insight_id] = data;
    _cacheTimestamps[insight_id] = millis();
}

bool PostHogClient::isCacheValid(const String& insight_id) const {
    auto it = _cacheTimestamps.find(insight_id);
    if (it == _cacheTimestamps.end()) {
        return false;
    }
    
    unsigned long now = millis();
    unsigned long cacheAge = now - it->second;
    
    // Cache is valid for 5 minutes for progressive loading
    static const unsigned long CACHE_VALIDITY_MS = 5 * 60 * 1000;
    return cacheAge < CACHE_VALIDITY_MS;
} 