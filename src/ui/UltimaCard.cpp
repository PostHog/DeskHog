#include "UltimaCard.h"
#include "hardware/Input.h" // For BUTTON_UP, BUTTON_DOWN, BUTTON_CENTER

// Font definitions for start screen (can be adjusted based on lv_conf.h)
#if LV_FONT_MONTSERRAT_16
#define START_TITLE_FONT &lv_font_montserrat_16
#else
#define START_TITLE_FONT &lv_font_unscii_8 // Fallback for title
#endif
#define START_INSTRUCTIONS_FONT &lv_font_unscii_8

// Game uses unscii_8, defined in original UltimaCard.cpp
// Ensure lv_font_unscii_8 is enabled in lv_conf.h

// For now, let's assume a default mono font. If not available, this needs adjustment.
// Ideally, enable LV_FONT_MONTSERRAT_10 or a specific mono font in lv_conf.h
#ifndef LV_FONT_MONO
#define LV_FONT_MONO LV_FONT_DEFAULT
#endif

// Static variables for combo action debouncing
static unsigned long last_combo_action_time = 0;
const unsigned long COMBO_COOLDOWN_MS = 100; // Cooldown for combo actions (milliseconds)

UltimaCard::UltimaCard(uint16_t width, uint16_t height)
    : card_obj(nullptr), 
      map_label(nullptr), stats_label(nullptr), message_label(nullptr),
      start_screen_title_label(nullptr), start_screen_instructions_label(nullptr),
      card_width(width), card_height(height), 
      current_display_state(UltimaCardDisplayState::SHOWING_START_SCREEN) { // Default to start screen
}

UltimaCard::~UltimaCard() {
    // LVGL handles deletion of children automatically
}

lv_obj_t* UltimaCard::createCard(lv_obj_t* parent) {
    card_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(card_obj);
    lv_obj_set_size(card_obj, card_width, card_height);
    lv_obj_set_style_bg_color(card_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(card_obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(card_obj, 2, 0); 
    lv_obj_center(card_obj);

    // --- Game UI Elements (created first, visibility managed by state) ---
    uint16_t game_message_area_height = 12;
    uint16_t game_main_area_height = card_height - game_message_area_height - 4;
    uint16_t game_stats_area_width = 70;
    uint16_t game_map_area_width = card_width - game_stats_area_width - 6;

    map_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(map_label, &lv_font_unscii_8, 0); 
    lv_obj_set_style_text_color(map_label, lv_color_white(), 0);
    lv_obj_set_size(map_label, game_map_area_width, game_main_area_height);
    lv_label_set_long_mode(map_label, LV_LABEL_LONG_CLIP);
    lv_obj_align(map_label, LV_ALIGN_TOP_LEFT, 0, 0);

    stats_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(stats_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(0xFFD700), 0); 
    lv_obj_set_size(stats_label, game_stats_area_width, game_main_area_height);
    lv_label_set_long_mode(stats_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(stats_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    message_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(message_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(0xADD8E6), 0); 
    lv_obj_set_size(message_label, card_width - 4, game_message_area_height -2);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // --- Start Screen UI Elements ---
    start_screen_title_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(start_screen_title_label, START_TITLE_FONT, 0);
    lv_obj_set_style_text_color(start_screen_title_label, lv_color_white(), 0);
    lv_label_set_text(start_screen_title_label, "Three Button Dungeon");
    lv_obj_align(start_screen_title_label, LV_ALIGN_TOP_MID, 0, 10); // Position title

    start_screen_instructions_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(start_screen_instructions_label, START_INSTRUCTIONS_FONT, 0);
    lv_obj_set_style_text_color(start_screen_instructions_label, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_long_mode(start_screen_instructions_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(start_screen_instructions_label, card_width - 20); // Padded width
    lv_label_set_text(start_screen_instructions_label, 
        "UP: Move Up\n"
        "DOWN: Move Down\n"
        "CENTER: Search/Interact\n\n"
        "COMBOS:\n"
        "CENTER + UP: Move Right\n"
        "CENTER + DOWN: Move Left\n\n"
    );
    lv_obj_align(start_screen_instructions_label, LV_ALIGN_CENTER, 0, 15); // Position instructions

    // Set initial display state
    setDisplayState(UltimaCardDisplayState::SHOWING_START_SCREEN);
    
    return card_obj;
}

void UltimaCard::setDisplayState(UltimaCardDisplayState new_state) {
    current_display_state = new_state;

    if (current_display_state == UltimaCardDisplayState::SHOWING_START_SCREEN) {
        lv_obj_clear_flag(start_screen_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(start_screen_instructions_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(message_label, "Press CENTER to Start");
        lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(message_label, LV_ALIGN_BOTTOM_MID, 0, -2); // Center message at bottom

        lv_obj_add_flag(map_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(stats_label, LV_OBJ_FLAG_HIDDEN);
    } else { // SHOWING_GAME
        lv_obj_add_flag(start_screen_title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(start_screen_instructions_label, LV_OBJ_FLAG_HIDDEN);
        
        lv_obj_clear_flag(map_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(stats_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(message_label, "Welcome to the Desert!");
        lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_LEFT, 0); // Default for game
        lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, 0, 0); // Default for game

        updateView(); // Initial render of game map and stats
    }
}

void UltimaCard::updateMapDisplay() {
    if (current_display_state != UltimaCardDisplayState::SHOWING_GAME) return;
    if (map_label) {
        String view = game_engine.renderView();
        lv_label_set_text(map_label, view.c_str());
    }
}

void UltimaCard::updateStatsDisplay() {
    if (current_display_state != UltimaCardDisplayState::SHOWING_GAME) return;
    if (stats_label) {
        String stats = game_engine.getFormattedStats();
        lv_label_set_text(stats_label, stats.c_str());
    }
}

void UltimaCard::updateView() {
    if (current_display_state != UltimaCardDisplayState::SHOWING_GAME) return;
    updateMapDisplay();
    updateStatsDisplay();
}

bool UltimaCard::handleButtonPress(uint8_t button_index) {
    if (current_display_state == UltimaCardDisplayState::SHOWING_START_SCREEN) {
        if (button_index == Input::BUTTON_CENTER) {
            setDisplayState(UltimaCardDisplayState::SHOWING_GAME);
            return true; // Handled
        }
        return true; // Consume other button presses on start screen
    }

    // --- Game Running Logic (existing logic) ---
    bool handled = false;
    String msg_text = ""; 
    unsigned long current_time_ms = millis();

    bool up_currently_pressed = Input::isUpPressed();
    bool down_currently_pressed = Input::isDownPressed();
    bool center_currently_pressed = Input::isCenterPressed();

    if (center_currently_pressed && up_currently_pressed) {
        if (current_time_ms - last_combo_action_time > COMBO_COOLDOWN_MS) {
            game_engine.movePlayer(1, 0); // Move Right
            updateMapDisplay();
            msg_text = "Moved Right."; 
            last_combo_action_time = current_time_ms;
        }
        handled = true; 
    } else if (center_currently_pressed && down_currently_pressed) {
        if (current_time_ms - last_combo_action_time > COMBO_COOLDOWN_MS) {
            game_engine.movePlayer(-1, 0); // Move Left
            updateMapDisplay();
            msg_text = "Moved Left.";
            last_combo_action_time = current_time_ms;
        }
        handled = true; 
    }

    if (!handled) {
        switch (button_index) {
            case Input::BUTTON_UP:
                game_engine.movePlayer(0, -1); // Move Up
                updateMapDisplay();
                msg_text = "Moved Up.";
                handled = true;
                break;
            case Input::BUTTON_DOWN:
                game_engine.movePlayer(0, 1);  // Move Down
                updateMapDisplay();
                msg_text = "Moved Down.";
                handled = true;
                break;
            case Input::BUTTON_CENTER:
                msg_text = game_engine.searchCurrentTile();
                // If search causes stat changes (e.g. oasis healing), update stats display
                updateStatsDisplay(); 
                handled = true; 
                break;
        }
    }

    if (message_label && !msg_text.isEmpty()) {
        lv_label_set_text(message_label, msg_text.c_str());
    }
    return handled;
} 