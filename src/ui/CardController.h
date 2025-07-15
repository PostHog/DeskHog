#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <unordered_map>

#include "ConfigManager.h"
#include "hardware/WifiInterface.h"
#include "posthog/PostHogClient.h"
#include "ui/CardNavigationStack.h"
#include "ui/ProvisioningCard.h"
#include "ui/InsightCard.h"
#include "ui/FriendCard.h"
#include "ui/examples/HelloWorldCard.h"
#include "ui/FlappyHogCard.h"
#include "hardware/DisplayInterface.h"
#include "EventQueue.h"
#include "config/CardConfig.h"
#include "UICallback.h"
#include "ui/QuestionCard.h"

/**
 * @class CardController
 * @brief Main UI controller managing card-based interface components
 * 
 * Coordinates the creation, management, and interaction between different
 * UI cards including insights, provisioning, and animations. Handles thread-safe
 * display updates and event-driven UI state changes.
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
     * @param posthogClient Reference to PostHog client
     * @param eventQueue Reference to event queue for state changes
     */
    CardController(
        lv_obj_t* screen,
        uint16_t screenWidth,
        uint16_t screenHeight,
        ConfigManager& configManager,
        WiFiInterface& wifiInterface,
        PostHogClient& posthogClient,
        EventQueue& eventQueue
    );
    
    /**
     * @brief Destructor - cleans up UI resources
     */
    ~CardController();
    
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
     * @brief Get the animation card
     * @return Pointer to animation card
     */
    FriendCard* getAnimationCard() { return animationCard; }

    /**
     * @brief Get all insight cards
     * @return Vector of insight card pointers
     */
    std::vector<InsightCard*> getInsightCards() {
        std::vector<InsightCard*> result;
        auto it = dynamicCards.find(CardType::INSIGHT);
        if (it != dynamicCards.end()) {
            for (const auto& instance : it->second) {
                // Cast the handler to InsightCard*
                result.push_back(static_cast<InsightCard*>(instance.handler));
            }
        }
        return result;
    }

    /**
     * @brief Get the display interface
     * @return Pointer to display interface
     */
    DisplayInterface* getDisplayInterface() { return displayInterface; }

    /**
     * @brief Get available card definitions for the web UI
     * @return Vector of CardDefinition objects representing available card types
     */
    std::vector<CardDefinition> getCardDefinitions() const;

    /**
     * @brief Register an available card type with its definition and factory function
     * @param definition The card definition including metadata and factory function
     */
    void registerCardType(const CardDefinition& definition);

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
    PostHogClient& posthogClient;  ///< PostHog client reference
    EventQueue& eventQueue;        ///< Event queue reference
    
    // UI Components
    CardNavigationStack* cardStack;     ///< Navigation stack for cards
    ProvisioningCard* provisioningCard; ///< Card for device provisioning
    
    // Unified card tracking system
    struct CardInstance {
        InputHandler* handler;  ///< The card as an InputHandler
        lv_obj_t* lvglCard;    ///< The LVGL card object
    };
    std::unordered_map<CardType, std::vector<CardInstance>> dynamicCards; ///< All dynamic cards by type
    
    // Legacy single instance tracking (for backwards compatibility during transition)
    FriendCard* animationCard;       ///< Card for animations
    
    // Display interface for thread safety
    DisplayInterface* displayInterface;  ///< Thread-safe display interface
    
    // UI Threading
    static QueueHandle_t uiQueue;  ///< Queue for thread-safe UI updates
    
    // Card registration and management
    std::vector<CardDefinition> registeredCardTypes; ///< Available card types with factory functions
    std::vector<CardConfig> currentCardConfigs;      ///< Current card configuration from storage
    bool reconcileInProgress = false;                ///< Flag to prevent concurrent reconciliations
    
    /**
     * @brief Create and initialize the animation card
     */
    void createAnimationCard();
    
    /**
     * @brief Create and initialize the hello world card
     */
    void createHelloWorldCard();
    

    /**
     * @brief Handle WiFi-related events
     * @param event Event containing WiFi state
     */
    void handleWiFiEvent(const Event& event);

    /**
     * @brief Handle card title update events
     * @param event Event containing insight ID and new title
     */
    void handleCardTitleUpdated(const Event& event);

    /**
     * @brief Initialize default card type registrations
     * Registers built-in card types (INSIGHT, FRIEND) with their factory functions
     */
    void initializeCardTypes();

    /**
     * @brief Reconcile current cards with new configuration
     * Diffs configuration, removes old cards, creates new ones, and reorders
     * @param newConfigs New card configuration from storage
     */
    void reconcileCards(const std::vector<CardConfig>& newConfigs);
}; 