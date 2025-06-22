# Dynamic Card Configuration System

This document describes DeskHog's dynamic card configuration system.

## Overview

DeskHog now gives users complete control over the cards displayed on their screen through a web UI. Developers can easily build and register new card types with rich configuration forms, which users can then configure and use.

## Features

### Dynamic Configuration System
- **Multiple field types**: TEXT, TEXTAREA, SELECT, BOOLEAN
- **Rich validation**: Required fields, default values, help text
- **Automatic form generation**: Web UI creates forms based on C++ definitions

### Data Structures (`src/config/CardConfig.h`)

#### `ConfigFieldType` Enum
```cpp
enum class ConfigFieldType {
    TEXT,       // Single-line text input
    SELECT,     // Dropdown selection from predefined options
    TEXTAREA,   // Multi-line text input
    BOOLEAN     // Checkbox/toggle for true/false values
};
```

#### `ConfigField` Struct
```cpp
struct ConfigField {
    String key;                         // Unique identifier for this config field
    String inputLabel;                  // Label shown to user for this field
    String uiDescription;               // Detailed description/help text
    ConfigFieldType type;               // Type of input field to render
    std::vector<String> options;       // Available options (for SELECT type)
    String defaultValue;                // Default value for the field
    bool required;                      // Whether this field is mandatory
};
```

#### `CardType` Enum
```cpp
enum class CardType {
    INSIGHT,
    FRIEND
    // New card types can be added here
};
```

#### `CardConfig` Struct
```cpp
struct CardConfig {
    CardType type;                      // The type of card (enum value)
    std::map<String, String> config;    // Dynamic configuration key-value pairs
    int order;                          // Display order in the card stack
    String name;                        // Human-readable name
    
    // Helper methods for easy config access
    String getConfig(const String& key, const String& defaultValue = "") const;
    void setConfig(const String& key, const String& value);
};
```

#### `CardDefinition` Struct
```cpp
struct CardDefinition {
    CardType type;                                  // The type of card this definition describes
    String name;                                    // Human-readable name
    bool allowMultiple;                             // Can the user add more than one?
    String uiDescription;                           // Description shown to user in web UI
    std::vector<ConfigField> configFields;         // Dynamic configuration fields
    
    // Factory function
    std::function<lv_obj_t*(const std::map<String, String>& configValues)> factory;
};
```

## Implementation

### Persistent Storage
- **Added `_cardPrefs`** to `ConfigManager` class for card-specific preferences
- **Dynamic JSON storage** supporting modern configuration formats
- **Implemented methods**:
  - `std::vector<CardConfig> getCardConfigs()`: Reads and deserializes JSON from preferences
  - `bool saveCardConfigs(const std::vector<CardConfig>& configs)`: Serializes and saves dynamic configs

### Web UI
- **Comprehensive card management** system
- **Added "Card Management" section** with:
  - Dynamic form generation based on `ConfigField` definitions
  - Add buttons that respect `allowMultiple` settings
  - Drag-and-drop reordering with visual feedback
  - Delete functionality with confirmation
  - Status indicators showing instance counts
  - Rich configuration display in card lists

### Dynamic Configuration Forms
- **TEXT fields**: Single-line inputs with validation
- **TEXTAREA fields**: Multi-line inputs for longer content
- **SELECT fields**: Dropdowns with predefined options
- **BOOLEAN fields**: Checkboxes for true/false values
- **Validation**: Required field checking with visual feedback
- **Help text**: Contextual descriptions for each field

### Example: PostHog Insight Card Configuration
Now supports:
- **Insight ID** (required text field) - The PostHog insight ID to display
- Multiple instances allowed

## API Endpoints

All API endpoints have been implemented in `CaptivePortal` with full support for dynamic configuration.

### `GET /api/cards/definitions`
- **Description**: Returns all available card types with their dynamic configuration fields
- **Implementation**: `CaptivePortal::handleGetCardDefinitions()`
- **Response Body**: Enhanced JSON with dynamic configuration support
  ```json
  [
    {
      "id": "INSIGHT",
      "name": "PostHog insight",
      "allowMultiple": true,
      "description": "Insight cards let you keep an eye on PostHog data",
      "configFields": [
        {
          "key": "insight_id",
          "inputLabel": "Insight ID",
          "uiDescription": "Enter the PostHog insight ID you want to display",
          "type": "TEXT",
          "required": true,
          "defaultValue": ""
        }
      ]
    }
  ]
  ```

### `GET /api/cards/configured`
- **Description**: Returns user's configured cards with dynamic configuration data
- **Implementation**: `CaptivePortal::handleGetConfiguredCards()`
- **Response Body**: Dynamic configuration support
  ```json
  [
    {
      "type": "INSIGHT",
      "name": "My Revenue Metrics",
      "order": 0,
      "configMap": {
        "insight_id": "abc123"
      }
    }
  ]
  ```

### `POST /api/cards/configured`
- **Description**: Saves complete card configuration with dynamic config support
- **Implementation**: `CaptivePortal::handleSaveConfiguredCards()`
- **Request Body**: Supports dynamic configuration formats
- **Response**: `{ "success": true, "message": "Card configuration saved successfully" }`

## CardController Implementation

`CardController` has been fully implemented as the central hub for card management:

### Card Type Registration
- **`initializeCardTypes()`**: Registers built-in cards with dynamic configuration support
- **`registerCardType()`**: Allows registration of new card types
- **Factory functions**: Support dynamic configuration
- **Built-in registrations**:
  - **INSIGHT**: Dynamic config with insight_id (required)
  - **FRIEND**: No configuration needed

### Configuration Change Handling
- **Event subscription**: Listens for `CARD_CONFIG_CHANGED` events from `ConfigManager`
- **`handleCardConfigChanged()`**: Processes configuration updates
- **`reconcileCards()`**: Performs complete card reconciliation:
  1. **Thread-safe operation**: Dispatches to LVGL task with mutex protection
  2. **Clean slate approach**: Removes all dynamic cards and rebuilds from config
  3. **Factory-based creation**: Uses registered factory functions for card instantiation
  4. **Ordering preservation**: Maintains user-defined card order
  5. **Position restoration**: Attempts to restore user's current card position

### API Integration
- **`getCardDefinitions()`**: Provides card definitions to `CaptivePortal`
- **Dynamic config support**: All definitions include `configFields` for UI generation

### Thread Safety & Architecture
- **No architectural changes**: Preserved existing task/thread management
- **UI queue utilization**: Uses existing `dispatchToLVGLTask()` for safe UI updates
- **Mutex protection**: Maintains display interface mutex discipline
- **Event-driven updates**: Leverages existing `EventQueue` system

## Key Implementation Details

### Clean Architecture
- **Modern configuration**: Dynamic field-based configuration system
- **Type safety**: Strongly-typed configuration with validation

### Dynamic Configuration Benefits
- **Rich forms**: Multi-field configuration with validation
- **Type safety**: Strongly-typed field definitions
- **Automatic UI**: Web forms generated from C++ definitions
- **Extensibility**: Easy to add new field types and validation

### Thread Safety
- **Preserved architecture**: No changes to core task management
- **Safe UI updates**: All LVGL operations properly dispatched
- **Mutex protection**: Display interface mutations properly protected
- **Event-driven**: Configuration changes handled asynchronously

## Notes

- **`ProvisioningCard`**: Remains a standard card at the top of the stack (not user-configurable)
- **Architecture preservation**: Successfully avoided changes to task/thread management
- **Performance**: Efficient reconciliation with minimal UI disruption
- **Extensibility**: Easy for developers to add new card types with rich configuration

## Adding New Card Types

### Example: Weather Card Registration

Here's how to add a new weather card type with multiple configuration fields:

```cpp
void CardController::initializeCardTypes() {
    // ... existing registrations ...
    
    // Register WEATHER card type with rich configuration
    std::vector<ConfigField> weatherFields = {
        ConfigField(
            "location", 
            "Location", 
            "Enter city name (e.g., 'San Francisco, CA')", 
            ConfigFieldType::TEXT, 
            "San Francisco, CA",  // default value
            true                  // required
        ),
        ConfigField(
            "units", 
            "Temperature Units", 
            "Choose temperature display format",
            {"Celsius", "Fahrenheit"},  // SELECT options
            "Celsius",                  // default value
            true                        // required
        ),
        ConfigField(
            "show_forecast", 
            "Show 5-day forecast", 
            "Display extended weather forecast below current conditions",
            ConfigFieldType::BOOLEAN, 
            "true",  // default value
            false    // not required
        ),
        ConfigField(
            "refresh_minutes", 
            "Refresh Interval (minutes)", 
            "How often to update weather data",
            ConfigFieldType::TEXT, 
            "30", 
            false
        )
    };
    
    CardDefinition weatherDef(
        CardType::WEATHER, 
        "Weather Display", 
        true,  // allow multiple instances
        "Shows current weather and forecast for any location", 
        weatherFields
    );
    
    // Set up factory function
    weatherDef.factory = [this](const std::map<String, String>& configValues) -> lv_obj_t* {
        String location = configValues.find("location")->second;
        String units = configValues.find("units")->second;
        bool showForecast = configValues.find("show_forecast")->second == "true";
        int refreshMinutes = configValues.find("refresh_minutes")->second.toInt();
        
        // Create new weather card
        WeatherCard* newCard = new WeatherCard(
            screen,
            eventQueue,
            location,
            units,
            showForecast,
            refreshMinutes,
            screenWidth,
            screenHeight
        );
        
        if (newCard && newCard->getCard()) {
            // Add to tracking list if needed
            weatherCards.push_back(newCard);
            
            // Start weather data fetching
            weatherClient.requestWeatherData(location, units);
            
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    
    registerCardType(weatherDef);
}
```

### Required Steps:

1. **Add to CardType enum** (in `CardConfig.h`):
   ```cpp
   enum class CardType {
       INSIGHT,
       FRIEND,
       WEATHER  // Add your new type
   };
   ```

2. **Update string conversion helpers** (in `CardConfig.h`):
   ```cpp
   inline String cardTypeToString(CardType type) {
       switch (type) {
           case CardType::INSIGHT: return "INSIGHT";
           case CardType::FRIEND: return "FRIEND";
           case CardType::WEATHER: return "WEATHER";  // Add this
           default: return "UNKNOWN";
       }
   }
   
   inline CardType stringToCardType(const String& str) {
       if (str == "INSIGHT") return CardType::INSIGHT;
       if (str == "FRIEND") return CardType::FRIEND;
       if (str == "WEATHER") return CardType::WEATHER;  // Add this
       return CardType::INSIGHT; // Default fallback
   }
   ```

3. **Create your card class** (implement `WeatherCard` with LVGL UI)

4. **Register in initializeCardTypes()** as shown above

The web UI will automatically generate configuration forms based on your `ConfigField` definitions!