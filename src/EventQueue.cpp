#include "EventQueue.h"

// Constructor: Initialize members correctly
EventQueue::EventQueue(size_t queueSize) : 
    _queueHandle(nullptr), 
    _subscriberMutex(nullptr), 
    _taskHandle(nullptr), 
    _isRunning(false) 
{
    // Create the event queue
    _queueHandle = xQueueCreate(queueSize, sizeof(Event*)); // Store pointers to Events, not Events themselves
                                                             // This avoids issues with String copying in/out of queue
    if (!_queueHandle) {
        Serial.println("EventQueue Error: Failed to create queue!");
    }

    // Create mutex for callback access
    _subscriberMutex = xSemaphoreCreateMutex();
    if (!_subscriberMutex) {
        Serial.println("EventQueue Error: Failed to create mutex!");
    }
}

// Destructor: Call end() and clean up
EventQueue::~EventQueue() {
    end(); // Stop task, delete queue and mutex
}

// Remove unused publishEvent overloads
/*
bool EventQueue::publishEvent(EventType eventType, const String& insightId) {
    Event event(eventType, insightId);
    return publishEvent(event);
}

bool EventQueue::publishEvent(EventType eventType, const String& insightId, std::shared_ptr<InsightParser> parser) {
    Event event(eventType, insightId, parser);
    return publishEvent(event);
}

bool EventQueue::publishEvent(EventType eventType, const String& insightId, const String& jsonData) {
    // For large JSON data, we need to handle it carefully
    if (jsonData.length() > 8192) { // 8KB threshold
        Serial.printf("Large JSON detected (%u bytes), handling via event\n", jsonData.length());
    }
    
    Event event(eventType, insightId, jsonData);
    return publishEvent(event);
}
*/

// Publish an event by copying it to heap and sending pointer to queue
bool EventQueue::publishEvent(const Event& event) {
    if (!_queueHandle) return false;

    // Allocate memory for the event on the heap
    // Using new Event(event) creates a copy
    Event* eventPtr = new (std::nothrow) Event(event); 
    if (!eventPtr) {
        Serial.println("EventQueue Error: Failed to allocate memory for event!");
        return false;
    }

    // Add the event pointer to the queue
    if (xQueueSend(_queueHandle, &eventPtr, 0) != pdPASS) {
        Serial.println("EventQueue Error: Failed to send event to queue!");
        delete eventPtr; // Clean up if send failed
        return false;
    }
    return true;
}

// Subscribe: Match header signature and use correct members
void EventQueue::subscribe(std::function<void(const Event&)> callback) {
    if (!_subscriberMutex) return; // Check if mutex exists
    // Protect access to the callbacks vector
    if (xSemaphoreTake(_subscriberMutex, portMAX_DELAY) == pdTRUE) {
        _subscribers.push_back(callback);
        xSemaphoreGive(_subscriberMutex);
    }
}

// Begin: Use correct task runner and members
void EventQueue::begin() {
    if (!_isRunning && _queueHandle && _subscriberMutex) { // Check members are valid
        _isRunning = true;
        // Create a task to process events
        BaseType_t result = xTaskCreatePinnedToCore(
            eventTaskRunner,      // Static task runner function
            "EventQueueTask",     // Task name
            4096,                 // Stack size (adjust as needed)
            this,                 // Task parameter (pass the EventQueue instance)
            5,                    // Priority (adjust as needed, 5 is common)
            &_taskHandle,         // Task handle
            0                     // Core ID (0 is often protocol core)
        );
        if (result != pdPASS) {
            Serial.println("EventQueue Error: Failed to create event processing task!");
            _isRunning = false; // Reset flag if task creation failed
        }
    } else if (_isRunning) {
        Serial.println("EventQueue Info: begin() called but already running.");
    } else {
        Serial.println("EventQueue Error: begin() called but queue or mutex not initialized.");
    }
}

// End: Implement cleanup logic
void EventQueue::end() {
    if (_isRunning && _taskHandle != nullptr) {
        _isRunning = false; // Signal task to stop
        
        // Wait a short time for the task to process the flag and finish
        vTaskDelay(pdMS_TO_TICKS(150)); 
        
        // Check if task deleted itself, otherwise delete it
        // Note: It's generally better for tasks to delete themselves upon seeing the flag.
        // But we add a vTaskDelete as a fallback.
        if (eTaskGetState(_taskHandle) != eDeleted) {
             vTaskDelete(_taskHandle);
        }
        _taskHandle = nullptr;
    } else if (_taskHandle != nullptr) {
        // Task handle exists but not running? Still try to delete.
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
    }
    _isRunning = false; // Ensure flag is false

    // Clean up queue: Empty it first to delete any remaining event pointers
    if (_queueHandle) {
        Event* eventPtr = nullptr;
        while(xQueueReceive(_queueHandle, &eventPtr, 0) == pdPASS) {
            if (eventPtr) {
                delete eventPtr;
            }
        }
        vQueueDelete(_queueHandle);
        _queueHandle = nullptr;
    }
    
    // Clean up mutex
    if (_subscriberMutex) {
        vSemaphoreDelete(_subscriberMutex);
        _subscriberMutex = nullptr;
    }

    // Clear subscribers (optional, depends if EventQueue might be reused)
    // If subscribers vector holds references/pointers that need cleanup, do it here.
    // Assuming std::function doesn't need manual cleanup beyond vector clear.
    if (_subscriberMutex) { // Check if mutex was successfully created before using
      if (xSemaphoreTake(_subscriberMutex, pdMS_TO_TICKS(100)) == pdTRUE) { // Use timeout take
          _subscribers.clear();
          xSemaphoreGive(_subscriberMutex);
      } else {
        // Handle mutex timeout if needed, maybe log an error
      }
    } else {
      _subscribers.clear(); // Clear anyway if mutex wasn't created
    }
}

// Static task runner: Calls the instance method
void EventQueue::eventTaskRunner(void* parameter) {
    EventQueue* self = static_cast<EventQueue*>(parameter);
    if (self) {
        self->processEventQueueTask();
    }
    // If self is null, the task cannot run, delete it
    vTaskDelete(NULL); 
}

// Instance method run by the task
void EventQueue::processEventQueueTask() {
    Event* eventPtr = nullptr;
    
    while (_isRunning) {
        // Wait for an event pointer (block until an event arrives)
        if (_queueHandle && xQueueReceive(_queueHandle, &eventPtr, pdMS_TO_TICKS(100)) == pdPASS) {
            if (eventPtr) {
                 // Process the event by calling all registered callbacks
                if (_subscriberMutex && xSemaphoreTake(_subscriberMutex, portMAX_DELAY) == pdTRUE) {
                    // Make a copy of the event data to pass to callbacks, 
                    // in case a callback modifies the original unintentionally.
                    // Although passing const Event& should prevent modification.
                    const Event currentEvent = *eventPtr; 
                    
                    for (const auto& callback : _subscribers) {
                        if(callback) { // Ensure callback is valid
                           callback(currentEvent); // Pass the const reference to the event data
                        }
                    }
                    xSemaphoreGive(_subscriberMutex);
                } else if (!_subscriberMutex) {
                     Serial.println("EventQueue Error: Cannot process event, subscriber mutex invalid.");
                }
                // Delete the event object from heap after processing
                delete eventPtr;
                eventPtr = nullptr;
            } else {
                 Serial.println("EventQueue Warning: Received NULL event pointer from queue.");
            }
        } 
        // No need for explicit vTaskDelay(1) here, xQueueReceive provides blocking with timeout.
        // If timeout occurs (100ms), the loop continues checking _isRunning.
    }
    
    // Task cleanup: Will exit loop when _isRunning is false
    Serial.println("EventQueue task exiting.");
}
