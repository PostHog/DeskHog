# Home Assistant Integration for DeskHog

This integration allows DeskHog to display Home Assistant entity data alongside PostHog analytics, creating a unified dashboard for both data analytics and smart home monitoring.

## 📁 Files Created

### Core Components
- **`HomeAssistantClient.h/cpp`** - HTTP client for Home Assistant API integration
- **`HomeAssistantParser.h/cpp`** - JSON parser for Home Assistant entity state data
- **`HomeAssistantCard.h/cpp`** - UI card component for displaying entity data
- **`ConfigManager.h/cpp`** (extended) - Added Home Assistant URL and API key storage

### Examples and Documentation
- **`HomeAssistantClient_example.cpp`** - Usage examples for the client
- **`HomeAssistantCard_example.cpp`** - Card creation and integration examples

## 🏠 Supported Entity Types

### Numeric Sensors
- **Temperature** sensors (°C, °F)
- **Humidity** sensors (%)
- **Power consumption** (W, kW)
- **Pressure** sensors (hPa, bar)
- **Any numeric sensor** with unit display

### Binary Sensors
- **Motion** detectors (ON/OFF)
- **Door/Window** sensors (OPEN/CLOSED)
- **Occupancy** sensors (DETECTED/CLEAR)

### Switches & Lights
- **Switch** entities (ON/OFF)
- **Light** entities (ON/OFF)

## 🔧 Configuration

### 1. Home Assistant Setup
```cpp
// Configure connection (typically done through web interface)
configManager->setHomeAssistantUrl("http://homeassistant.local:8123");
configManager->setHomeAssistantApiKey("your_long_lived_access_token_here");
```

### 2. Create Client Instance
```cpp
// In main.cpp or CardController setup
HomeAssistantClient* haClient = new HomeAssistantClient(*configManager, *eventQueue);
```

### 3. Create Entity Cards
```cpp
// Create cards for different entity types
cardController->createHomeAssistantCard("sensor.outdoor_temperature");
cardController->createHomeAssistantCard("binary_sensor.front_door");
cardController->createHomeAssistantCard("switch.desk_lamp");
```

## 📊 Card Display Examples

### Temperature Sensor
```
┌─────────────────────────┐
│   Outdoor Temperature   │
│                         │
│        23.5            │
│         °C             │
└─────────────────────────┘
```

### Binary Sensor
```
┌─────────────────────────┐
│      Front Door        │
│                         │
│       CLOSED           │
│                         │
└─────────────────────────┘
```

### Switch/Light
```
┌─────────────────────────┐
│       Desk Lamp        │
│                         │
│         ON             │
│                         │
└─────────────────────────┘
```

## 🔄 Data Flow

1. **Request Phase**: `homeAssistantClient->requestEntityState("sensor.temperature")`
2. **HTTP Request**: Client sends GET request to `/api/states/sensor.temperature`
3. **Response Parsing**: HomeAssistantParser extracts state, unit, friendly name
4. **Event Publishing**: `HA_ENTITY_STATE_RECEIVED` event published
5. **UI Update**: HomeAssistantCard receives event and updates display
6. **Auto Refresh**: Client automatically refreshes data every 30 seconds

## 🎛️ API Integration

### Entity State Endpoint
```
GET /api/states/{entity_id}
Authorization: Bearer {long_lived_access_token}
```

**Response Example:**
```json
{
  "entity_id": "sensor.outdoor_temperature",
  "state": "23.5",
  "attributes": {
    "unit_of_measurement": "°C",
    "friendly_name": "Outdoor Temperature",
    "device_class": "temperature"
  },
  "last_changed": "2025-06-19T10:30:00.000000+00:00"
}
```

### Service Call Endpoint (Future Enhancement)
```
POST /api/services/{domain}/{service}
Authorization: Bearer {long_lived_access_token}
Content-Type: application/json

{"entity_id": "light.bedroom", "brightness": 255}
```

## 🔒 Authentication

Uses Home Assistant **Long-Lived Access Tokens**:

1. Go to Home Assistant → Profile → Long-Lived Access Tokens
2. Click "Create Token"
3. Copy the token and configure in DeskHog
4. Token provides read/write access to all entities

## 🧵 Thread Safety

- **UI Updates**: All LVGL operations dispatched to UI thread via `globalUIDispatch`
- **HTTP Requests**: Non-blocking with queue management
- **Memory Management**: PSRAM allocation for large responses
- **State Management**: Thread-safe event publishing and subscription

## 🔧 Error Handling

### Connection Issues
- Automatic retry logic with exponential backoff
- Queue management for failed requests
- Graceful degradation when Home Assistant unavailable

### Entity States
- **Unavailable entities**: Display "Unavailable" message
- **Unknown states**: Display "Unknown" message  
- **Parse errors**: Display "Data Error" message
- **Non-numeric sensors**: Display raw state string

## 🚀 Integration with DeskHog

### Event System
- Uses existing `EventQueue` for state change notifications
- New event types: `HA_ENTITY_STATE_RECEIVED`, `HA_SERVICE_CALLED`
- Compatible with existing event handling architecture

### Card Management
- Managed by `CardController` alongside PostHog insight cards
- Same navigation system (button inputs for card switching)
- Unified styling using `Style.h` fonts and colors

### Configuration Storage
- Persistent storage in ESP32 NVS (Non-Volatile Storage)
- Configuration via web interface (same as PostHog setup)
- Validation and size limits for stored values

## 🎯 Usage Scenarios

### Home Office Dashboard
- **Temperature/Humidity**: Monitor office environment
- **Power Consumption**: Track energy usage
- **Security**: Door/window sensors, motion detection
- **Lighting**: Quick status of office lighting

### Smart Home Monitor
- **HVAC Status**: Temperature, humidity, thermostat states
- **Security System**: Armed/disarmed, sensor states
- **Energy Monitoring**: Solar production, grid consumption
- **Device Status**: Critical switches and lights

## 🔮 Future Enhancements

### Service Control
- Touch interface for controlling switches/lights
- Brightness/color controls for lights
- Thermostat temperature adjustment

### Advanced Visualizations
- Historical data graphs (like PostHog insights)
- Multi-entity dashboards
- Custom aggregations

### Configuration UI
- Web interface for entity selection
- Card layout customization
- Refresh interval configuration

## 🏃‍♂️ Quick Start

1. **Configure Home Assistant connection**:
   ```cpp
   configManager->setHomeAssistantUrl("http://192.168.1.100:8123");
   configManager->setHomeAssistantApiKey("your_token_here");
   ```

2. **Create Home Assistant client**:
   ```cpp
   homeAssistantClient = new HomeAssistantClient(*configManager, *eventQueue);
   ```

3. **Add to main loop**:
   ```cpp
   homeAssistantClient->process();
   ```

4. **Create cards for your entities**:
   ```cpp
   cardController->createHomeAssistantCard("sensor.outdoor_temp");
   cardController->createHomeAssistantCard("binary_sensor.front_door");
   ```

5. **Navigate using existing button controls** to view Home Assistant data alongside PostHog analytics!

The Home Assistant integration seamlessly extends DeskHog's capabilities, providing a unified view of both business analytics and smart home data on a single, elegant desk display.
- **Service Calls**: Execute Home Assistant services to control devices
- **Queue Management**: Automatic retry logic and background refresh of entity states
- **Event System**: Integration with the device's event queue for real-time updates

## Configuration

The client requires two configuration parameters:

- **URL**: Your Home Assistant instance URL (e.g., `http://homeassistant.local:8123`)
- **API Key**: A long-lived access token from Home Assistant

### Setting up the API Key

1. In Home Assistant, go to Profile → Long-Lived Access Tokens
2. Click "Create Token"
3. Give it a descriptive name (e.g., "DeskHog Device")
4. Copy the generated token

### Configuration Methods

```cpp
// Set configuration programmatically
configManager->setHomeAssistantUrl("http://homeassistant.local:8123");
configManager->setHomeAssistantApiKey("your_long_lived_access_token_here");

// Get configuration
String url = configManager->getHomeAssistantUrl();
String apiKey = configManager->getHomeAssistantApiKey();

// Clear configuration
configManager->clearHomeAssistantConfig();
```

## Usage

### Basic Setup

```cpp
#include "homeassistant/HomeAssistantClient.h"

// Create client instance
HomeAssistantClient* haClient = new HomeAssistantClient(*configManager, *eventQueue);

// In your main loop
haClient->process();
```

### Requesting Entity States

```cpp
// Request current state of entities
haClient->requestEntityState("sensor.outdoor_temperature");
haClient->requestEntityState("light.living_room");
haClient->requestEntityState("switch.desk_lamp");
```

### Calling Services

```cpp
// Turn on a light with specific settings
haClient->callService("light", "turn_on", "light.living_room", 
                     "\"brightness\":255,\"color_name\":\"blue\"");

// Turn off a switch
haClient->callService("switch", "turn_off", "switch.desk_lamp");

// Set thermostat temperature
haClient->callService("climate", "set_temperature", "climate.living_room",
                     "\"temperature\":22.5");
```

## Events

The client publishes events to the event queue:

- `HA_ENTITY_STATE_RECEIVED`: When an entity state is successfully fetched
- `HA_SERVICE_CALLED`: When a service call is completed (future enhancement)

### Handling Events

```cpp
void handleEvent(const Event& event) {
    if (event.type == EventType::HA_ENTITY_STATE_RECEIVED) {
        Serial.printf("Entity %s state: %s\n", 
                     event.insightId.c_str(), event.jsonData.c_str());
        
        // Parse JSON response to extract state and attributes
        // event.jsonData contains the full entity state JSON
    }
}
```

## API Endpoints Used

- **Entity States**: `GET /api/states/{entity_id}`
- **Service Calls**: `POST /api/services/{domain}/{service}`

## Features

- **Automatic Retry**: Failed requests are retried up to 3 times
- **Background Refresh**: Entity states are refreshed every 30 seconds
- **Memory Optimization**: Large responses are allocated in PSRAM
- **Queue Management**: Non-blocking operation with request queuing
- **Error Handling**: Comprehensive error logging and recovery

## Integration with DeskHog

The HomeAssistantClient follows the same patterns as PostHogClient:

1. Created in `setup()` with ConfigManager and EventQueue references
2. Called in the main loop via `process()`
3. Configured through the web portal or programmatically
4. Publishes events for UI updates

This allows seamless integration with the existing DeskHog architecture for displaying Home Assistant data alongside PostHog analytics.
