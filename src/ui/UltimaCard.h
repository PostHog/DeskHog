#pragma once

#include <lvgl.h>
#include "../game/UltimaGame.h"
#include "InputHandler.h" // Assuming InputHandler.h is in the ui directory

class UltimaCard : public InputHandler {
public:
    enum class UltimaCardDisplayState {
        SHOWING_SPLASH_SCREEN,
        SHOWING_START_SCREEN,
        SHOWING_GAME
    };

    UltimaCard(uint16_t width, uint16_t height);
    ~UltimaCard();

    lv_obj_t* createCard(lv_obj_t* parent);
    void updateView();

    // From InputHandler
    bool handleButtonPress(uint8_t button_index) override;

    lv_obj_t* getLvglObject() { return card_obj; }

private:
    void updateMapDisplay();    // New: Method to update only the map label
    void updateStatsDisplay();  // New: Method to update only the stats label
    void setDisplayState(UltimaCardDisplayState new_state);

    UltimaGame game_engine;
    lv_obj_t* card_obj; // The main LVGL object for this card
    lv_obj_t* map_label; // LVGL label to display ASCII game map (renamed from game_label)
    lv_obj_t* stats_label; // LVGL label for player statistics
    lv_obj_t* message_label; // LVGL label for search messages etc.

    // Splash Screen UI element
    lv_obj_t* splash_screen_img; // New: For the splash screen image

    // Start Screen UI elements
    lv_obj_t* start_screen_title_label;
    lv_obj_t* start_screen_instructions_label;

    uint16_t card_width;
    uint16_t card_height;
    UltimaCardDisplayState current_display_state;
}; 