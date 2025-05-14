#include <Arduino.h> // Added for Serial object
#include "game/GameCard.h" // Assuming GameCard.h is in include/game/
#include <cstdlib>      // For std::rand, std::srand
#include <cstdio>       // For snprintf
#include <algorithm>    // For std::min, std::max
// #include <ctime>     // For std::time for srand, if available and desired

// LVGL logging (optional, for debugging)
// #define LV_LVGL_H_INCLUDE_SIMPLE
// #include "lvgl.h"

// If using ESP-IDF specific random numbers
// #include "esp_random.h"

// For Input::BUTTON_CENTER mapping - this might need to be derived from main.cpp's BUTTON_PINS array
// For now, we'll assume button_index 1 is center, as per typical ordering.
// #include "hardware/Input.h" 

const int TILE_DISPLAY_WIDTH = 30; // Approximate width for each tile label
const int TILE_DISPLAY_HEIGHT = 30; // Approximate height for each tile label
// const int STATS_LABEL_WIDTH = 100; // Not used, can be removed

// Define button indices based on typical ordering in main.cpp
const uint8_t BUTTON_INDEX_DOWN = 0;
const uint8_t BUTTON_INDEX_CENTER = 1;
const uint8_t BUTTON_INDEX_UP = 2;

GameCard::GameCard() : 
    screen_container(nullptr),
    stats_container(nullptr),
    grid_container(nullptr),
    hp_label(nullptr),
    score_label(nullptr),
    message_label(nullptr),
    instruction_label(nullptr),
    title_label(nullptr),
    player_hp(INITIAL_PLAYER_HP),
    player_score(0),
    current_game_state(GameState::START_SCREEN)
    // player_position_index is no longer used here, player interacts with tile_buffer[0]
{
    last_action_message[0] = '\0'; // Initialize to empty string
    // Seed random number generator - ESP specific might be better if available
    // std::srand(std::time(0)); // Requires <ctime>
    // For ESP32, esp_random() is better. For now, let rand() be as is or fixed seed for dev.
    std::srand(12345); // Fixed seed for predictable testing initially

    for (int i = 0; i < TOTAL_DISPLAY_CELLS; ++i) {
        game_tile_labels[i] = nullptr;
    }
    // tile_buffer (environment) is initialized in init_game()
    init_game();
}

GameCard::~GameCard() {
    if (screen_container) {
        lv_obj_del(screen_container); // This should delete all children too
        screen_container = nullptr;
    }
}

void GameCard::init_game() {
    player_hp = INITIAL_PLAYER_HP;
    player_score = 0;
    last_action_message[0] = '\0'; // Reset action message
    
    // Initialize environment tiles
    for (int i = 0; i < VISIBLE_ENVIRONMENT_TILES; ++i) {
        tile_buffer[i] = generate_random_tile(); 
        tile_buffer[i].display_char = get_char_for_tile_type(tile_buffer[i].type);
    }
}

lv_obj_t* GameCard::get_card() {
    // Serial.println("[GameCard-DEBUG] get_card() called.");
    if (!screen_container) {
        // Serial.println("[GameCard-DEBUG] get_card(): screen_container is null, creating new one.");
        lv_obj_t* parent_obj = lv_scr_act(); 
        screen_container = lv_obj_create(parent_obj);
        lv_obj_set_size(screen_container, lv_pct(100), lv_pct(100));
        lv_obj_align(screen_container, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_flex_flow(screen_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(screen_container, 20, 0); // Increased spacing between main sections
        
        // Set dark background for the game card
        lv_obj_set_style_bg_color(screen_container, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(screen_container, LV_OPA_COVER, 0);

        setup_ui(screen_container);
        update_display();
        // Serial.printf("[GameCard-DEBUG] get_card(): screen_container created: %p\n", screen_container);
    }
    return screen_container;
}

void GameCard::setup_ui(lv_obj_t* parent_obj) {
    // Serial.printf("[GameCard-DEBUG] setup_ui called with parent: %p\n", parent_obj);
    
    // Title Label (Flashing "One Button Roguelike")
    title_label = lv_label_create(parent_obj);
    lv_label_set_text(title_label, "One Button Roguelike");
    lv_obj_set_width(title_label, lv_pct(90));
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    // lv_obj_set_style_text_color(title_label, lv_color_white(), 0); // Original initial color
    // Consider using a different font or size if available and desired
    // lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

    // Flashing animation for the title - MODIFIED for RED/YELLOW color flash
    lv_obj_set_style_text_opa(title_label, LV_OPA_COVER, 0); // Ensure text is always opaque
    lv_obj_set_style_text_color(title_label, lv_palette_main(LV_PALETTE_RED), 0); // Initial color Red

    lv_anim_t title_anim;
    lv_anim_init(&title_anim);
    lv_anim_set_var(&title_anim, title_label);
    lv_anim_set_values(&title_anim, 0, 1); // Animate between state 0 (Red) and state 1 (Yellow)
    lv_anim_set_time(&title_anim, 750); 
    lv_anim_set_playback_time(&title_anim, 750); 
    lv_anim_set_repeat_count(&title_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&title_anim, lv_anim_path_step); // Use step path for sharp color change

    lv_anim_set_custom_exec_cb(&title_anim, [](lv_anim_t* a, int32_t v) {
        lv_obj_t* obj = static_cast<lv_obj_t*>(a->var);
        if (v == 0) { // State 0
            lv_obj_set_style_text_color(obj, lv_palette_main(LV_PALETTE_RED), 0);
        } else { // State 1
            lv_obj_set_style_text_color(obj, lv_palette_main(LV_PALETTE_YELLOW), 0);
        }
    });
    lv_anim_start(&title_anim);
    
    // Stats Container
    stats_container = lv_obj_create(parent_obj);
    lv_obj_set_width(stats_container, lv_pct(95));
    lv_obj_set_height(stats_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_style(stats_container, NULL, LV_PART_SCROLLBAR | LV_STATE_ANY); // Remove border/padding from default obj
    lv_obj_set_style_bg_opa(stats_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(stats_container, 2, 0);

    hp_label = lv_label_create(stats_container);
    lv_label_set_text(hp_label, "HP: --");
    lv_obj_set_style_text_color(hp_label, lv_color_white(), 0);

    score_label = lv_label_create(stats_container);
    lv_label_set_text(score_label, "Score: --");
    lv_obj_set_style_text_color(score_label, lv_color_white(), 0);

    // Game Grid Container
    grid_container = lv_obj_create(parent_obj);
    lv_obj_set_width(grid_container, lv_pct(95));
    lv_obj_set_height(grid_container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(grid_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_style(grid_container, NULL, LV_PART_SCROLLBAR | LV_STATE_ANY); // Remove border/padding
    lv_obj_set_style_bg_opa(grid_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(grid_container, 0, 0);
    lv_obj_set_style_pad_column(grid_container, 2, 0); // Spacing between tiles

    for (int i = 0; i < TOTAL_DISPLAY_CELLS; ++i) {
        game_tile_labels[i] = lv_label_create(grid_container);
        lv_label_set_text(game_tile_labels[i], " ");
        lv_obj_set_size(game_tile_labels[i], TILE_DISPLAY_WIDTH, TILE_DISPLAY_HEIGHT);
        lv_obj_set_style_text_align(game_tile_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(game_tile_labels[i], LV_ALIGN_CENTER, 0, 0); 
        // Initial text color set in render_tiles
    }

    // Message Label (Start/Game Over)
    message_label = lv_label_create(parent_obj);
    lv_label_set_text(message_label, ""); // Text set in render_message_text
    lv_obj_set_width(message_label, lv_pct(90));
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(message_label, lv_color_white(), 0);
    lv_obj_align(message_label, LV_ALIGN_CENTER, 0, 0);
    // lv_obj_set_style_margin_top(message_label, 5, 0); // Margin handled by parent flexbox pad_row

    // Instruction Label (Scrolling on Start Screen)
    instruction_label = lv_label_create(parent_obj);
    lv_obj_set_width(instruction_label, lv_pct(90)); 
    lv_label_set_text(instruction_label, "Player: @ | Center:Advance | Avoid: # (Wall,-1HP) < (Enemy,-3HP) | Get: + (Heal) $ (Score)");
    lv_label_set_long_mode(instruction_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_LEFT, 0); // Use LEFT for consistent scroll start
    lv_obj_set_style_text_color(instruction_label, lv_color_hex(0xCCCCCC), 0); 
    lv_obj_align(instruction_label, LV_ALIGN_CENTER, 0, 0); 
    // lv_obj_set_style_margin_top(instruction_label, 5, 0); // Margin handled by parent flexbox pad_row
}

// Implements InputHandler interface
bool GameCard::handleButtonPress(uint8_t button_index) {
    if (button_index == BUTTON_INDEX_CENTER) { 
        internal_handle_input();
        return true; 
    }
    return false; 
}

// Renamed original handle_input logic
void GameCard::internal_handle_input() {
    switch (current_game_state) {
        case GameState::START_SCREEN:
            current_game_state = GameState::IN_GAME;
            init_game();
            break;
        case GameState::IN_GAME:
            resolve_current_tile();
            if (player_hp <= 0) {
                current_game_state = GameState::GAME_OVER;
            } else {
                generate_and_shift_tiles();
            }
            break;
        case GameState::GAME_OVER:
            current_game_state = GameState::START_SCREEN;
            break;
    }
    update_display();
}

void GameCard::update_display() {
    if (!screen_container) return;
    update_visibility(); // Control what is shown/hidden
    render_stats();      // Update text if visible
    render_tiles();      // Update tiles if visible
    render_message_text(); // Update message text if visible
}

void GameCard::update_visibility() {
    if (!stats_container || !grid_container || !message_label || !instruction_label || !title_label) return;

    bool is_start_screen = (current_game_state == GameState::START_SCREEN);
    bool is_in_game = (current_game_state == GameState::IN_GAME);
    bool is_game_over = (current_game_state == GameState::GAME_OVER);

    // Stats (HP/Score)
    if (is_in_game || is_game_over) lv_obj_clear_flag(stats_container, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(stats_container, LV_OBJ_FLAG_HIDDEN);

    // Game Grid (Tiles)
    if (is_in_game) lv_obj_clear_flag(grid_container, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(grid_container, LV_OBJ_FLAG_HIDDEN);

    // Message Label ("Press to Start", "Game Over", or Action Message)
    // Show if on start/game over screens, or if in game and there's a message to display.
    if (is_start_screen || is_game_over || (is_in_game && last_action_message[0] != '\0')) {
        lv_obj_clear_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Hide if in game and no message, or other states not covered.
        lv_obj_add_flag(message_label, LV_OBJ_FLAG_HIDDEN);
    }

    // Instruction Label (Scrolling)
    if (is_start_screen) lv_obj_clear_flag(instruction_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(instruction_label, LV_OBJ_FLAG_HIDDEN);

    // Title Label ("One Button Roguelike")
    if (is_start_screen) lv_obj_clear_flag(title_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
}

void GameCard::render_stats() {
    if (lv_obj_has_flag(stats_container, LV_OBJ_FLAG_HIDDEN)) return;
    if (!hp_label || !score_label) return;
    char text_buffer[32];
    snprintf(text_buffer, sizeof(text_buffer), "HP: %d", player_hp);
    lv_label_set_text(hp_label, text_buffer);
    snprintf(text_buffer, sizeof(text_buffer), "Score: %d", player_score);
    lv_label_set_text(score_label, text_buffer);
}

void GameCard::render_tiles() {
    if (lv_obj_has_flag(grid_container, LV_OBJ_FLAG_HIDDEN)) return;
    if (game_tile_labels[PLAYER_DISPLAY_COLUMN]) {
        lv_label_set_text(game_tile_labels[PLAYER_DISPLAY_COLUMN], "@");
        lv_obj_set_style_text_color(game_tile_labels[PLAYER_DISPLAY_COLUMN], lv_color_white(), 0);
    }
    for (int i = 0; i < VISIBLE_ENVIRONMENT_TILES; ++i) {
        int display_label_idx = PLAYER_DISPLAY_COLUMN + 1 + i;
        if (display_label_idx < TOTAL_DISPLAY_CELLS && game_tile_labels[display_label_idx]) {
            Tile& current_env_tile = tile_buffer[i];
            char tile_char_str[2] = {current_env_tile.display_char, '\0'};
            lv_label_set_text(game_tile_labels[display_label_idx], tile_char_str);
            lv_obj_set_style_text_color(game_tile_labels[display_label_idx], get_tile_color(current_env_tile.type), 0);
        }
    }
}

void GameCard::render_message_text() { // Renamed from render_message
    if (lv_obj_has_flag(message_label, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(instruction_label, LV_OBJ_FLAG_HIDDEN)) return;
    if (!message_label || !instruction_label) return; // instruction_label check might be redundant if only message_label matters here

    switch (current_game_state) {
        case GameState::START_SCREEN:
            lv_label_set_text(message_label, "Press Center Button\nTo Start");
            // Instruction text is set in setup_ui and visibility handled by update_visibility
            break;
        case GameState::IN_GAME:
            if (last_action_message[0] != '\0') {
                lv_label_set_text(message_label, last_action_message);
            } else {
                lv_label_set_text(message_label, ""); // Clear if no message, or message_label will be hidden by update_visibility
            }
            break;
        case GameState::GAME_OVER:
            char game_over_msg[64];
            snprintf(game_over_msg, sizeof(game_over_msg), "Game Over!\nScore: %d\nPress to Restart", player_score);
            lv_label_set_text(message_label, game_over_msg);
            break;
    }
}

char GameCard::get_char_for_tile_type(TileType type) {
    switch (type) {
        case TileType::EMPTY:       return '.';
        case TileType::WALL:        return '#';
        case TileType::ENEMY_BASIC: return '<';
        case TileType::HEALTH_BUFF: return '+';
        case TileType::SCORE_BUFF:  return '$';
        default:                    return '?'; // Should not happen
    }
}

lv_color_t GameCard::get_tile_color(TileType type) {
    switch (type) {
        case TileType::EMPTY:       return lv_color_hex(0x555555); // Darker Grey
        case TileType::WALL:        return lv_color_hex(0xAAAAAA); // Lighter Grey
        case TileType::ENEMY_BASIC: return lv_palette_main(LV_PALETTE_RED);
        case TileType::HEALTH_BUFF: return lv_palette_main(LV_PALETTE_GREEN);
        case TileType::SCORE_BUFF:  return lv_palette_main(LV_PALETTE_YELLOW);
        default:                    return lv_color_white(); // Default fallback
    }
}

Tile GameCard::generate_random_tile() {
    int rand_value = std::rand() % 100;
    Tile new_tile;
    if (rand_value < 40) { new_tile.type = TileType::EMPTY; }
    else if (rand_value < 60) { new_tile.type = TileType::ENEMY_BASIC; }
    else if (rand_value < 75) { new_tile.type = TileType::WALL; }
    else if (rand_value < 88) { new_tile.type = TileType::HEALTH_BUFF; }
    else { new_tile.type = TileType::SCORE_BUFF; }
    new_tile.display_char = get_char_for_tile_type(new_tile.type);
    return new_tile;
}

void GameCard::generate_and_shift_tiles() {
    // Shift environment tiles left
    for (int i = 0; i < VISIBLE_ENVIRONMENT_TILES - 1; ++i) {
        tile_buffer[i] = tile_buffer[i + 1];
    }
    // Generate a new tile for the end of the environment buffer
    tile_buffer[VISIBLE_ENVIRONMENT_TILES - 1] = generate_random_tile();
}

void GameCard::resolve_current_tile() {
    // Player interacts with the first tile in the environment buffer
    if (VISIBLE_ENVIRONMENT_TILES == 0) return; // No environment tiles to interact with

    Tile& current_env_tile = tile_buffer[0];
    // Clear previous message before resolving new one
    // last_action_message[0] = '\0'; // Let's set specific messages or clear if no action.

    switch (current_env_tile.type) {
        case TileType::EMPTY:
            player_score += 1;
            snprintf(last_action_message, sizeof(last_action_message), "Moved. (+1 Score)");
            break;
        case TileType::WALL:
            player_hp -= 1;
            snprintf(last_action_message, sizeof(last_action_message), "Hit a wall! (-1 HP)");
            break;
        case TileType::ENEMY_BASIC:
            player_hp -= 3;
            player_score += 5;
            snprintf(last_action_message, sizeof(last_action_message), "Hit enemy! -3HP +5pts");
            break;
        case TileType::HEALTH_BUFF:
            player_hp = std::min(player_hp + 5, MAX_PLAYER_HP);
            player_score += 2;
            snprintf(last_action_message, sizeof(last_action_message), "Health! (+5HP,+2Pts)");
            break;
        case TileType::SCORE_BUFF:
            player_score += 25;
            snprintf(last_action_message, sizeof(last_action_message), "Treasure! (+25 Score)");
            break;
        default:
            last_action_message[0] = '\0'; // Clear message if tile type is unknown or has no specific message
            break;
    }
}

void GameCard::animate_tile_transition() {
    // Animation logic for player_position_index needs update if we re-enable this,
    // as player is now fixed. Animation might apply to tile_buffer[0] display.
    // if (game_tile_labels[PLAYER_DISPLAY_COLUMN + 1]) { ... animate this label ... }
} 