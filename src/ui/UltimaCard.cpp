#include "UltimaCard.h"
#include "hardware/Input.h" // For BUTTON_UP, BUTTON_DOWN, BUTTON_CENTER
#include "sprites/3buttondungeon/sprite_3buttondungeon.h" // Path for splash image

// Define the game font - using unscii_16 for all Ultima text
LV_FONT_DECLARE(lv_unscii_16_custom_symbols); // Declare the custom symbols font
static lv_font_t ultima_font_with_fallback;   // Static font object to hold main font + fallback
#define ULTIMA_GAME_FONT &ultima_font_with_fallback // Using unscii_16 with custom symbols fallback

// Fallback for LV_FONT_MONO if unscii_16 isn't designated as a mono font by LVGL
// (though for unscii, it effectively is)
#ifndef LV_FONT_MONO
#define LV_FONT_MONO ULTIMA_GAME_FONT
#endif

// Static variables for combo action debouncing
static unsigned long last_combo_action_time = 0;
const unsigned long COMBO_COOLDOWN_MS = 100; // Cooldown for combo actions (milliseconds)
static unsigned long last_up_press_time = 0;
static unsigned long last_center_press_time = 0;
static unsigned long last_down_press_time = 0;
const unsigned long COMBO_WINDOW_MS = 200; // Window for button combo detection

UltimaCard::UltimaCard(uint16_t width, uint16_t height)
    : card_obj(nullptr), 
      map_label(nullptr), stats_label(nullptr), message_label(nullptr),
      splash_screen_img(nullptr), // Initialize new member
      start_screen_title_label(nullptr), start_screen_instructions_label(nullptr),
      card_width(width), card_height(height), 
      current_display_state(UltimaCardDisplayState::SHOWING_SPLASH_SCREEN) { // Default to splash screen
    // Game engine is initialized by its own constructor

    // Initialize the fallback font
    memcpy(&ultima_font_with_fallback, &lv_font_unscii_16, sizeof(lv_font_t));
    ultima_font_with_fallback.fallback = &lv_unscii_16_custom_symbols;
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

    // --- Splash Screen UI Element ---
    splash_screen_img = lv_img_create(card_obj);
    lv_img_set_src(splash_screen_img, &sprite_3buttondungeon);
    lv_obj_align(splash_screen_img, LV_ALIGN_CENTER, 0, 0);

    // --- Game UI Elements (created next, visibility managed by state) ---
    uint16_t game_message_area_height = 20; // Adjusted for potentially larger font
    uint16_t game_main_area_height = card_height - game_message_area_height - 4;
    uint16_t game_stats_area_width = 80; // Adjusted for potentially larger font
    uint16_t game_map_area_width = card_width - game_stats_area_width - 8; // Adjusted

    map_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(map_label, ULTIMA_GAME_FONT, 0);
    lv_obj_set_style_text_color(map_label, lv_color_white(), 0);
    lv_obj_set_size(map_label, game_map_area_width, game_main_area_height);
    lv_label_set_long_mode(map_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(map_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(map_label, LV_ALIGN_TOP_LEFT, 0, 0);

    stats_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(stats_label, ULTIMA_GAME_FONT, 0);
    lv_obj_set_style_text_color(stats_label, lv_color_hex(0xFFD700), 0); 
    lv_obj_set_size(stats_label, game_stats_area_width, game_main_area_height);
    lv_label_set_long_mode(stats_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(stats_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    message_label = lv_label_create(card_obj); // Common message label
    lv_obj_set_style_text_font(message_label, ULTIMA_GAME_FONT, 0);
    lv_obj_set_style_text_color(message_label, lv_color_hex(0xADD8E6), 0); 
    lv_obj_set_size(message_label, card_width - 4, game_message_area_height - 2);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // Initial alignment/text set in setDisplayState

    // --- Start Screen UI Elements (Text based) ---
    // start_screen_title_label = lv_label_create(card_obj);
    // lv_obj_set_style_text_font(start_screen_title_label, ULTIMA_GAME_FONT, 0);
    // lv_obj_set_style_text_color(start_screen_title_label, lv_color_white(), 0);
    // lv_label_set_text(start_screen_title_label, "How to Play");
    // lv_obj_align(start_screen_title_label, LV_ALIGN_TOP_MID, 0, 5); // Adjusted y for larger font

    start_screen_instructions_label = lv_label_create(card_obj);
    lv_obj_set_style_text_font(start_screen_instructions_label, ULTIMA_GAME_FONT, 0);
    lv_obj_set_style_text_color(start_screen_instructions_label, lv_color_hex(0xCCCCCC), 0);
    lv_label_set_long_mode(start_screen_instructions_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(start_screen_instructions_label, card_width - 10); // Padded width
    lv_label_set_text(start_screen_instructions_label, 
        "UP/DOWN: Move\n"
        "MID: Use\n"
        "MID+UP/DOWN: Right/Left\n"
        "UP+DOWN: Restart"
    );
    lv_obj_align(start_screen_instructions_label, LV_ALIGN_TOP_MID, 0, 5); // Adjusted y: move to where title was

    setDisplayState(UltimaCardDisplayState::SHOWING_SPLASH_SCREEN); // Initial state
    
    return card_obj;
}

void UltimaCard::setDisplayState(UltimaCardDisplayState new_state) {
    current_display_state = new_state;

    // Hide all toggleable elements first
    if(splash_screen_img) lv_obj_add_flag(splash_screen_img, LV_OBJ_FLAG_HIDDEN);
    // if(start_screen_title_label) lv_obj_add_flag(start_screen_title_label, LV_OBJ_FLAG_HIDDEN);
    if(start_screen_instructions_label) lv_obj_add_flag(start_screen_instructions_label, LV_OBJ_FLAG_HIDDEN);
    if(map_label) lv_obj_add_flag(map_label, LV_OBJ_FLAG_HIDDEN);
    if(stats_label) lv_obj_add_flag(stats_label, LV_OBJ_FLAG_HIDDEN);

    switch (new_state) {
        case UltimaCardDisplayState::SHOWING_SPLASH_SCREEN:
            if(splash_screen_img) lv_obj_clear_flag(splash_screen_img, LV_OBJ_FLAG_HIDDEN);
            if(message_label) {
                lv_label_set_text(message_label, ""); // Set to empty string
                lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(message_label, LV_ALIGN_BOTTOM_MID, 0, -2);
            }
            break;
        case UltimaCardDisplayState::SHOWING_START_SCREEN:
            // if(start_screen_title_label) lv_obj_clear_flag(start_screen_title_label, LV_OBJ_FLAG_HIDDEN);
            if(start_screen_instructions_label) lv_obj_clear_flag(start_screen_instructions_label, LV_OBJ_FLAG_HIDDEN);
            if(message_label) {
                lv_label_set_text(message_label, ""); // Set to empty string
        lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_align(message_label, LV_ALIGN_BOTTOM_MID, 0, -2);
            }
            break;
        case UltimaCardDisplayState::SHOWING_GAME:
            if(map_label) lv_obj_clear_flag(map_label, LV_OBJ_FLAG_HIDDEN);
            if(stats_label) lv_obj_clear_flag(stats_label, LV_OBJ_FLAG_HIDDEN);
            if(message_label) {
                lv_label_set_text(message_label, "Welcome adventurer! Explore the desert, defeat devils and seals caves to get stronger."); // Updated welcome message
                lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_LEFT, 0);
                lv_obj_align(message_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
            }
        updateView(); // Initial render of game map and stats
            break;
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
    if (current_display_state == UltimaCardDisplayState::SHOWING_GAME) {
        // Update map display
        String map_view = game_engine.renderView();
        lv_label_set_text(map_label, map_view.c_str());

        // Update stats display
        String stats = game_engine.getFormattedStats();
        lv_label_set_text(stats_label, stats.c_str());

        // Update message display
        String turn_message = game_engine.getTurnMessageAndClear();
        if (turn_message.length() > 0) {
            lv_label_set_text(message_label, turn_message.c_str());
        }

        // Check for game over or win state
        if (game_engine.isPlayerDefeated()) {
            lv_label_set_text(message_label, "GAME OVER");
            lv_obj_set_style_text_color(message_label, lv_color_make(255, 0, 0), 0); // Red color
        } else if (game_engine.isGameWon()) {
            lv_label_set_text(message_label, "YOU WIN ☻");
            lv_obj_set_style_text_color(message_label, lv_color_make(0, 255, 0), 0); // Green color
        }
    }
}

bool UltimaCard::handleButtonPress(uint8_t button_index) {
    unsigned long current_time = millis();
    
    // Check for restart combo (UP+DOWN) first, regardless of state
    if (Input::isUpPressed() && Input::isDownPressed()) {
        game_engine.restartGame();
        setDisplayState(UltimaCardDisplayState::SHOWING_SPLASH_SCREEN);
        updateView();
        return true;
    }

    // Update button press timestamps
    if (button_index == Input::BUTTON_UP) {
        last_up_press_time = current_time;
    } else if (button_index == Input::BUTTON_CENTER) {
        last_center_press_time = current_time;
    } else if (button_index == Input::BUTTON_DOWN) {
        last_down_press_time = current_time;
    }

    if (current_display_state == UltimaCardDisplayState::SHOWING_SPLASH_SCREEN) {
        if (button_index == Input::BUTTON_CENTER) {
            setDisplayState(UltimaCardDisplayState::SHOWING_START_SCREEN);
            return true;
        }
    } else if (current_display_state == UltimaCardDisplayState::SHOWING_START_SCREEN) {
        if (button_index == Input::BUTTON_CENTER) {
            setDisplayState(UltimaCardDisplayState::SHOWING_GAME);
            return true;
        }
    } else if (current_display_state == UltimaCardDisplayState::SHOWING_GAME) {
        // If game is won or player is defeated, only allow restart
        if (game_engine.isGameWon() || game_engine.isPlayerDefeated()) {
            if (button_index == Input::BUTTON_CENTER) {
                game_engine.restartGame();
                updateView();
                return true;
            }
            return false;
        }

        // Normal game controls
        if (button_index == Input::BUTTON_UP) {
            game_engine.movePlayer(0, -1);
            game_engine.moveMonsters(); // Add monster movement
            updateView();
            return true;
        } else if (button_index == Input::BUTTON_DOWN) {
            game_engine.movePlayer(0, 1);
            game_engine.moveMonsters(); // Add monster movement
            updateView();
            return true;
        } else if (button_index == Input::BUTTON_CENTER) {
            // Check if Up was pressed within the combo window
            bool up_pressed_recently = (current_time - last_up_press_time) < COMBO_WINDOW_MS;
            bool down_pressed_recently = (current_time - last_down_press_time) < COMBO_WINDOW_MS;
            
            Serial.printf("[UltimaCard] Button timing - Up: %lu ms ago, Down: %lu ms ago\n", 
                current_time - last_up_press_time,
                current_time - last_down_press_time);

            if (up_pressed_recently) {
                Serial.println("[UltimaCard] Center + Up combo detected - moving right");
                game_engine.movePlayer(1, 0); // Right
                game_engine.moveMonsters(); // Add monster movement
                updateView();
                return true;
            } else if (down_pressed_recently) {
                Serial.println("[UltimaCard] Center + Down combo detected - moving left");
                game_engine.movePlayer(-1, 0); // Left
                game_engine.moveMonsters(); // Add monster movement
                updateView();
                return true;
            } else {
                Serial.println("[UltimaCard] Center pressed alone - attempting to search");
                String search_result = game_engine.searchCurrentTile();
                if (search_result.length() > 0) {
                    lv_label_set_text(message_label, search_result.c_str());
                }
                game_engine.moveMonsters(); // Add monster movement even after searching
                updateView();
                return true;
            }
        }
    }
    return false;
} 