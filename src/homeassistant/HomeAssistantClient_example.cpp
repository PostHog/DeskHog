/**
 * HomeAssistantClient Usage Example
 * =================================
 * 
 * This example demonstrates how to use the HomeAssistantClient to:
 * 1. Fetch entity states from Home Assistant
 * 2. Call Home Assistant services
 * 3. Handle configuration
 * 
 * To integrate into main.cpp, add similar code patterns.
 */

#include "homeassistant/HomeAssistantClient.h"
#include "ConfigManager.h"
#include "EventQueue.h"

// Global instances (similar to PostHogClient pattern in main.cpp)
HomeAssistantClient* homeAssistantClient;
ConfigManager* configManager;
EventQueue* eventQueue;

void setupHomeAssistant() {
    // Configure Home Assistant connection
    configManager->setHomeAssistantUrl("http://homeassistant.local:8123");
    configManager->setHomeAssistantApiKey("your_long_lived_access_token_here");
    
    // Create the client instance
    homeAssistantClient = new HomeAssistantClient(*configManager, *eventQueue);
}

void homeAssistantLoop() {
    // Call this in your main loop (similar to posthogClient->process())
    if (homeAssistantClient) {
        homeAssistantClient->process();
    }
}

void requestSensorData() {
    // Request entity states
    homeAssistantClient->requestEntityState("sensor.outdoor_temperature");
    homeAssistantClient->requestEntityState("sensor.indoor_humidity");
    homeAssistantClient->requestEntityState("light.living_room");
    homeAssistantClient->requestEntityState("switch.desk_lamp");
}

void controlDevices() {
    // Turn on a light
    homeAssistantClient->callService("light", "turn_on", "light.living_room", 
                                   "\"brightness\":255,\"color_name\":\"blue\"");
    
    // Turn off a switch
    homeAssistantClient->callService("switch", "turn_off", "switch.desk_lamp");
    
    // Set thermostat temperature
    homeAssistantClient->callService("climate", "set_temperature", "climate.living_room",
                                   "\"temperature\":22.5");
}

// Event handler example (add to your event handling system)
void handleHomeAssistantEvent(const Event& event) {
    if (event.type == EventType::HA_ENTITY_STATE_RECEIVED) {
        Serial.printf("Received state for entity %s: %s\n", 
                     event.insightId.c_str(), event.jsonData.c_str());
        
        // Parse the JSON response to extract state information
        // Example JSON response:
        // {
        //   "entity_id": "sensor.outdoor_temperature",
        //   "state": "23.5",
        //   "attributes": {
        //     "unit_of_measurement": "°C",
        //     "friendly_name": "Outdoor Temperature"
        //   }
        // }
    }
}

/**
 * Integration into main.cpp:
 * 
 * 1. Add include: #include "homeassistant/HomeAssistantClient.h"
 * 
 * 2. Add global variable: HomeAssistantClient* homeAssistantClient;
 * 
 * 3. In setup(), add:
 *    homeAssistantClient = new HomeAssistantClient(*configManager, *eventQueue);
 * 
 * 4. In the main loop task, add:
 *    homeAssistantClient->process();
 * 
 * 5. Configure through web portal or programmatically:
 *    configManager->setHomeAssistantUrl("http://your-ha-instance:8123");
 *    configManager->setHomeAssistantApiKey("your_token");
 * 
 * 6. Request entity states:
 *    homeAssistantClient->requestEntityState("sensor.temperature");
 * 
 * 7. Call services:
 *    homeAssistantClient->callService("light", "turn_on", "light.bedroom");
 */
