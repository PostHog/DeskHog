#pragma once

#include <lvgl.h>
#include <memory>
#include <functional>
#include "ConfigManager.h"
#include "EventQueue.h"
#include "homeassistant/HomeAssistantParser.h"
#include "homeassistant/HomeAssistantClient.h"
#include "ui/InputHandler.h"
#include "UICallback.h"

/**
 * @class HomeAssistantCard
 * @brief UI component for displaying Home Assistant entity data
 * 
 * Provides a card-based UI component that displays Home Assistant entity states:
 * - Numeric sensor values (temperature, humidity, power, etc.)
 * - Binary sensor states (motion, door, window states)
 * - Switch and light states
 * - Cover states (blinds, curtains, garage doors with position display)
 * 
 * Features:
 * - Thread-safe UI updates via queue system
 * - Automatic entity type detection and UI adaptation
 * - Memory-safe LVGL object management
 * - Unit display and formatting
 * - State availability indication
 * - Button interaction for cover and light control
 */
class HomeAssistantCard : public InputHandler {
public:
    /**
     * @brief Constructor
     * 
     * @param parent LVGL parent object to attach this card to
     * @param config Configuration manager for persistent storage
     * @param eventQueue Event queue for receiving data updates
     * @param homeAssistantClient Home Assistant client for service calls
     * @param entityId Home Assistant entity ID (e.g., "sensor.temperature")
     * @param width Card width in pixels
     * @param height Card height in pixels
     * 
     * Creates a card with a vertical flex layout containing:
     * - Title label showing friendly name with ellipsis for overflow
     * - Value label for numeric display
     * - Unit label for unit of measurement
     * Subscribes to HA_ENTITY_STATE_RECEIVED events for the specified entityId.
     */
    HomeAssistantCard(lv_obj_t* parent, ConfigManager& config, EventQueue& eventQueue,
                     HomeAssistantClient& homeAssistantClient, const String& entityId, 
                     uint16_t width, uint16_t height);
    
    /**
     * @brief Destructor - cleans up LVGL objects safely
     */
    ~HomeAssistantCard();
    
    /**
     * @brief Get the entity ID for this card
     * @return Entity ID string
     */
    const String& getEntityId() const { return _entity_id; }
    
    /**
     * @brief Get the current friendly name/title
     * @return Current title string
     */
    const String& getCurrentTitle() const { return _current_title; }
    
    /**
     * @brief Get the main card object
     * @return Pointer to LVGL card object
     */
    lv_obj_t* getCardObject() const { return _card; }

    /**
     * @brief Handle button press events
     * @param button_index The button that was pressed
     * @return true if the event was handled, false otherwise
     * 
     * For cover entities: toggles open/close
     * For light entities: toggles on/off
     * For switch entities: toggles on/off
     */
    bool handleButtonPress(uint8_t button_index) override;

private:
    // Configuration and events
    ConfigManager& _config;
    EventQueue& _event_queue;
    HomeAssistantClient& _home_assistant_client;
    
    // Entity identification
    String _entity_id;
    String _current_title;
    
    // LVGL UI objects
    lv_obj_t* _card;                ///< Main card container
    lv_obj_t* _title_label;         ///< Entity friendly name
    lv_obj_t* _value_label;         ///< Main value display
    lv_obj_t* _unit_label;          ///< Unit of measurement
    lv_obj_t* _content_container;   ///< Container for value and unit
    
    // State tracking
    HomeAssistantParser::EntityType _current_type;
    
    /**
     * @brief Handle incoming Home Assistant entity events
     * @param event Event containing entity state data
     */
    void onEvent(const Event& event);
    
    /**
     * @brief Process parsed entity data and update UI
     * @param parser Shared pointer to parsed entity data
     */
    void handleParsedData(std::shared_ptr<HomeAssistantParser> parser);
    
    /**
     * @brief Update numeric sensor display
     * @param parser Parser containing entity data
     * @param friendly_name Entity friendly name
     */
    void updateNumericDisplay(const HomeAssistantParser& parser, const String& friendly_name);
    
    /**
     * @brief Update binary sensor display
     * @param parser Parser containing entity data
     * @param friendly_name Entity friendly name
     */
    void updateBinaryDisplay(const HomeAssistantParser& parser, const String& friendly_name);
    
    /**
     * @brief Update switch/light display
     * @param parser Parser containing entity data
     * @param friendly_name Entity friendly name
     */
    void updateSwitchDisplay(const HomeAssistantParser& parser, const String& friendly_name);
    
    /**
     * @brief Update cover display (blinds, curtains, garage doors)
     * @param parser Parser containing entity data
     * @param friendly_name Entity friendly name
     */
    void updateCoverDisplay(const HomeAssistantParser& parser, const String& friendly_name);
    
    /**
     * @brief Format numeric value with appropriate precision
     * @param value Numeric value to format
     * @param buffer Buffer to store formatted string
     * @param bufferSize Size of the buffer
     */
    void formatNumericValue(double value, char* buffer, size_t bufferSize);
    
    /**
     * @brief Clear content container
     */
    void clearContentContainer();
    
    /**
     * @brief Check if LVGL object is valid
     * @param obj Object to check
     * @return true if valid, false otherwise
     */
    bool isValidObject(lv_obj_t* obj) const;
};
