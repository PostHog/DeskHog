#ifndef FLAPPY_BIRD_H
#define FLAPPY_BIRD_H

#include "lvgl.h"
#include "hardware/Input.h" // For button inputs

// Basic game constants
const int FB_SCREEN_WIDTH = 240;
const int FB_SCREEN_HEIGHT = 135;
const int BIRD_SIZE = 10; // Bird is a square - Original size
const int BIRD_X_POSITION = 30; // Fixed X position of the bird from the left

// Pipe constants
const int PIPE_COUNT = 2; // Number of pipe pairs on screen at once
const int PIPE_WIDTH = 20;
const int PIPE_GAP_HEIGHT = 75; // Vertical opening for the bird
const int MIN_PIPE_HEIGHT = 15; // Minimum height for top or bottom pipe part
const int HORIZONTAL_SPACING_BETWEEN_PIPES = FB_SCREEN_WIDTH / 2 + PIPE_WIDTH / 2; // Roughly half screen apart
const float PIPE_MOVE_SPEED = 0.215625f;

enum class GameState {
    PRE_GAME,
    ACTIVE,
    GAME_OVER
};

struct PipePair {
    // lv_obj_t* top_asterisk_obj;    // LVGL label for the top asterisk
    // lv_obj_t* bottom_asterisk_obj; // LVGL label for the bottom asterisk
    lv_obj_t* top_pipe_obj;     // LVGL object for the top pipe rectangle
    lv_obj_t* bottom_pipe_obj;  // LVGL object for the bottom pipe rectangle
    float x_position;           
    int gap_y_top;              
    bool scored;                
};

class FlappyBirdGame {
public:
    FlappyBirdGame();
    ~FlappyBirdGame();

    void setup(lv_obj_t* parent_screen); // Creates the game's UI elements on parent_screen
    void loop();                     // Main game logic tick, called when this card is active
    void cleanup();                  // Cleans up LVGL objects
    lv_obj_t* get_main_container();   // Returns the root LVGL object for this game/card

private:
    void handle_input();
    void update_game_state();
    void render();
    void reset_and_initialize_pipes(); // New private method

    lv_obj_t* main_container; // The root object for this game, to be added to CardNavigationStack
    lv_obj_t* bird_obj;
    // Add other game elements like pipes, score label, etc.
    lv_obj_t* start_message_label;     // Label for "Press to Start"
    lv_obj_t* game_over_message_label; // Label for "Game Over"
    lv_obj_t* score_label;             // For displaying the score

    // Game state variables
    int bird_y;
    float bird_velocity;
    GameState current_game_state;
    int score;

    PipePair pipes[PIPE_COUNT];
};

#endif // FLAPPY_BIRD_H 