#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "posthog/parsers/InsightParser.h"

/**
 * @brief Event types in the system
 */
enum class EventType {
    INSIGHT_DATA_RECEIVED,
    INSIGHT_FORCE_REFRESH,
    INSIGHT_DATA_ERROR,
    INSIGHT_NETWORK_STATE_CHANGED,
    UI_UPDATE_REQUESTED,
    WIFI_CREDENTIALS_FOUND,
    NEED_WIFI_CREDENTIALS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_CONNECTION_FAILED,
    WIFI_AP_STARTED,
    OTA_PROCESS_START,
    OTA_PROCESS_END,
    CARD_CONFIG_CHANGED,
    CARD_TITLE_UPDATED
};

/**
 * @brief Represents an event in the system
 */
struct Event {
    EventType type;                         // Type of event
    String insightId;                       // ID of the insight related to the event
    std::shared_ptr<InsightParser> parser;  // Optional parsed insight data
    String jsonData;                        // Raw JSON data for insights
    String title;                           // Title/name for card title updates
    
    Event() {}
    
    Event(EventType t, const String& id) : type(t), insightId(id), parser(nullptr) {}
    
    Event(EventType t, const String& id, std::shared_ptr<InsightParser> p)
        : type(t), insightId(id), parser(p) {}
        
    Event(EventType t, const String& id, const String& json)
        : type(t), insightId(id), parser(nullptr), jsonData(json) {}
        
    // Constructor for title update events
    static Event createTitleUpdateEvent(const String& id, const String& title_text) {
        Event e;
        e.type = EventType::CARD_TITLE_UPDATED;
        e.insightId = id;
        e.title = title_text;
        return e;
    }
};

/**
 * @brief Callback function type for event handlers
 */
using EventCallback = std::function<void(const Event&)>;

/**
 * @brief Thread-safe event queue for handling system events
 */
class EventQueue {
private:
    QueueHandle_t eventQueue;
    SemaphoreHandle_t callbackMutex;
    std::vector<EventCallback> eventCallbacks;
    
    static void eventProcessingTask(void* parameter);
    TaskHandle_t taskHandle;
    bool isRunning;
    
public:
    EventQueue(size_t queueSize = 10);
    ~EventQueue();
    
    /**
     * @brief Publish an event to the queue
     * 
     * @param eventType Type of the event
     * @param insightId ID of the insight related to the event
     * @return true if the event was successfully queued
     * @return false if the queue is full
     */
    bool publishEvent(EventType eventType, const String& insightId);
    
    /**
     * @brief Publish an event with parsed insight data
     * 
     * @param eventType Type of the event
     * @param insightId ID of the insight related to the event
     * @param parser Shared pointer to parsed insight data
     * @return true if the event was successfully queued
     * @return false if the queue is full
     */
    bool publishEvent(EventType eventType, const String& insightId, std::shared_ptr<InsightParser> parser);
    
    /**
     * @brief Publish an event with raw JSON data
     * 
     * @param eventType Type of the event
     * @param insightId ID of the insight related to the event
     * @param jsonData Raw JSON data string
     * @return true if the event was successfully queued
     * @return false if the queue is full
     */
    bool publishEvent(EventType eventType, const String& insightId, const String& jsonData);
    
    /**
     * @brief Alternative method to publish a pre-constructed Event
     * 
     * @param event The event to publish
     * @return true if the event was successfully queued
     * @return false if the queue is full
     */
    bool publishEvent(const Event& event);
    
    /**
     * @brief Subscribe to events
     * 
     * @param callback Function to call when an event is processed
     */
    void subscribe(EventCallback callback);
    
    /**
     * @brief Start the event processing task
     */
    void begin();
    
    /**
     * @brief Stop the event processing task
     */
    void end();
}; 