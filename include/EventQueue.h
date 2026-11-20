#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

/**
 * @brief What can happen that other parts of the firmware want to know about
 */
enum class EventType {
    WIFI_CREDENTIALS_FOUND,
    NEED_WIFI_CREDENTIALS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_CONNECTION_FAILED,
    WIFI_AP_STARTED,
    CARD_CONFIG_CHANGED
};

/**
 * @brief An event: only what happened. Whoever cares reads the details from
 * where they live (saved settings, the WiFi interface).
 *
 * Events are copied byte by byte through a FreeRTOS queue, so this must stay
 * a plain value: no String or other member that owns memory.
 */
struct Event {
    EventType type;
};

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
     * @brief Publish an event
     * @return false if the queue is full
     */
    bool publishEvent(EventType eventType);

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