#include "LifeCountdownCardWrapper.h"
#include "ui/CardController.h"
#include "ui/PaddleCard.h"
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
    PostHogClient& posthogClient,
    EventQueue& eventQueue
) : screen(screen),
    screenWidth(screenWidth),
    screenHeight(screenHeight),
    configManager(configManager),
    wifiInterface(wifiInterface),
    posthogClient(posthogClient),
    eventQueue(eventQueue),
    cardStack(nullptr),
    provisioningCard(nullptr),
    animationCard(nullptr),
    displayInterface(nullptr),
    dynamicCards()
{
}

CardController::~CardController() {
    // Clean up any allocated resources
    delete cardStack;
    cardStack = nullptr;
    
    delete provisioningCard;
    provisioningCard = nullptr;
    
    delete animationCard;
    animationCard = nullptr;
    
    // Use mutex if available before cleaning up dynamic cards
    if (displayInterface && displayInterface->getMutexPtr()) {
        xSemaphoreTake(*(displayInterface->getMutexPtr()), portMAX_DELAY);
    }
    
    // Clean up all dynamic cards using unified system
    for (auto& [cardType, cards] : dynamicCards) {
        for (auto& cardInstance : cards) {
            delete cardInstance.handler;
        }
    }
    dynamicCards.clear();
    
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
    
    // Initialize card type registrations
    initializeCardTypes();
    
    // Create card navigation stack
    cardStack = new CardNavigationStack(screen, screenWidth, screenHeight);
    
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
        } else if (event.type == EventType::CARD_TITLE_UPDATED) {
            handleCardTitleUpdated(event);
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
    
    // Set the mutex for the card stack if we have a display interface
    if (cardStack && displayInterface) {
        cardStack->setMutex(displayInterface->getMutexPtr());
    }
}

// Create an animation card with the walking sprites
void CardController::createAnimationCard() {
    if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
        return;
    }
    
    // Create new animation card
    animationCard = new FriendCard(
        screen
    );
    
    // Add to navigation stack
    cardStack->addCard(animationCard->getCard());
    
    // Register the animation card as an input handler
    cardStack->registerInputHandler(animationCard->getCard(), animationCard);
    
    displayInterface->giveMutex();
}

void CardController::createHelloWorldCard() {
    if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
        return;
    }
    
    // Create new hello world card
    HelloWorldCard* helloCard = new HelloWorldCard(screen);
    
    if (helloCard && helloCard->getCard()) {
        // Add to navigation stack
        cardStack->addCard(helloCard->getCard());
        
        // Register as an input handler
        cardStack->registerInputHandler(helloCard->getCard(), helloCard);
    }
    
    displayInterface->giveMutex();
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

std::vector<CardDefinition> CardController::getCardDefinitions() const {
    return registeredCardTypes;
}

void CardController::registerCardType(const CardDefinition& definition) {
    registeredCardTypes.push_back(definition);
}

void CardController::initializeCardTypes() {
      Serial.println("DEBUG: Starting card type initialization...");
    
    // Register INSIGHT card type
    CardDefinition insightDef;
    insightDef.type = CardType::INSIGHT;
    insightDef.name = "PostHog insight";
    insightDef.allowMultiple = true;
    insightDef.needsConfigInput = true;
    insightDef.configInputLabel = "Insight ID";
    insightDef.uiDescription = "Insight cards let you keep an eye on PostHog data";
    insightDef.factory = [this](const String& configValue) -> lv_obj_t* {
        // Create new insight card using the insight ID
        InsightCard* newCard = new InsightCard(
            screen,
            configManager,
            eventQueue,
            configValue,
            screenWidth,
            screenHeight
        );
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::INSIGHT].push_back(instance);
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            
            // Request data for this insight immediately
            posthogClient.requestInsightData(configValue);
            Serial.printf("Requested insight data for: %s\n", configValue.c_str());
            
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    registerCardType(insightDef);
    
    // Register FRIEND card type  
    CardDefinition friendDef;
    friendDef.type = CardType::FRIEND;
    friendDef.name = "Friend card";
    friendDef.allowMultiple = false;
    friendDef.needsConfigInput = false;
    friendDef.configInputLabel = "";
    friendDef.uiDescription = "Get reassurance from Max the hedgehog";
    friendDef.factory = [this](const String& configValue) -> lv_obj_t* {
        // Create new friend card (ignore configValue for now)
        FriendCard* newCard = new FriendCard(screen);
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::FRIEND].push_back(instance);
            
            // Keep legacy pointer for backwards compatibility
            animationCard = newCard;
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    registerCardType(friendDef);
    
    // Register HELLO_WORLD card type
    CardDefinition helloDef;
    helloDef.type = CardType::HELLO_WORLD;
    helloDef.name = "Hello, world!";
    helloDef.allowMultiple = true;
    helloDef.needsConfigInput = false;
    helloDef.configInputLabel = "";
    helloDef.uiDescription = "A simple greeting card";
    helloDef.factory = [this](const String& configValue) -> lv_obj_t* {
        HelloWorldCard* newCard = new HelloWorldCard(screen);
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::HELLO_WORLD].push_back(instance);
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    registerCardType(helloDef);
    
    // Register FLAPPY_HOG card type
    CardDefinition flappyDef;
    flappyDef.type = CardType::FLAPPY_HOG;
    flappyDef.name = "Flappy Hog";
    flappyDef.allowMultiple = false;  // Only one game instance at a time
    flappyDef.needsConfigInput = false;
    flappyDef.configInputLabel = "";
    flappyDef.uiDescription = "One button. Endless frustration. Infinite glory.";
    flappyDef.factory = [this](const String& configValue) -> lv_obj_t* {
        FlappyHogCard* newCard = new FlappyHogCard(screen);
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::FLAPPY_HOG].push_back(instance);
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    registerCardType(flappyDef);
    
    // Register QUESTION card type
    CardDefinition questionDef;
    questionDef.type = CardType::QUESTION;
    questionDef.name = "Question Card";
    questionDef.allowMultiple = false;  // Only one question card at a time
    questionDef.needsConfigInput = false;
    questionDef.configInputLabel = "";
    questionDef.uiDescription = "Break the ice with your coworkers.";
    questionDef.factory = [this](const String& configValue) -> lv_obj_t* {
        QuestionCard* newCard = new QuestionCard(screen);
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::QUESTION].push_back(instance);
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    registerCardType(questionDef);
    
    // Register PADDLE card type
    CardDefinition paddleDef;
    paddleDef.type = CardType::PADDLE;
    paddleDef.name = "Paddle";
    paddleDef.allowMultiple = false;  // Only one paddle game at a time
    paddleDef.needsConfigInput = false;
    paddleDef.configInputLabel = "";
    paddleDef.uiDescription = "Classic Paddle game - beat the AI!";
    paddleDef.factory = [this](const String& configValue) -> lv_obj_t* {
        PaddleCard* newCard = new PaddleCard(screen);
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::PADDLE].push_back(instance);
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };
    registerCardType(paddleDef);
    
        // Register LIFE_COUNTDOWN card type
    CardDefinition lifecountdownDef;
    lifecountdownDef.type = CardType::LIFE_COUNTDOWN;
    lifecountdownDef.name = "Life Countdown";
    lifecountdownDef.allowMultiple = false;
    lifecountdownDef.needsConfigInput = false;
    lifecountdownDef.configInputLabel = "";
    lifecountdownDef.uiDescription = "Motivational countdowns for your life and work";
    lifecountdownDef.factory = [this](const String& configValue) -> lv_obj_t* {
        LifeCountdownCardWrapper* newCard = new LifeCountdownCardWrapper(screen);
        
        if (newCard && newCard->getCard()) {
            // Add to unified tracking system
            CardInstance instance{newCard, newCard->getCard()};
            dynamicCards[CardType::LIFE_COUNTDOWN].push_back(instance);
            
            // Register as input handler
            cardStack->registerInputHandler(newCard->getCard(), newCard);
            return newCard->getCard();
        }
        
        delete newCard;
        return nullptr;
    };

    registerCardType(lifecountdownDef);
Serial.println("DEBUG: Life Countdown card type registered!");
Serial.printf("Total registered card types: %d\n", registeredCardTypes.size());
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
    
    
    // Track the number of cards before reconciliation
    size_t oldCardCount = 0;
    for (const auto& [cardType, cards] : dynamicCards) {
        oldCardCount += cards.size();
    }
    
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
        
        // Remove all existing dynamic cards using unified system
        for (auto& [cardType, cards] : dynamicCards) {
            for (auto& cardInstance : cards) {
                if (cardInstance.lvglCard) {
                    // Notify the card that its LVGL object will be managed externally
                    cardInstance.handler->prepareForRemoval();
                    // Remove from navigation stack (this deletes the LVGL object)
                    cardStack->removeCard(cardInstance.lvglCard);
                }
                delete cardInstance.handler;
            }
        }
        dynamicCards.clear();
        
        // Clear legacy pointer
        animationCard = nullptr;
        
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
            // Find the registered card type
            auto it = std::find_if(registeredCardTypes.begin(), registeredCardTypes.end(),
                                  [&config](const CardDefinition& def) {
                                      return def.type == config.type;
                                  });
            
            if (it != registeredCardTypes.end() && it->factory) {
                // Create the card using the factory function
                lv_obj_t* cardObj = it->factory(config.config);
                if (cardObj) {
                    cardStack->addCard(cardObj);
                    
                    // Track position of new card if this is likely the new one
                    if (hasNewCard && i == sortedConfigs.size() - 1) {
                        newCardPosition = cardsCreated + 1; // +1 for provisioning card
                    }
                    
                    cardsCreated++;
                } else {
                    Serial.printf("Failed to create card of type %s\n", 
                                 cardTypeToString(config.type).c_str());
                }
            } else {
                Serial.printf("No factory found for card type %s\n", 
                             cardTypeToString(config.type).c_str());
            }
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

void CardController::handleCardTitleUpdated(const Event& event) {
    // Find and update the card configuration with the new title
    for (auto& cardConfig : currentCardConfigs) {
        if (cardConfig.type == CardType::INSIGHT && cardConfig.config == event.insightId) {
            // Update the name with the new title
            if (cardConfig.name != event.title) {
                cardConfig.name = event.title;
                
                // Save the updated configuration to persistent storage
                configManager.saveCardConfigs(currentCardConfigs);
                
                Serial.printf("Updated card title for insight %s to: %s\n", 
                             event.insightId.c_str(), event.title.c_str());
            }
            break;
        }
    }
} 