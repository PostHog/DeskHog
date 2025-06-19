#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <queue>
#include <vector>
#include <set>
#include <memory>
#include "../ConfigManager.h"
#include "SystemController.h"
#include "EventQueue.h"

/**
 * @class HomeAssistantClient
 * @brief Client for fetching Home Assistant data
 * 
 * Features:
 * - Queued entity state requests with retry logic
 * - Automatic refresh of entity states
 * - Thread-safe operation with event queue
 * - Configurable retry and refresh intervals
 * - Support for multiple entity types
 */
class HomeAssistantClient {
public:
    /**
     * @brief Constructor
     * 
     * @param config Reference to configuration manager
     * @param eventQueue Reference to event system
     */
    explicit HomeAssistantClient(ConfigManager& config, EventQueue& eventQueue);
    
    // Delete copy constructor and assignment operator
    HomeAssistantClient(const HomeAssistantClient&) = delete;
    void operator=(const HomeAssistantClient&) = delete;
    
    /**
     * @brief Queue an entity for immediate state fetch
     * 
     * @param entity_id ID of entity to fetch (e.g., "sensor.temperature")
     * 
     * Adds entity to request queue with retry count of 0.
     * Will be processed in FIFO order.
     */
    void requestEntityState(const String& entity_id);
    
    /**
     * @brief Call a Home Assistant service
     * 
     * @param domain Service domain (e.g., "light", "switch")
     * @param service Service name (e.g., "turn_on", "turn_off")
     * @param entity_id Entity to call service on
     * @param data Optional service data (JSON)
     * @return true if service call was successful
     */
    bool callService(const String& domain, const String& service, const String& entity_id, const String& data = "");
    
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
     * - Refreshing existing entity states
     */
    void process();
    
private:
    /**
     * @struct QueuedRequest
     * @brief Tracks a queued entity state request
     */
    struct QueuedRequest {
        String entity_id;      ///< ID of entity to fetch
        uint8_t retry_count;   ///< Number of retry attempts
    };
    
    // Configuration
    ConfigManager& _config;         ///< Configuration storage
    EventQueue& _eventQueue;        ///< Event system
    
    // Request tracking
    std::set<String> requested_entities;    ///< All known entity IDs
    std::queue<QueuedRequest> request_queue; ///< Queue of pending requests
    bool has_active_request;               ///< Request in progress flag
    HTTPClient _http;                      ///< HTTP client instance
    unsigned long last_refresh_check;       ///< Last refresh timestamp
    
    // Constants
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
     * @brief Check if entities need refreshing
     * 
     * Queues refresh requests for entities older than REFRESH_INTERVAL.
     */
    void checkRefreshes();
    
    /**
     * @brief Fetch entity state from Home Assistant
     * 
     * @param entity_id ID of entity to fetch
     * @param response String to store response
     * @return true if fetch was successful
     */
    bool fetchEntityState(const String& entity_id, String& response);
    
    /**
     * @brief Build entity state API URL
     * 
     * @param entity_id ID of entity
     * @return Complete API URL
     */
    String buildEntityStateUrl(const String& entity_id) const;
    
    /**
     * @brief Build service call API URL
     * 
     * @param domain Service domain
     * @param service Service name
     * @return Complete API URL
     */
    String buildServiceUrl(const String& domain, const String& service) const;
    
    // Event-related methods
    void publishEntityStateEvent(const String& entity_id, const String& response);
}; 
