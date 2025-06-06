#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <vector>
#include <string>
#include "ui/InputHandler.h" // Assuming InputHandler is in ui directory

// Forward declaration for LVGL objects if needed, or include necessary LVGL headers
// struct lv_obj_t;

// Game constants
const int ROGUELITE_GRID_WIDTH = 10;
const int ROGUELITE_GRID_HEIGHT = 1; // Player moves in a single line, we show a "slice"
const int ROGUELITE_TILE_BUFFER_SIZE = ROGUELITE_GRID_WIDTH + 5; // How many tiles ahead we generate
const int ROGUELITE_PLAYER_VISIBLE_X = 1; // Player is at the 2nd position in the visible grid
const int ROGUELITE_MAX_HP = 10;


enum class RogueliteGameState {
    START_SCREEN,
    IN_GAME,
    GAME_OVER
};

enum class RogueliteTileType {
    EMPTY,      // .
    WALL,       // #
    ENEMY,      // *
    BUFF_HP,    // + (health)
    BUFF_SCORE  // $ (score)
};

struct RogueliteTile {
    RogueliteTileType type;
    char display_char;
};

class RogueliteGame : public InputHandler {
public:
    RogueliteGame(lv_obj_t* parent_screen, int screen_width, int screen_height);
    ~RogueliteGame();

    lv_obj_t* getCard(); // To be added to CardNavigationStack
    bool handleButtonPress(uint8_t button_index) override;

    void update(); // Main game loop update, called periodically if needed or on action

private:
    void init_ui();
    void reset_game();
    void process_turn(); // Advances the game by one step
    void generate_tile_row(); // Generates the next set of tiles
    RogueliteTile create_random_tile();
    void render_game_area();
    void update_status_labels();

    void show_message(const char* message_text);
    void hide_message();

    lv_obj_t* _card_container;    // Main container for this "card"
    lv_obj_t* _game_area_label;   // Label to display the ASCII grid
    lv_obj_t* _hp_label;
    lv_obj_t* _score_label;
    lv_obj_t* _message_label;     // For "Game Over", "Start", etc.
    lv_style_t _game_area_style;  // Style for monospace font if possible

    RogueliteGameState _current_state;
    int _player_hp;
    int _player_score;
    
    std::vector<RogueliteTile> _tile_buffer; // Rolling buffer of upcoming tiles
    int _current_buffer_idx; // Player's current position within the tile buffer

    int _screen_width;
    int _screen_height;

    lv_font_t* _game_font; // To store a monospace font if found

    void setup_styles();
    void check_and_apply_default_font();
}; 