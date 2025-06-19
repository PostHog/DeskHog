/**
 * HomeAssistantCard Usage Example
 * ===============================
 * 
 * This example demonstrates how to use the HomeAssistantCard to display
 * Home Assistant entity data on the DeskHog display.
 */

#include "ui/CardController.h"
#include "homeassistant/HomeAssistantClient.h"

// Example: Creating Home Assistant cards in your main application

void setupHomeAssistantCards(CardController& cardController, HomeAssistantClient& haClient) {
    // Configure Home Assistant connection (do this once during setup)
    // This would typically be done through the web interface or stored configuration
    
    // Create cards for various Home Assistant entities
    
    // Temperature sensor
    cardController.createHomeAssistantCard("sensor.outdoor_temperature");
    
    // Humidity sensor  
    cardController.createHomeAssistantCard("sensor.indoor_humidity");
    
    // Power consumption sensor
    cardController.createHomeAssistantCard("sensor.power_consumption");
    
    // Binary sensors
    cardController.createHomeAssistantCard("binary_sensor.front_door");
    cardController.createHomeAssistantCard("binary_sensor.motion_living_room");
    
    // Switch states
    cardController.createHomeAssistantCard("switch.desk_lamp");
    cardController.createHomeAssistantCard("light.bedroom_light");
    
    // The cards will automatically:
    // 1. Request entity state data from Home Assistant
    // 2. Parse the JSON response using HomeAssistantParser
    // 3. Display the appropriate UI based on entity type:
    //    - Numeric sensors: Show value with unit (e.g., "23.5 °C")
    //    - Binary sensors: Show ON/OFF, OPEN/CLOSED
    //    - Switches/Lights: Show ON/OFF state
    // 4. Refresh data automatically every 30 seconds
    // 5. Handle unavailable/unknown states gracefully
}

/**
 * Example JSON responses that HomeAssistantCard can handle:
 * 
 * Numeric Sensor:
 * {
 *   "entity_id": "sensor.outdoor_temperature",
 *   "state": "23.5",
 *   "attributes": {
 *     "unit_of_measurement": "°C",
 *     "friendly_name": "Outdoor Temperature",
 *     "device_class": "temperature"
 *   }
 * }
 * 
 * Binary Sensor:
 * {
 *   "entity_id": "binary_sensor.front_door",
 *   "state": "off",
 *   "attributes": {
 *     "friendly_name": "Front Door",
 *     "device_class": "door"
 *   }
 * }
 * 
 * Switch:
 * {
 *   "entity_id": "switch.desk_lamp",
 *   "state": "on",
 *   "attributes": {
 *     "friendly_name": "Desk Lamp"
 *   }
 * }
 */

/**
 * Card Display Examples:
 * 
 * Temperature Sensor Card:
 * ┌─────────────────────────┐
 * │    Outdoor Temperature  │
 * │                         │
 * │        23.5            │
 * │         °C             │
 * └─────────────────────────┘
 * 
 * Binary Sensor Card:
 * ┌─────────────────────────┐
 * │      Front Door        │
 * │                         │
 * │        CLOSED          │
 * │                         │
 * └─────────────────────────┘
 * 
 * Switch Card:
 * ┌─────────────────────────┐
 * │       Desk Lamp        │
 * │                         │
 * │         ON             │
 * │                         │
 * └─────────────────────────┘
 */

/**
 * Integration with existing DeskHog workflow:
 * 
 * 1. The HomeAssistantCard uses the same event system as InsightCard
 * 2. Cards are managed by CardController alongside PostHog insight cards
 * 3. Navigation between Home Assistant and PostHog cards works seamlessly
 * 4. All cards share the same LVGL styling from Style.h
 * 5. Thread-safe UI updates are handled automatically
 * 6. Cards respond to the same button inputs for navigation
 */

/**
 * Configuration requirements:
 * 
 * Before creating Home Assistant cards, ensure:
 * 1. Home Assistant URL is configured: configManager.setHomeAssistantUrl("http://homeassistant.local:8123")
 * 2. API key is configured: configManager.setHomeAssistantApiKey("your_long_lived_access_token")
 * 3. WiFi is connected and HomeAssistantClient.isReady() returns true
 * 4. The specified entities exist in your Home Assistant instance
 */
