#include "ui/PongCard.h"
#include "hardware/Input.h" 
#include "Arduino.h" // For millis()
#include <cstdlib>  // For rand() and srand()
#include <ctime>    // For time() to seed srand()

// Define screen dimensions, or get them from a display interface if available
const int16_t SCREEN_WIDTH = 240;
const int16_t SCREEN_HEIGHT = 135;

// Static member definitions for victory phrases
const char* PongCard::VICTORY_PHRASES[] = {
    "Hog-tastic!",
    "Hawkins would be proud",
    "Spiked-em!",
    "Prickly perfect!"
};
const int PongCard::NUM_VICTORY_PHRASES = sizeof(PongCard::VICTORY_PHRASES) / sizeof(PongCard::VICTORY_PHRASES[0]);

PongCard::PongCard(lv_obj_t* parent) :
    _card_root_obj(nullptr),
    _pong_game_instance(SCREEN_WIDTH, SCREEN_HEIGHT), // Initialize PongGame with dimensions
    _player_paddle_obj(nullptr),
    _ai_paddle_obj(nullptr),
    _ball_obj(nullptr),
    _player_score_label_obj(nullptr),
    _ai_score_label_obj(nullptr),
    _message_label_obj(nullptr),
    _is_victory_phrase_chosen(false),
    _previous_game_state(_pong_game_instance.getState()) { // Initialize with current game state

    _chosen_victory_phrase_buffer[0] = '\0';

    // Seed random number generator once
    static bool seeded = false;
    if (!seeded) {
        srand(time(nullptr));
        seeded = true;
    }

    createUi(parent);
    _pong_game_instance.setState(PongGame::GameState::StartScreen); 
    updateMessageLabel();
}

PongCard::~PongCard() {
    if (_card_root_obj) {
        lv_obj_del_async(_card_root_obj);
        _card_root_obj = nullptr;
    }
}

void PongCard::createUi(lv_obj_t* parent) {
    _card_root_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(_card_root_obj); 
    lv_obj_set_size(_card_root_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_card_root_obj, lv_color_black(), 0);
    lv_obj_set_style_pad_all(_card_root_obj, 0, 0); 

    _player_paddle_obj = lv_obj_create(_card_root_obj);
    lv_obj_set_size(_player_paddle_obj, PADDLE_WIDTH, PADDLE_HEIGHT); // Uses PongCard constants for UI size
    lv_obj_set_style_bg_color(_player_paddle_obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(_player_paddle_obj, 0, 0);

    _ai_paddle_obj = lv_obj_create(_card_root_obj);
    lv_obj_set_size(_ai_paddle_obj, PADDLE_WIDTH, PADDLE_HEIGHT); // Uses PongCard constants for UI size
    lv_obj_set_style_bg_color(_ai_paddle_obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(_ai_paddle_obj, 0, 0);

    _ball_obj = lv_obj_create(_card_root_obj);
    lv_obj_set_size(_ball_obj, BALL_DIAMETER, BALL_DIAMETER); // Uses PongCard constants for UI size
    lv_obj_set_style_radius(_ball_obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(_ball_obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(_ball_obj, 0, 0);

    _player_score_label_obj = lv_label_create(_card_root_obj);
    lv_obj_set_style_text_color(_player_score_label_obj, lv_color_white(), 0);
    lv_obj_align(_player_score_label_obj, LV_ALIGN_TOP_LEFT, 10, 5); 
    lv_label_set_text(_player_score_label_obj, "0");

    _ai_score_label_obj = lv_label_create(_card_root_obj);
    lv_obj_set_style_text_color(_ai_score_label_obj, lv_color_white(), 0);
    lv_obj_align(_ai_score_label_obj, LV_ALIGN_TOP_RIGHT, -10, 5); 
    lv_label_set_text(_ai_score_label_obj, "0");
    
    _message_label_obj = lv_label_create(_card_root_obj);
    lv_obj_set_style_text_color(_message_label_obj, lv_color_white(), 0);
    lv_obj_set_style_text_align(_message_label_obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_message_label_obj, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_long_mode(_message_label_obj, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_message_label_obj, lv_pct(80)); 
}

void PongCard::update() {
    Bounce2::Button& center_button = buttons[Input::BUTTON_CENTER];
    Bounce2::Button& up_button = buttons[Input::BUTTON_UP];
    Bounce2::Button& down_button = buttons[Input::BUTTON_DOWN];
    PongGame::GameState current_game_state = _pong_game_instance.getState();

    // --- State Transition Logic for Victory Message ---
    if (current_game_state == PongGame::GameState::GameOver && _previous_game_state != PongGame::GameState::GameOver) {
        // Just entered GameOver state
        if (_pong_game_instance.getPlayerWinState() == PongGame::PLAYER_WON) {
            int random_index = rand() % NUM_VICTORY_PHRASES;
            snprintf(_chosen_victory_phrase_buffer, sizeof(_chosen_victory_phrase_buffer), "%s\nPress Center to Restart", VICTORY_PHRASES[random_index]);
            _is_victory_phrase_chosen = true;
        } else {
            _is_victory_phrase_chosen = false; // AI won or other, no special phrase
        }
    } else if (current_game_state != PongGame::GameState::GameOver && _previous_game_state == PongGame::GameState::GameOver) {
        // Just exited GameOver state (e.g., restarted)
        _is_victory_phrase_chosen = false;
    }

    // --- Center button press logic based on current state ---
    if (center_button.pressed()) {
        if (current_game_state == PongGame::GameState::StartScreen || current_game_state == PongGame::GameState::GameOver) {
            _pong_game_instance.reset(); // This now sets the game to ServeDelay internally
            // Player paddle should be reset to non-moving state immediately
            _pong_game_instance.movePlayerPaddle(true, false);
            _pong_game_instance.movePlayerPaddle(false, false);
            // updateMessageLabel(); // PongGame state change will trigger UI update via updateUi -> updateMessageLabel
        } else if (current_game_state == PongGame::GameState::Paused) {
            _pong_game_instance.setState(PongGame::GameState::Playing); // Resume from pause
            // updateMessageLabel();
        } else if (current_game_state == PongGame::GameState::Playing) {
            _pong_game_instance.setState(PongGame::GameState::Paused); // Pause the game
            _pong_game_instance.movePlayerPaddle(true, false); // Stop player paddle
            _pong_game_instance.movePlayerPaddle(false, false);
            // updateMessageLabel();
        }
        // After any state change initiated by center button, ensure message label is current for the new state.
        // This is now more reliably handled in updateUi()->updateMessageLabel() call at the end of this function.
    }

    // Paddle movement input only if Playing
    if (current_game_state == PongGame::GameState::Playing) {
        if (down_button.isPressed()) {
            _pong_game_instance.movePlayerPaddle(false, true);
        } else if (down_button.released()) {
            _pong_game_instance.movePlayerPaddle(false, false);
        }

        if (up_button.isPressed()) {
            _pong_game_instance.movePlayerPaddle(true, true);
        } else if (up_button.released()) {
            _pong_game_instance.movePlayerPaddle(true, false);
        }

        // Check for all three buttons for GameOver (from Playing state)
        if (up_button.isPressed() && down_button.isPressed() && center_button.isPressed()) {
            _pong_game_instance.setState(PongGame::GameState::GameOver);
            _pong_game_instance.movePlayerPaddle(true, false); // Stop player paddle
            _pong_game_instance.movePlayerPaddle(false, false);
            // updateMessageLabel(); 
        }
    }

    // Call PongGame's update loop if card is valid and game is Playing or in ServeDelay
    if (lv_obj_is_valid(_card_root_obj)) {
      if (current_game_state == PongGame::GameState::Playing || 
          current_game_state == PongGame::GameState::ServeDelay) {
          _pong_game_instance.update();
      }
      updateUi(); // This will call updateMessageLabel() internally
    }

    _previous_game_state = current_game_state; // Update previous state for next frame
}

void PongCard::updateUi() {
    if (!lv_obj_is_valid(_card_root_obj)) return; // Ensure card is valid

    Coordinates player_coords = _pong_game_instance.getPlayerPaddleCoordinates();
    if (lv_obj_is_valid(_player_paddle_obj)) {
        lv_obj_set_pos(_player_paddle_obj, player_coords.x, player_coords.y);
    }

    Coordinates ai_coords = _pong_game_instance.getAiPaddleCoordinates();
    if (lv_obj_is_valid(_ai_paddle_obj)) {
        lv_obj_set_pos(_ai_paddle_obj, ai_coords.x, ai_coords.y);
    }

    Coordinates ball_coords = _pong_game_instance.getBallCoordinates();
    if (lv_obj_is_valid(_ball_obj)) {
        lv_obj_set_pos(_ball_obj, ball_coords.x, ball_coords.y);
    }

    char score_text[4];
    snprintf(score_text, sizeof(score_text), "%d", _pong_game_instance.getPlayerScore());
    if (lv_obj_is_valid(_player_score_label_obj)) lv_label_set_text(_player_score_label_obj, score_text);

    snprintf(score_text, sizeof(score_text), "%d", _pong_game_instance.getAiScore());
    if (lv_obj_is_valid(_ai_score_label_obj)) lv_label_set_text(_ai_score_label_obj, score_text);
    
    updateMessageLabel(); 
}

void PongCard::updateMessageLabel() {
    if (!lv_obj_is_valid(_message_label_obj)) return;
    PongGame::GameState current_game_state = _pong_game_instance.getState();
    // char message_buffer[100]; // No longer needed here, specific message set in _chosen_victory_phrase_buffer

    switch (current_game_state) {
        case PongGame::GameState::StartScreen:
            lv_label_set_text(_message_label_obj, "PONG!\nPress Center to Start");
            break;
        case PongGame::GameState::Playing:
            lv_obj_add_flag(_message_label_obj, LV_OBJ_FLAG_HIDDEN);
            return; // Don't clear hidden flag below
        case PongGame::GameState::Paused:
             lv_label_set_text(_message_label_obj, "PAUSED");
            break;
        case PongGame::GameState::ServeDelay:
            lv_label_set_text(_message_label_obj, "READY?");
            break;
        case PongGame::GameState::GameOver:
            if (_is_victory_phrase_chosen) {
                lv_label_set_text(_message_label_obj, _chosen_victory_phrase_buffer);
            } else {
                // AI Won or other game over condition
                lv_label_set_text(_message_label_obj, "GAME OVER\nPress Center to Restart");
            }
            break;
    }
    lv_obj_clear_flag(_message_label_obj, LV_OBJ_FLAG_HIDDEN);
}

bool PongCard::handleButtonPress(uint8_t button_index) {
    PongGame::GameState current_game_state = _pong_game_instance.getState();

    if (current_game_state == PongGame::GameState::GameOver) {
        // In GameOver state:
        // - Center button is still handled by PongCard (to restart)
        // - Up/Down buttons are NOT handled, allowing CardNavigationStack to use them
        if (button_index == Input::BUTTON_CENTER) {
            return true; // Center press restarts the game (handled in update())
        }
        return false; // Up/Down presses are not handled by PongCard in GameOver
    } else {
        // In other states (StartScreen, Playing, Paused):
        // PongCard handles all three relevant buttons
        if (button_index == Input::BUTTON_CENTER ||
            button_index == Input::BUTTON_UP ||
            button_index == Input::BUTTON_DOWN) {
            return true; // Event handled by PongCard (logic is in update())
        }
    }
    return false; // Other buttons or unhandled cases
}

lv_obj_t* PongCard::getCardObject() const {
    return _card_root_obj;
}
