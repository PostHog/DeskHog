#include "ui/FlappyHogCard.h"
#include "hardware/Input.h"

FlappyHogCard::FlappyHogCard(lv_obj_t* parent) 
    : game(nullptr)
    , cardContainer(nullptr)
    , markedForRemoval(false) {
    
    // Create the game instance
    game = new FlappyBirdGame();
    
    // Setup the game with the parent screen
    game->setup(parent);
    
    // Get the game's container
    cardContainer = game->get_main_container();
}

FlappyHogCard::~FlappyHogCard() {
    // Clean up game resources
    if (game && !markedForRemoval) {
        game->cleanup();
        delete game;
        game = nullptr;
    }
    
    // The LVGL object will be deleted by CardNavigationStack
    // if prepareForRemoval() was called
    cardContainer = nullptr;
}

lv_obj_t* FlappyHogCard::getCard() {
    return cardContainer;
}

bool FlappyHogCard::handleButtonPress(uint8_t button_index) {
    // The game handles its own center button internally
    // We don't need to handle navigation buttons here
    // as they're handled by CardNavigationStack
    
    // Return false to allow normal navigation
    return false;
}

void FlappyHogCard::prepareForRemoval() {
    // Mark that we're being removed to prevent double deletion
    markedForRemoval = true;
    
    // The CardNavigationStack will delete the LVGL object
    // We just need to ensure we don't try to delete it again
}

bool FlappyHogCard::update() {
    if (game) {
        game->loop();
        return true; // Continue receiving updates
    }
    return false;
}