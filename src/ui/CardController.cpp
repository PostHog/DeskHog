#include "ui/CardController.h"
#include "plugins/PluginCard.h"
#include "plugins/PluginManager.h"
#include <algorithm>

QueueHandle_t CardController::uiQueue = nullptr;

// Define the global UI dispatch function
std::function<void(std::function<void()>, bool)> globalUIDispatch;

CardController::CardController(
    lv_obj_t* screen,
    uint16_t screenWidth,
    uint16_t screenHeight,
    ConfigManager& configManager,
    WiFiInterface& wifiInterface,
    EventQueue& eventQueue
) : screen(screen),
    screenWidth(screenWidth),
    screenHeight(screenHeight),
    configManager(configManager),
    wifiInterface(wifiInterface),
    eventQueue(eventQueue),
    cardStack(nullptr),
    provisioningCard(nullptr),
    displayInterface(nullptr)
{
}

CardController::~CardController() {
    // Clean up any allocated resources
    delete cardStack;
    cardStack = nullptr;
    
    delete provisioningCard;
    provisioningCard = nullptr;
    
    // Use mutex if available before cleaning up dynamic cards
    if (displayInterface && displayInterface->getMutexPtr()) {
        xSemaphoreTake(*(displayInterface->getMutexPtr()), portMAX_DELAY);
    }
    
    for (PluginCard* card : pluginCards) {
        delete card;
    }
    pluginCards.clear();
    
    // Release mutex if we took it
    if (displayInterface && displayInterface->getMutexPtr()) {
        xSemaphoreGive(*(displayInterface->getMutexPtr()));
    }
}

void CardController::initialize(DisplayInterface* display) {
    // Set the display interface first
    setDisplayInterface(display);
    
    // Initialize UI queue for thread-safe operations
    initUIQueue();
    
    // Create card navigation stack
    cardStack = new CardNavigationStack(screen, screenWidth, screenHeight);

    // Let the plugin system bring a card to the front, e.g. the plugin that was just pushed
    if (pluginManager) {
        pluginManager->setShowCard([this](lv_obj_t* card) {
            if (!cardStack || !card) return;
            const int32_t index = lv_obj_get_index(card);
            if (index >= 0) cardStack->goToCard(static_cast<uint8_t>(index));
        });
    }
    
    // Create provision UI (always present, not configurable)
    provisioningCard = new ProvisioningCard(
        screen, 
        wifiInterface, 
        screenWidth, 
        screenHeight
    );
    
    // Add provisioning card to navigation stack
    cardStack->addCard(provisioningCard->getCard());
    
    // Load current card configuration and create cards
    currentCardConfigs = configManager.getCardConfigs();
    
    // Don't create any default cards when no configuration exists
    // Only the provisioning card will be shown
    
    // If we have card configurations now, reconcile them
    if (!currentCardConfigs.empty()) {
        reconcileCards(currentCardConfigs);
    }
    
    // Connect WiFi manager to UI
    wifiInterface.setUI(provisioningCard);
    
    
    // Subscribe to card configuration changes
    eventQueue.subscribe([this](const Event& event) {
        if (event.type == EventType::CARD_CONFIG_CHANGED) {
            handleCardConfigChanged();
        }
    });
    
    // Subscribe to WiFi events
    eventQueue.subscribe([this](const Event& event) {
        if (event.type == EventType::WIFI_CONNECTING || 
            event.type == EventType::WIFI_CONNECTED ||
            event.type == EventType::WIFI_CONNECTION_FAILED ||
            event.type == EventType::WIFI_AP_STARTED) {
            handleWiFiEvent(event);
        }
    });
}

void CardController::setDisplayInterface(DisplayInterface* display) {
    displayInterface = display;
    if (pluginManager) {
        pluginManager->setDisplayInterface(display);
    }
    
    // Set the mutex for the card stack if we have a display interface
    if (cardStack && displayInterface) {
        cardStack->setMutex(displayInterface->getMutexPtr());
    }
}

// Handle WiFi events
void CardController::handleWiFiEvent(const Event& event) {
    if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
        return;
    }
    
    switch (event.type) {
        case EventType::WIFI_CONNECTING:
            provisioningCard->updateConnectionStatus("Connecting to WiFi...");
            break;
            
        case EventType::WIFI_CONNECTED:
            provisioningCard->updateConnectionStatus("Connected");
            provisioningCard->showWiFiStatus();
            break;
            
        case EventType::WIFI_CONNECTION_FAILED:
            provisioningCard->updateConnectionStatus("Connection failed");
            break;
            
        case EventType::WIFI_AP_STARTED:
            provisioningCard->showQRCode();
            break;
            
        default:
            break;
    }
    
    displayInterface->giveMutex();
}

void CardController::handleCardConfigChanged() {
    // Load new configuration from storage
    std::vector<CardConfig> newConfigs = configManager.getCardConfigs();
    currentCardConfigs = newConfigs;
    
    // Perform reconciliation
    reconcileCards(newConfigs);
    
}

void CardController::reconcileCards(const std::vector<CardConfig>& newConfigs) {
    if (reconcileInProgress) {
        return;
    }
    
    
    const size_t oldCardCount = pluginCards.size();
    
    reconcileInProgress = true;
    
    // Dispatch the entire reconciliation to the LVGL task to ensure thread safety
    dispatchToLVGLTask([this, newConfigs, oldCardCount]() {
        if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
            reconcileInProgress = false;  // Clear flag on failure
            return;
        }
        
        // Save current card index to restore after reconciliation
        uint8_t savedCardIndex = cardStack ? cardStack->getCurrentIndex() : 0;
        
        // Simple approach: Clear everything and rebuild from scratch
        // This avoids complex diffing logic that can cause sync issues
        
        // Remove every plugin card. A running plugin keeps its state for a
        // moment without cards (see PluginConnection), so this does not restart it.
        for (PluginCard* card : pluginCards) {
            // The stack deletes the LVGL object and forgets the card's input
            // handler; the card is told not to delete the object again. Ask
            // for the object first: afterwards the card no longer has it.
            lv_obj_t* object = card->getCard();
            card->prepareForRemoval();
            if (object) cardStack->removeCard(object);
            delete card;
        }
        pluginCards.clear();
        
        // Force LVGL to process all pending operations
        lv_refr_now(NULL);
        
        // Now recreate cards based on new configuration
        std::vector<CardConfig> sortedConfigs = newConfigs;
        std::sort(sortedConfigs.begin(), sortedConfigs.end(), 
                  [](const CardConfig& a, const CardConfig& b) {
                      return a.order < b.order;
                  });
        
        // Track how many cards we've created
        size_t cardsCreated = 0;
        
        // Check if we have a new card (more configs than before)
        bool hasNewCard = (sortedConfigs.size() > oldCardCount);
        size_t newCardPosition = 0;
        
        for (size_t i = 0; i < sortedConfigs.size(); i++) {
            const CardConfig& config = sortedConfigs[i];
            // The stack leaves 7px on the right for its position indicator.
            PluginCard* card = (pluginManager && config.isPlugin())
                ? pluginManager->createCard(screen, config.config, screenWidth - 7, screenHeight)
                : nullptr;
            if (!card) {
                Serial.printf("Could not create the %s card \"%s\"\n", config.type.c_str(), config.config.c_str());
                continue;
            }
            pluginCards.push_back(card);
            cardStack->registerInputHandler(card->getCard(), card);
            cardStack->addCard(card->getCard());

            // Track position of new card if this is likely the new one
            if (hasNewCard && i == sortedConfigs.size() - 1) {
                newCardPosition = cardsCreated + 1; // +1 for provisioning card
            }
            cardsCreated++;
        }
        
        // Force another LVGL refresh to ensure everything is properly laid out
        lv_refr_now(NULL);
        
        // Force the card stack to update its pip indicators
        // This ensures the indicators are correct after bulk card operations
        cardStack->forceUpdateIndicators();
        
        // Navigate to appropriate card
        if (hasNewCard && newCardPosition > 0) {
            // Navigate to the newly added card
            cardStack->goToCard(newCardPosition);
        } else if (savedCardIndex > 0 && cardsCreated > 0) {
            // Restore previous position if no new card was added
            // Adjust for the provisioning card (always at index 0)
            uint8_t maxIndex = cardsCreated; // provisioning + created cards - 1
            uint8_t targetIndex = (savedCardIndex <= maxIndex) ? savedCardIndex : maxIndex;
            cardStack->goToCard(targetIndex);
        }
        
        // Clear the in-progress flag
        reconcileInProgress = false;
        
        displayInterface->giveMutex();
    }, true); // Use to_front=true for immediate processing
}

void CardController::initUIQueue() {
    if (uiQueue == nullptr) {
        uiQueue = xQueueCreate(20, sizeof(UICallback*));
        if (uiQueue == nullptr) {
            Serial.println("[UI-CRITICAL] Failed to create UI task queue!");
        } else {
            // Set the global dispatch function to point to our method
            globalUIDispatch = [this](std::function<void()> func, bool to_front) {
                this->dispatchToLVGLTask(std::move(func), to_front);
            };
        }
    }
}

void CardController::processUIQueue() {
    if (uiQueue == nullptr) return;

    UICallback* callback_ptr = nullptr;
    while (xQueueReceive(uiQueue, &callback_ptr, 0) == pdTRUE) {
        if (callback_ptr) {
            callback_ptr->execute();
            delete callback_ptr;
        }
    }
    
    // Update active card (for games and other interactive cards)
    if (cardStack) {
        cardStack->updateActiveCard();
    }

    // Plugins: timers, network answers and drawing for every plugin card
    if (pluginManager) {
        pluginManager->tick();
    }
}

void CardController::dispatchToLVGLTask(std::function<void()> update_func, bool to_front) {
    if (uiQueue == nullptr) {
        Serial.println("[UI-ERROR] UI Queue not initialized, cannot dispatch UI update.");
        return;
    }

    UICallback* callback = new UICallback(std::move(update_func));
    if (!callback) {
        Serial.println("[UI-CRITICAL] Failed to allocate UICallback for dispatch!");
        return;
    }

    BaseType_t queue_send_result;
    if (to_front) {
        queue_send_result = xQueueSendToFront(uiQueue, &callback, (TickType_t)0); 
    } else {
        queue_send_result = xQueueSend(uiQueue, &callback, (TickType_t)0);
    }

    if (queue_send_result != pdTRUE) {
        Serial.printf("[UI-WARN] UI queue full/error (send_to_front: %d), update discarded. Core: %d\n", 
                      to_front, xPortGetCoreID());
        delete callback;
    }
}
