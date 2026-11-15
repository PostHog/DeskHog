#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <vector>

#include "ConfigManager.h"
#include "hardware/WifiInterface.h"
#include "ui/CardNavigationStack.h"
#include "ui/ProvisioningCard.h"
#include "hardware/DisplayInterface.h"
#include "EventQueue.h"
#include "config/CardConfig.h"
#include "UICallback.h"

class PluginCard;
class PluginManager;

/**
 * @class CardController
 * @brief Main UI controller managing card-based interface components
 * 
 * Owns the card stack: the WiFi/status card, which is always first, followed
 * by the plugin cards in the order the user gave them. Rebuilds the stack when
 * the saved list changes, and runs the queue through which other tasks ask
 * for work on the UI task.
 */
class CardController {
public:
    /**
     * @brief Constructor for CardController
     * 
     * @param screen LVGL screen object
     * @param screenWidth Width of the display in pixels
     * @param screenHeight Height of the display in pixels
     * @param configManager Reference to configuration manager
     * @param wifiInterface Reference to WiFi interface
     * @param eventQueue Reference to event queue for state changes
     */
    CardController(
        lv_obj_t* screen,
        uint16_t screenWidth,
        uint16_t screenHeight,
        ConfigManager& configManager,
        WiFiInterface& wifiInterface,
        EventQueue& eventQueue
    );
    
    /**
     * @brief Destructor - cleans up UI resources
     */
    ~CardController();
    
    /**
     * @brief Connect the Lua plugin system. Call before initialize().
     * @param manager Plugin manager, or nullptr to run without plugins
     */
    void setPluginManager(PluginManager* manager) { pluginManager = manager; }

    /**
     * @brief Initialize the controller with a display interface
     * @param display Pointer to display interface for thread-safe updates
     */
    void initialize(DisplayInterface* display);

    /**
     * @brief Set the display interface for thread-safe updates
     * @param display Pointer to display interface
     */
    void setDisplayInterface(DisplayInterface* display);
    
    
    /**
     * @brief Get the card navigation stack
     * @return Pointer to card navigation stack
     */
    CardNavigationStack* getCardStack() { return cardStack; }

    /**
     * @brief Get the provisioning card
     * @return Pointer to provisioning card
     */
    ProvisioningCard* getProvisioningCard() { return provisioningCard; }

    /**
     * @brief Get the display interface
     * @return Pointer to display interface
     */
    DisplayInterface* getDisplayInterface() { return displayInterface; }

    /**
     * @brief Process card configuration changes from the web UI
     * Called when CARD_CONFIG_CHANGED event is received
     */
    void handleCardConfigChanged();

    /**
     * @brief Initialize the UI update queue
     * 
     * Creates a FreeRTOS queue for handling UI updates across threads.
     * Must be called once during CardController initialization.
     */
    void initUIQueue();

    /**
     * @brief Process pending UI updates
     * 
     * Processes all queued UI updates in the LVGL task context.
     * Should be called regularly from the LVGL handler task.
     */
    void processUIQueue();
    
    /**
     * @brief Thread-safe method to dispatch UI updates to the LVGL task
     * 
     * @param update_func Lambda function containing UI operations
     * @param to_front If true, tries to add the callback to the front of the queue
     * 
     * Queues UI operations to be executed on the LVGL thread.
     * Handles queue overflow by discarding updates if queue is full.
     */
    void dispatchToLVGLTask(std::function<void()> update_func, bool to_front = false);

private:
    // Screen reference
    lv_obj_t* screen;              ///< Main LVGL screen object
    uint16_t screenWidth;          ///< Screen width in pixels
    uint16_t screenHeight;         ///< Screen height in pixels
    
    // System components
    ConfigManager& configManager;   ///< Configuration manager reference
    WiFiInterface& wifiInterface;  ///< WiFi interface reference
    EventQueue& eventQueue;        ///< Event queue reference
    PluginManager* pluginManager = nullptr; ///< Lua plugin system (optional)
    
    // UI Components
    CardNavigationStack* cardStack;     ///< Navigation stack for cards
    ProvisioningCard* provisioningCard; ///< Card for device provisioning
    
    /// The plugin cards in the stack. Each one is owned here and deleted when the stack is rebuilt.
    std::vector<PluginCard*> pluginCards;

    // Display interface for thread safety
    DisplayInterface* displayInterface;  ///< Thread-safe display interface
    
    // UI Threading
    static QueueHandle_t uiQueue;  ///< Queue for thread-safe UI updates
    
    std::vector<CardConfig> currentCardConfigs;      ///< Current card configuration from storage
    bool reconcileInProgress = false;                ///< Flag to prevent concurrent reconciliations
    

    /**
     * @brief Handle WiFi-related events
     * @param event Event containing WiFi state
     */
    void handleWiFiEvent(const Event& event);

    /**
     * @brief Rebuild the stack from the saved list of cards
     * Removes every plugin card and creates them again in the saved order
     * @param newConfigs New card configuration from storage
     */
    void reconcileCards(const std::vector<CardConfig>& newConfigs);
}; 