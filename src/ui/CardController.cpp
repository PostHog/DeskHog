#include "ui/CardController.h"

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
    displayInterface(nullptr)
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
    
    // Use mutex if available before cleaning up insight cards
    if (displayInterface && displayInterface->getMutexPtr()) {
        xSemaphoreTake(*(displayInterface->getMutexPtr()), portMAX_DELAY);
    }
    
    // Clean up insight cards
    for (auto* card : insightCards) {
        delete card;
    }
    insightCards.clear();
    
    // Release mutex if we took it
    if (displayInterface && displayInterface->getMutexPtr()) {
        xSemaphoreGive(*(displayInterface->getMutexPtr()));
    }
}

void CardController::initialize(DisplayInterface* display) {
    // Set the display interface first
    setDisplayInterface(display);
    
    // Create card navigation stack
    cardStack = new CardNavigationStack(screen, screenWidth, screenHeight);
    
    // Create provision UI
    provisioningCard = new ProvisioningCard(
        screen, 
        wifiInterface, 
        screenWidth, 
        screenHeight
    );
    
    // Add provisioning card to navigation stack
    cardStack->addCard(provisioningCard->getCard());
    
    // Create animation card
    createAnimationCard();
    
    // Get count of insights to determine card count
    std::vector<String> insightIds = configManager.getAllInsightIds();
    
    // Create and add insight cards
    for (const String& id : insightIds) {
        createInsightCard(id);
    }
    
    // Connect WiFi manager to UI
    wifiInterface.setUI(provisioningCard);
    
    // Subscribe to insight events
    eventQueue.subscribe([this](const Event& event) {
        if (event.type == EventType::INSIGHT_ADDED || 
            event.type == EventType::INSIGHT_DELETED) {
            handleInsightEvent(event);
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
    animationCard = new AnimationCard(
        screen
    );
    
    // Add to navigation stack
    cardStack->addCard(animationCard->getCard());
    
    // Register the animation card as an input handler
    cardStack->registerInputHandler(animationCard->getCard(), animationCard);
    
    displayInterface->giveMutex();
}

// Create an insight card and add it to the UI
void CardController::createInsightCard(const String& insightId) {
    // Log current task and core
    Serial.printf("[CardCtrl-DEBUG] createInsightCard called from Core: %d, Task: %s\n", 
                  xPortGetCoreID(), 
                  pcTaskGetTaskName(NULL));

    // Dispatch the actual card creation and LVGL work to the LVGL task
    InsightCard::dispatchToLVGLTask([this, insightId]() {
        Serial.printf("[CardCtrl-DEBUG] LVGL Task creating card for insight: %s from Core: %d, Task: %s\n", 
                      insightId.c_str(), xPortGetCoreID(), pcTaskGetTaskName(NULL));

        if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
            Serial.println("[CardCtrl-ERROR] Failed to take mutex in LVGL task for card creation.");
            return;
        }

        // Create new insight card using full screen dimensions
        InsightCard* newCard = new InsightCard(
            screen,         // LVGL parent object
            configManager,  // Dependencies
            eventQueue,
            insightId,
            screenWidth,    // Dimensions
            screenHeight
        );

        if (!newCard || !newCard->getCard()) {
            Serial.printf("[CardCtrl-ERROR] Failed to create InsightCard or its LVGL object for ID: %s\n", insightId.c_str());
            displayInterface->giveMutex();
            delete newCard; // Clean up if partially created
            return;
        }

        // Add to navigation stack
        cardStack->addCard(newCard->getCard());

        // Add to our list of cards (ensure this is thread-safe if accessed elsewhere)
        // The mutex taken above should protect this operation too.
        insightCards.push_back(newCard);
        
        Serial.printf("[CardCtrl-DEBUG] InsightCard for ID: %s created and added to stack.\n", insightId.c_str());

        displayInterface->giveMutex();

        // Request immediate data for this insight now that it's set up
        posthogClient.requestInsightData(insightId);
    });
}

// Handle insight events
void CardController::handleInsightEvent(const Event& event) {
    if (event.type == EventType::INSIGHT_ADDED) {
        createInsightCard(event.insightId);
    } 
    else if (event.type == EventType::INSIGHT_DELETED) {
        if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
            return;
        }
        
        // Find and remove the card
        for (auto it = insightCards.begin(); it != insightCards.end(); ++it) {
            InsightCard* card = *it;
            if (card->getInsightId() == event.insightId) {
                // Remove from card stack
                cardStack->removeCard(card->getCard());
                
                // Remove from vector and delete
                insightCards.erase(it);
                delete card;
                break;
            }
        }
        
        displayInterface->giveMutex();
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