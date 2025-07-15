#ifndef PADDLE_CARD_H
#define PADDLE_CARD_H

#include "lvgl.h"
#include "ui/InputHandler.h" // Corrected base class
#include "game/PaddleGame.h"
#include <cstdint> // For uint8_t

class PaddleCard : public InputHandler {
public:
    PaddleCard(lv_obj_t* parent);
    ~PaddleCard() override; // Marking as override

    bool update() override; // Returns true to keep receiving updates
    bool handleButtonPress(uint8_t button_index) override;
    lv_obj_t* getCard() const; // Matches main's architecture
    void prepareForRemoval() override { markedForRemoval = true; } // Prevent double deletion

private:
    void createUi(lv_obj_t* parent);
    void updateUi(); // Replaces drawGameElements, more general for UI updates
    void updateMessageLabel(); // For displaying game state text

    lv_obj_t* _card_root_obj; // Main LVGL container for the Paddle card
    PaddleGame _paddle_game_instance;

    // LVGL objects for game elements
    lv_obj_t* _player_paddle_obj;
    lv_obj_t* _ai_paddle_obj;
    lv_obj_t* _ball_obj;
    lv_obj_t* _player_score_label_obj;
    lv_obj_t* _ai_score_label_obj;
    lv_obj_t* _message_label_obj;

    char _chosen_victory_phrase_buffer[100]; // Buffer to hold the chosen victory message
    bool _is_victory_phrase_chosen;        // Flag to indicate if a phrase has been chosen for current game over
    PaddleGame::GameState _previous_game_state;  // To detect state transitions
    bool markedForRemoval = false;  // Track if card is being removed

    // Constants
    static constexpr int PADDLE_WIDTH = 5; // Adjusted for visibility
    static constexpr int PADDLE_HEIGHT = 30; // Adjusted
    static constexpr int BALL_DIAMETER = 5; // Adjusted

    // Victory phrases
    static const char* VICTORY_PHRASES[];
    static const int NUM_VICTORY_PHRASES;
};

#endif // PADDLE_CARD_H 