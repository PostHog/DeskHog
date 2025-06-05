#include "ui/CardController.h"

CardController::CardController(
    lv_obj_t* screen_obj,
    uint16_t scrWidth,
    uint16_t scrHeight,
    ConfigManager& cfgManager,
    WiFiInterface& wifi,
    PostHogClient& phClient,
    EventQueue& events
) : screen(screen_obj),
    screenWidth(scrWidth),
    screenHeight(scrHeight),
    configManager(cfgManager),
    wifiInterface(wifi),
    posthogClient(phClient),
    eventQueue(events),
    cardStack(nullptr),
    provisioningCard(nullptr),
    friendCard(nullptr),
    insightCards(),
    flappyBirdGame(nullptr),
    flappyBirdCardContainer(nullptr),
    displayInterface(nullptr)
{
}

CardController::~CardController() {
    delete cardStack;
    delete provisioningCard;
    delete friendCard;

    if (flappyBirdGame) {
        flappyBirdGame->cleanup();
        delete flappyBirdGame;
    }

    if (displayInterface && displayInterface->getMutexPtr()) {
        if (xSemaphoreTake(*(displayInterface->getMutexPtr()), pdMS_TO_TICKS(1000)) == pdTRUE) {
            for (auto* card : insightCards) {
                delete card;
            }
            insightCards.clear();
            xSemaphoreGive(*(displayInterface->getMutexPtr()));
        } else {
            Serial.println("[CardCtrl-WARN] Failed to take mutex in destructor for insightCards cleanup.");
            for (auto* card : insightCards) { delete card; }
            insightCards.clear();
        }
    } else {
        for (auto* card : insightCards) {
            delete card;
        }
        insightCards.clear();
    }
}

void CardController::initialize(DisplayInterface* disp) {
    setDisplayInterface(disp);
    
    cardStack = new CardNavigationStack(screen, screenWidth, screenHeight);
    if (displayInterface) { 
        cardStack->setMutex(displayInterface->getMutexPtr());
    }
    
    provisioningCard = new ProvisioningCard(
        screen, 
        wifiInterface, 
        screenWidth, 
        screenHeight
    );
    cardStack->addCard(provisioningCard->getCard());
    
    createFriendCard();

    if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
         Serial.println("[CardCtrl-ERROR] Failed to take mutex for Flappy Bird card creation.");
    } else {
        flappyBirdGame = new FlappyBirdGame();
        flappyBirdGame->setup(screen);
        flappyBirdCardContainer = flappyBirdGame->get_main_container();
        if (flappyBirdCardContainer) {
            cardStack->addCard(flappyBirdCardContainer);
            Serial.println("[CardCtrl-INFO] Flappy Bird game card added to stack.");
        } else {
            Serial.println("[CardCtrl-ERROR] Failed to get Flappy Bird game container.");
            delete flappyBirdGame;
            flappyBirdGame = nullptr;
        }
        displayInterface->giveMutex();
    }
    
    std::vector<String> insightIds = configManager.getAllInsightIds();
    for (const String& id : insightIds) {
        createInsightCard(id);
    }
    
    wifiInterface.setUI(provisioningCard);
    
    eventQueue.subscribe([this](const Event& event) {
        if (event.type == EventType::INSIGHT_ADDED || 
            event.type == EventType::INSIGHT_DELETED) {
            handleInsightEvent(event);
        }
    });
    
    eventQueue.subscribe([this](const Event& event) {
        if (event.type == EventType::WIFI_CONNECTING || 
            event.type == EventType::WIFI_CONNECTED ||
            event.type == EventType::WIFI_CONNECTION_FAILED ||
            event.type == EventType::WIFI_AP_STARTED) {
            handleWiFiEvent(event);
        }
    });
}

void CardController::setDisplayInterface(DisplayInterface* disp) {
    displayInterface = disp;
    if (cardStack && displayInterface) {
        cardStack->setMutex(displayInterface->getMutexPtr());
    }
}

void CardController::createFriendCard() {
    if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
        Serial.println("[CardCtrl-ERROR] Failed to take mutex for FriendCard creation.");
        return;
    }
    friendCard = new AnimationCard(screen);
    if (friendCard && friendCard->getCard()) {
        cardStack->addCard(friendCard->getCard());
        InputHandler* handler = dynamic_cast<InputHandler*>(friendCard);
        if (handler) {
             cardStack->registerInputHandler(friendCard->getCard(), handler);
        }
         Serial.println("[CardCtrl-INFO] FriendCard added to stack.");
    } else {
        Serial.println("[CardCtrl-ERROR] Failed to create FriendCard or its LVGL object.");
        delete friendCard;
        friendCard = nullptr;
    }
    displayInterface->giveMutex();
}

void CardController::createInsightCard(const String& insightId) {
    Serial.printf("[CardCtrl-DEBUG] createInsightCard called from Core: %d, Task: %s\n", 
                  xPortGetCoreID(), 
                  pcTaskGetTaskName(NULL));
    InsightCard::dispatchToLVGLTask([this, insightId]() {
        Serial.printf("[CardCtrl-DEBUG] LVGL Task creating card for insight: %s from Core: %d, Task: %s\n", 
                      insightId.c_str(), xPortGetCoreID(), pcTaskGetTaskName(NULL));
        if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
            Serial.println("[CardCtrl-ERROR] Failed to take mutex in LVGL task for card creation.");
            return;
        }
        InsightCard* newCard = new InsightCard(
            screen, 
            configManager, 
            eventQueue,
            insightId,
            screenWidth,
            screenHeight
        );
        if (!newCard || !newCard->getCard()) {
            Serial.printf("[CardCtrl-ERROR] Failed to create InsightCard or its LVGL object for ID: %s\n", insightId.c_str());
            displayInterface->giveMutex();
            delete newCard;
            return;
        }
        cardStack->addCard(newCard->getCard());
        insightCards.push_back(newCard);
        Serial.printf("[CardCtrl-DEBUG] InsightCard for ID: %s created and added to stack.\n", insightId.c_str());
        displayInterface->giveMutex();
        posthogClient.requestInsightData(insightId);
    });
}

void CardController::handleInsightEvent(const Event& event) {
    if (event.type == EventType::INSIGHT_ADDED) {
        createInsightCard(event.insightId);
    } 
    else if (event.type == EventType::INSIGHT_DELETED) {
        if (!displayInterface || !displayInterface->takeMutex(portMAX_DELAY)) {
            return;
        }
        for (auto it = insightCards.begin(); it != insightCards.end(); ++it) {
            InsightCard* card = *it;
            if (card->getInsightId() == event.insightId) {
                cardStack->removeCard(card->getCard());
                insightCards.erase(it);
                delete card;
                break;
            }
        }
        displayInterface->giveMutex();
    }
}

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

bool CardController::isFlappyBirdCardActive() const {
    if (!cardStack || !flappyBirdCardContainer) {
        return false;
    }
    if (cardStack->getCardCount() > 0) {
        lv_obj_t* currentCardObj = cardStack->getCardObjectByIndex(cardStack->getCurrentIndex());
        return currentCardObj == flappyBirdCardContainer;
    }
    return false;
} 