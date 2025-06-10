# Enhancing card management

This refactor will enhance DeskHog to give the user better control of the cards displayed on their screen. This will also allow developers to build and register new card types, which users can then switch on and use.

## Data Structures

To support the new functionality, we will introduce the following data structures, likely in a new header file like `src/config/CardConfig.h`.

### `CardType` Enum

An enum to uniquely identify each type of card available in the system.

```cpp
enum class CardType {
    INSIGHT,
    FRIEND
    // New card types can be added here
};
```

### `CardConfig` Struct

This struct represents an *instance* of a configured card. A list of these will be stored in persistent memory.

```cpp
struct CardConfig {
    CardType id;
    String config; // e.g., insight ID, animation speed
    int order;
};
```

### `CardDefinition` Struct

This struct represents an *available type* of card that a user can choose to add. These will be defined in `CardController`.

```cpp
#include <functional>

struct CardDefinition {
    CardType id;
    String name;             // "PostHog Insight"
    bool allowMultiple;      // Can the user add more than one of this card type?
    bool needsConfigInput;   // Does this card require a config value?
    String configInputLabel; // "Insight ID", "Animation Speed"
    String uiDescription;
    
    // Factory function to create an instance of the card's UI
    std::function<lv_obj_t*(const String& configValue)> factory;
};
```

## Changes

### Persistent storage:

This will add a new preferences instance: `_cardPrefs`, to the `ConfigManager` class.

All enabled cards will be stored as a JSON array of `CardConfig` objects under a single key in `_cardPrefs`. This provides flexibility for adding/removing/reordering cards.

The `ConfigManager` will expose new methods for card management:
- `std::vector<CardConfig> getCardConfigs()`: Reads and deserializes the JSON array from preferences.
- `bool saveCardConfigs(const std::vector<CardConfig>& configs)`: Serializes the vector to JSON and saves it to preferences.

### Web UI:

The only card management currently supported by the web UI (in the `html` folder) is adding and deleting insights. This section should go away. 

A new section should be added to the web UI: "Card Management."

This section enables adding new cards from a list of available types, re-ordering existing cards, editing their configuration values, and removing them.

The user can click and drag to set the order of cards in the list. They can delete a card. A user can add new cards from a list of available types.

Insights will be adapted to this format with these specifications: allowing multiple instances, needing config input, and `configInputLabel` being "Insight ID."

## API Endpoints

The `CaptivePortal` will be updated with a new set of API endpoints to support the web UI. Old insight-related endpoints will be removed.

- `GET /api/cards/definitions`
  - **Description**: Returns a list of all available card types that the user can add.
  - **Response Body**: A JSON array of `CardDefinition`-like objects.
    ```json
    [
      {
        "id": "INSIGHT",
        "name": "PostHog Insight",
        "allowMultiple": true,
        "needsConfigInput": true,
        "configInputLabel": "Insight ID"
        "description": "Insight cards let you keep an eye on PostHog data"
      }
    ]
    ```

- `GET /api/cards/configured`
  - **Description**: Returns the user's currently configured and ordered list of cards.
  - **Response Body**: A JSON array of `CardConfig` objects.

- `POST /api/cards/configured`
  - **Description**: Saves the entire list of configured cards. The web UI will use this to add, remove, and re-order cards by sending the complete new state.
  - **Request Body**: A JSON array of `CardConfig` objects representing the desired new state.
  - **Response**: `{ "success": true }`

## Card Controller as the source of card definition

`CardController` will be the place that:
- **Registers available card types**: It will hold a list or map of `CardDefinition` objects. An initialization method will register the built-in cards (Insight, Animation, etc.) along with their factory functions.
- **Responds to configuration changes**:
  - `ConfigManager` will publish a `CARD_CONFIG_CHANGED` event when the card configuration is updated via the API.
  - `CardController` will subscribe to this event.
  - Upon receiving the event, it will fetch the new configuration from `ConfigManager` and perform a reconciliation:
    1. It will diff the new configuration with the currently displayed cards.
    2. It will remove any card instances that are no longer in the config.
    3. It will create new card instances for any new entries in the config using the registered factory functions.
    4. It will re-order the cards in the `CardNavigationStack` to match the new `displayOrder`. This will likely involve removing all cards from the stack and re-adding them in the correct order.
- **Provides card definitions to the API**: It will expose a method for `CaptivePortal` to retrieve the list of `CardDefinition`s for the `GET /api/cards/definitions` endpoint.

## Other notes

- `ProvisioningCard` is a standard card that cannot be removed and is always at the top of the stack.
- This refactor should avoid making any changes to the underlying architecture of the project's task/thread management, unless absolutely necessary and only if completely confident that new design will be effective