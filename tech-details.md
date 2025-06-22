# DeskHog technical details

Microcontrollers are a pain. They've got limited memory and, for our purposes here, you've got to write C++ 🫠

But in exchange, our code can touch reality like no other kind of project. Here's what we're dealing with.

### Core and task isolation

If you've ever written mobile code, you'll feel right at home: we can only update the UI via the UI thread, otherwise the board crashes.

We've got two cores and multiple "tasks" assigned between them – task is [FreeRTOS](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESP32/examples/FreeRTOS/BasicMultiThreading)-speak for threads:

**Core 0 (Protocol CPU) tasks:**

- WiFi
- Web portal server
- Insight parsing
- NeoPixel control

**Core 1 (Application CPU) tasks:**

- LVGL tick (maintains timing, animations, etc for the graphics library) 
- UI: screen drawing and input handling

We have to keep this stuff carefully isolated or we're going to crash.

### Buttons

<img width="500" alt="diagram" src="https://github.com/user-attachments/assets/14ea2440-90d8-4540-bebb-045c18fbbc99" />

If the board isn't responding:

- Hold **▼ (Page down/D0)**
- Press **Reset**
- Release **▼ (Page down/D0)**

The board will restart in bootloader mode, where it can be re-flashed using PlatformIO.

### Pin definitions

You don't have to guess the pin definitions. You'll find them documented here:

~/.platformio/packages/framework-arduinoespressif32/variants/adafruit_feather_esp32s3_reversetft/pins_arduino.h

## UI progress

- Status card: working
- WiFi provisioning card with QR Code: working
- Friend card to give you (mild) reassurance: working
- Numeric card for Big Number insights: working
- Funnel card: needs a redesign; probably should be horizontal layout instead, won't display more than three steps right now
- Line graph card: working decently, but could use more detail
- Other insights: not yet supported

## Important components

### Event queue

`EventQueue` is how the project manages communication between tasks and prevents coupling. Events – changes via the web UI, returned requests from the PostHog client – are dispatched out of core 0 to be received by the UI task. Any important data can be safely copied from one context into the other, preventing crashes and other drama.

### Card stack

The UI is a stack of cards. The user navigates between them using built-in buttons (the arrow keys)

`CardNavigationStack` manages the UI presentation of these cards, animating transitions.

`CardController` manages updates to the stack contents. If an insight is deleted or added via web UI, the controller processes that update reactively.

### Web UI

A basic provisioning and configuration UI is provided. You can access it via a QR code on first launch, and by the IP shown in the status screen once WiFi is configured.

Open `html/portal.html` in your browser to preview changes. The contents of `html` are inlined into a single file on each build by `htmlconvert.py`.

**Web portal budget:** Right now the portal costs about 18KB. We'll allow up to **100KB**. All portal assets must be locally available, since the portal needs to work when the device doesn't have WiFi. If you want to try adding a more complex UI framework than hand-rolled JS and HTML, you're welcome to try as long as its build system is quick and the final static output is under 100KB.

### Card system

The DeskHog uses a dynamic card system that allows users to configure which cards appear on their device through a web UI. Cards are managed by the `CardController` and can be easily extended by developers.

#### Built-in cards

`ProvisioningCard` displays a QR code to connect to the device. If WiFi is connected, it displays connection stats.

`InsightCard` visualizes PostHog data. Numeric card is working best. The rest need help.

`FriendCard` lets Max the hedgehog visit with you and provide encouragement.

#### Adding new card types

The DeskHog now features a **dynamic configuration system** that allows cards to have sophisticated configuration forms with multiple fields, different input types, validation, and more. Here's how to create new card types:

**Step 1: Add to CardType enum**
Add your new type to `src/config/CardConfig.h`:

```cpp
enum class CardType {
    INSIGHT,
    FRIEND,
    WEATHER    // Add your new type here
};

// Don't forget to update the helper functions too:
inline String cardTypeToString(CardType type) {
    switch (type) {
        case CardType::INSIGHT: return "INSIGHT";
        case CardType::FRIEND: return "FRIEND";
        case CardType::WEATHER: return "WEATHER";  // Add this line
        default: return "UNKNOWN";
    }
}

inline CardType stringToCardType(const String& str) {
    if (str == "INSIGHT") return CardType::INSIGHT;
    if (str == "FRIEND") return CardType::FRIEND;
    if (str == "WEATHER") return CardType::WEATHER;  // Add this line
    return CardType::INSIGHT; // Default fallback
}
```

**Step 2: Create your card class**
Implement your card UI using LVGL:

```cpp
// src/ui/WeatherCard.h
class WeatherCard {
public:
    WeatherCard(lv_obj_t* parent, const std::map<String, String>& config);
    lv_obj_t* getCard() const { return _card; }

private:
    lv_obj_t* _card;
    String _location;
    bool _showForecast;
};

// src/ui/WeatherCard.cpp
WeatherCard::WeatherCard(lv_obj_t* parent, const std::map<String, String>& config) {
    // Extract configuration values
    auto locationIt = config.find("location");
    _location = (locationIt != config.end()) ? locationIt->second : "New York";
    
    auto forecastIt = config.find("show_forecast");
    _showForecast = (forecastIt != config.end()) ? (forecastIt->second == "true") : false;
    
    // Create LVGL UI using the configuration...
    _card = lv_obj_create(parent);
    // ... implement your UI here
}
```

**Step 3: Register the card type with dynamic configuration**
Add to `CardController::initializeCardTypes()` in `src/ui/CardController.cpp`:

```cpp
void CardController::initializeCardTypes() {
    // ... existing registrations ...
    
    // Register WEATHER card type with dynamic configuration
    std::vector<ConfigField> weatherFields = {
        ConfigField("location", "Location", "Enter the city name for weather", 
                   ConfigFieldType::TEXT, "New York", true),
        ConfigField("units", "Units", "Temperature units", 
                   std::vector<String>{"Celsius", "Fahrenheit"}, "Celsius", false),
        ConfigField("show_forecast", "Show Forecast", "Display 5-day forecast", 
                   ConfigFieldType::BOOLEAN, "false", false),
        ConfigField("update_interval", "Update Interval (minutes)", 
                   "How often to fetch weather data", 
                   ConfigFieldType::TEXT, "30", false)
    };
    
    CardDefinition weatherDef(CardType::WEATHER, "Weather card", true,
                             "Display current weather and forecast", weatherFields);
    
    // Set up the factory function
    weatherDef.factory = [this](const std::map<String, String>& configValues) -> lv_obj_t* {
        // Validate required fields
        auto locationIt = configValues.find("location");
        if (locationIt == configValues.end() || locationIt->second.isEmpty()) {
            Serial.println("Error: Location is required for weather card");
            return nullptr;
        }
        
        // Create the weather card with configuration
        WeatherCard* newCard = new WeatherCard(screen, configValues);
        
        if (newCard && newCard->getCard()) {
            // Add to any tracking collections if needed
            // weatherCards.push_back(newCard);
            
            Serial.printf("Created weather card for location: %s\n", 
                         locationIt->second.c_str());
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    
    // Legacy factory for backward compatibility (optional)
    weatherDef.legacyFactory = [this](const String& configValue) -> lv_obj_t* {
        std::map<String, String> config;
        config["location"] = configValue;  // Treat legacy config as location
        return weatherDef.factory(config);
    };
    
    registerCardType(weatherDef);
}
```

**Configuration Field Types:**

- **`ConfigFieldType::TEXT`** - Single-line text input
- **`ConfigFieldType::TEXTAREA`** - Multi-line text input  
- **`ConfigFieldType::SELECT`** - Dropdown with predefined options
- **`ConfigFieldType::BOOLEAN`** - Checkbox for true/false values

**ConfigField Parameters:**
```cpp
ConfigField(
    "key",              // Unique key for this field
    "Display Label",    // Label shown to user
    "Help description", // Detailed help text
    ConfigFieldType::TEXT,  // Input type
    "default_value",    // Default value (optional)
    true               // Whether field is required
);

// For SELECT type with options:
ConfigField("units", "Temperature Units", "Choose temperature scale",
           std::vector<String>{"Celsius", "Fahrenheit"}, "Celsius", false);
```

**CardDefinition Properties:**
- `type`: Your CardType enum value
- `name`: Display name in web UI
- `allowMultiple`: Whether users can add multiple instances of this card type
- `uiDescription`: Description shown in web UI
- `configFields`: Vector of ConfigField objects defining the configuration form
- `factory`: Lambda that creates cards using `std::map<String, String>` config
- `legacyFactory`: Optional legacy support for old single-string configs

**Step 4: Update cleanup code (if needed)**
If your card needs special cleanup, add it to `CardController::reconcileCards()`:

```cpp
// In reconcileCards(), add cleanup for your card type:
// Remove weather cards
for (auto* card : weatherCards) {
    if (card && card->getCard()) {
        cardStack->removeCard(card->getCard());
    }
    delete card;
}
weatherCards.clear();
```

#### Web UI integration

The web UI automatically generates sophisticated configuration forms based on your card definitions. Features include:

**Automatic form generation:**
- Text inputs, dropdowns, textareas, and checkboxes based on `ConfigFieldType`
- Field validation for required fields
- Default values and help text
- Options for SELECT fields

**Card management:**
- Add buttons for cards that can be added (respects `allowMultiple`)
- Status indicators showing how many instances are configured
- Drag-and-drop reordering for configured cards
- Dynamic configuration display in the cards list

**Example generated form for the weather card above:**
- "Location" text input (required, marked with *)
- "Units" dropdown with Celsius/Fahrenheit options
- "Show Forecast" checkbox
- "Update Interval" text input with default "30"
- Help text displayed under each field

**No web UI changes needed** - everything is driven by the card registration system. The web UI automatically:
1. Fetches card definitions from `/api/cards/definitions`
2. Renders appropriate input fields based on `configFields`
3. Validates required fields before submission
4. Sends dynamic configuration as JSON to `/api/cards/configured`

**Backward compatibility:** The system seamlessly supports both new dynamic configurations and legacy single-string configurations, so existing cards continue working without changes.

### Insight parser and PostHog client

`InsightParser` ingests PostHog API responses and makes them available to the UI. `PostHogClient` constructs requests and dispatches responses.

### LVGL

This project relies on the powerful [LVGL project](https://docs.lvgl.io/9.2/intro/index.html) at [v9.2.2](https://registry.platformio.org/libraries/lvgl/lvgl?version=9.2.2) for drawing, animation and other UI tasks.

### Config manager and captive portal

`ConfigManager` handles persistent storage and retrieval of credentials and insights. `CaptivePortal` provides the web server and interacts with `ConfigManager` to read and write to persistent storage.
