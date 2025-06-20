#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <queue>
#include <vector>
#include <set>
#include <memory>
#include "../ConfigManager.h"
#include "SystemController.h"
#include "EventQueue.h"
#include "parsers/InsightParser.h"

/**
 * @class PostHogClient
 * @brief Client for fetching PostHog insight data
 * 
 * Features:
 * - Queued insight requests with retry logic
 * - Automatic refresh of insights
 * - Thread-safe operation with event queue
 * - Configurable retry and refresh intervals
 * - Support for multiple insight types
 */
class PostHogClient {
public:
    /**
     * @brief Constructor
     * 
     * @param config Reference to configuration manager
     * @param eventQueue Reference to event system
     */
    explicit PostHogClient(ConfigManager& config, EventQueue& eventQueue);
    
    // Delete copy constructor and assignment operator
    PostHogClient(const PostHogClient&) = delete;
    void operator=(const PostHogClient&) = delete;
    
    /**
     * @brief Queue an insight for immediate fetch
     * 
     * @param insight_id ID of insight to fetch
     * 
     * Adds insight to request queue with retry count of 0.
     * Will be processed in FIFO order.
     */
    void requestInsightData(const String& insight_id);
    
    /**
     * @brief Check if client is ready for operation
     * 
     * @return true if configured and connected
     */
    bool isReady() const;
    
    /**
     * @brief Process queued requests and refreshes
     * 
     * Should be called regularly in main loop.
     * Handles:
     * - Processing queued requests
     * - Retrying failed requests
     * - Refreshing existing insights
     */
    void process();
    
private:
    /**
     * @struct QueuedRequest
     * @brief Tracks a queued insight request
     */
    struct QueuedRequest {
        String insight_id;     ///< ID of insight to fetch
        uint8_t retry_count;   ///< Number of retry attempts
    };
    
    // Configuration
    ConfigManager& _config;         ///< Configuration storage
    EventQueue& _eventQueue;        ///< Event system
    
    // Request tracking
    std::set<String> requested_insights;  ///< All known insight IDs
    std::queue<QueuedRequest> request_queue; ///< Queue of pending requests
    bool has_active_request;               ///< Request in progress flag
    WiFiClientSecure _secureClient;        ///< Secure WiFi client for HTTPS
    HTTPClient _http;                      ///< HTTP client instance
    unsigned long last_refresh_check;       ///< Last refresh timestamp
    
    // Constants
    static const char* BASE_URL;                        ///< PostHog API base URL
    static const unsigned long REFRESH_INTERVAL = 30000; ///< Refresh every 30s
    static const uint8_t MAX_RETRIES = 3;              ///< Max retry attempts
    static const unsigned long RETRY_DELAY = 1000;      ///< Delay between retries
    
    /**
     * @brief Handle system state changes
     * @param state New system state
     */
    void onSystemStateChange(SystemState state);
    
    /**
     * @brief Process pending requests in queue
     * 
     * Handles retry logic and request timeouts.
     */
    void processQueue();
    
    /**
     * @brief Check if insights need refreshing
     * 
     * Queues refresh requests for insights older than REFRESH_INTERVAL.
     */
    void checkRefreshes();
    
    /**
     * @brief Fetch insight data from PostHog
     * 
     * @param insight_id ID of insight to fetch
     * @param response String to store response
     * @return true if fetch was successful
     */
    bool fetchInsight(const String& insight_id, String& response);
    
    /**
     * @brief Build insight API URL
     * 
     * @param insight_id ID of insight
     * @param refresh_mode Cache control mode
     * @return Complete API URL
     */
    String buildInsightUrl(const String& insight_id, const char* refresh_mode = "force_cache") const;
    
    // Event-related methods
    void publishInsightDataEvent(const String& insight_id, const String& response);
}; 