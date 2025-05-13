#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include <FreeRTOS.h> // For QueueHandle_t, SemaphoreHandle_t, TaskHandle_t
#include <queue.h>    // For xQueueCreate, xQueueSend, xQueueReceive
#include <semphr.h>   // For xSemaphoreCreateMutex, xSemaphoreTake, xSemaphoreGive

// Event types enum
enum class EventType {
    // WiFi Events
    WIFI_CREDENTIALS_FOUND,
    NEED_WIFI_CREDENTIALS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_CONNECTION_FAILED,
    WIFI_AP_STARTED,

    // PostHog API Configuration Events
    POSTHOG_API_CONFIGURED,
    POSTHOG_API_AWAITING_CONFIG,

    // Insight Events
    INSIGHT_DATA_RECEIVED,    // id=insightId, payload=json_string
    INSIGHT_FETCH_FAILED,     // id=insightId, data=error_message
    INSIGHT_ADDED,            // id=insightId
    INSIGHT_DELETED,          // id=insightId
    REQUEST_INSIGHT_DATA,     // id=insightId

    // System Events (Placeholder, adjust as needed)
    SYSTEM_STATE_CHANGED,     // data can hold new state info if needed

    // OTA Events (Placeholder)
    OTA_UPDATE_AVAILABLE,
    OTA_UPDATE_STARTED,
    OTA_UPDATE_PROGRESS,
    OTA_UPDATE_FINISHED,
    OTA_UPDATE_FAILED,
    
    // Time Update Event
    TIME_UPDATE               // data = formatted time string (e.g., "HH:MM")
};

// Event data structure
struct Event {
    EventType type;
    String id;      // Typically used for insight_id or other identifiers
    String data;    // General purpose data, like error messages, or the time string
    String payload; // For larger data like JSON payloads

    Event(EventType t, const String& eventId = "", const String& eventData = "", const String& eventPayload = "")
        : type(t), id(eventId), data(eventData), payload(eventPayload) {}
};

class EventQueue {
public:
    EventQueue(size_t maxSize);
    ~EventQueue();

    void begin(); // To start the processing task
    void end();   // <-- Add end method declaration

    bool publishEvent(const Event& event);
    // Overload for simple events with just type and a single string (can be id or data based on context)
    // bool publishEvent(EventType type, const String& string_param); // <-- Remove this confusing overload

    void subscribe(std::function<void(const Event&)> callback);
    
    // This method will be run in a task to process events from the queue
    // and call subscribers. It's made public for xTaskCreate but not meant for direct user calls.
    void processEventQueueTask(); 

private:
    QueueHandle_t _queueHandle;
    std::vector<std::function<void(const Event&)>> _subscribers;
    SemaphoreHandle_t _subscriberMutex;
    TaskHandle_t _taskHandle; // <-- Use _taskHandle to match cpp usage expectation
    bool _isRunning;          // <-- Add _isRunning member

    static void eventTaskRunner(void* param); // Static C-style function to pass to xTaskCreate
}; 