#ifndef GAME_CARD_H
#define GAME_CARD_H

#include "lvgl.h"
#include "ui/InputHandler.h" // Added for InputHandler interface

// Forward declaration for input handling if necessary, or include relevant input header
// For now, we assume handle_input will be called externally.

// Display configuration for the game grid
#define PLAYER_DISPLAY_COLUMN 0       // Player is fixed in the first column of the display
#define VISIBLE_ENVIRONMENT_TILES 4 // Number of upcoming environment tiles shown
#define TOTAL_DISPLAY_CELLS (VISIBLE_ENVIRONMENT_TILES + 1) // Total cells: Player + Environment

#define INITIAL_PLAYER_HP 10
#define MAX_PLAYER_HP 20 // Example max HP

enum class TileType {
    EMPTY,
    WALL,
    ENEMY_BASIC, // Basic enemy
    HEALTH_BUFF,
    SCORE_BUFF
    // PLAYER type removed, player is now a fixed visual element
};

enum class GameState {
    START_SCREEN,
    IN_GAME,
    GAME_OVER
};

struct Tile {
    char display_char;
    TileType type;
    // Future: Add more properties, e.g., enemy strength, buff amount
};

class GameCard : public InputHandler { // Inherit from InputHandler
public:
    GameCard();
    ~GameCard();

    lv_obj_t* get_card();
    // void handle_input(); // To be called on button press - REMOVED
    bool handleButtonPress(uint8_t button_index) override; // Implement InputHandler interface

private:
    lv_obj_t* screen_container;
    lv_obj_t* stats_container;      // Container for HP and Score labels
    lv_obj_t* grid_container;       // Container for game tile labels
    lv_obj_t* hp_label;
    lv_obj_t* score_label;
    lv_obj_t* message_label; // For "Press to Start", "Game Over"
    lv_obj_t* instruction_label; // For scrolling instructions
    lv_obj_t* title_label; // For the "One Button Roguelike" title
    lv_obj_t* game_tile_labels[TOTAL_DISPLAY_CELLS]; // Labels for player and environment tiles

    Tile tile_buffer[VISIBLE_ENVIRONMENT_TILES]; // Buffer for environment tiles data
    int player_hp;
    int player_score;
    GameState current_game_state;
    char last_action_message[64]; // To store the message for the last action
    // player_position_index removed/repurposed as player is visually fixed and interacts with tile_buffer[0]

    void internal_handle_input(); // Renamed original handle_input logic

    void init_game(); // Initializes game state for a new game
    void setup_ui(lv_obj_t* parent); // Sets up LVGL objects
    void update_display(); // Refreshes all dynamic UI elements
    void update_visibility(); // New function to handle UI element visibility

    void generate_and_shift_tiles(); // Operates on tile_buffer for environment
    Tile generate_random_tile();    // Generates a single new random tile
    void resolve_current_tile();  // Interacts with tile_buffer[0]

    void render_tiles();    // Updates the tile display characters
    void render_stats();    // Updates HP and Score display
    void render_message_text();  // Updates the message label based on game state

    char get_char_for_tile_type(TileType type); // Helper to get char for tile
    lv_color_t get_tile_color(TileType type); // Helper for tile colors
    
    // Basic animation (bonus)
    void animate_tile_transition();
};

#endif // GAME_CARD_H 