#include "game/PaddleGame.h"
#include <cstdlib> // For abs(), potentially rand() later if needed
#include "Arduino.h" // For millis()

PaddleGame::PaddleGame(int16_t play_area_width, int16_t play_area_height) : 
    _current_state(GameState::StartScreen),
    _player_score(0),
    _ai_score(0),
    _play_area_width(play_area_width),
    _play_area_height(play_area_height),
    _player_paddle_velocity_y(0),
    _serve_delay_start_time(0),
    _player_serves_next(false) {
    reset();
}

PaddleGame::~PaddleGame() {
    // Destructor
}

void PaddleGame::reset() {
    _player_score = 0;
    _ai_score = 0;

    // Player paddle (left side, centered vertically)
    _player_paddle_coords.x = 5;
    _player_paddle_coords.y = (_play_area_height / 2) - (PADDLE_HEIGHT / 2);
    _player_paddle_velocity_y = 0;

    // AI paddle (right side, centered vertically)
    _ai_paddle_coords.x = _play_area_width - (PADDLE_WIDTH * 2);
    _ai_paddle_coords.y = (_play_area_height / 2) - (PADDLE_HEIGHT / 2);

    // Determine who serves. For a full game reset, player serves.
    // This value might be immediately used if going into ServeDelay.
    _player_serves_next = false; // AI serves first
    resetBall(_player_serves_next); // This positions ball and sets its velocity to 0,0
                                  // and stores who will serve.

    // Transition to ServeDelay state and record start time.
    // This is the key change for ensuring consistent serve behavior.
    setState(GameState::ServeDelay);
    _serve_delay_start_time = millis();
}

void PaddleGame::resetBall(bool player_will_serve) {
    _ball_coords.x = _play_area_width / 2 - BALL_DIAMETER / 2;
    _ball_coords.y = _play_area_height / 2 - BALL_DIAMETER / 2;

    // Velocities are set to 0 here. Actual serving velocity is applied after ServeDelay.
    _ball_velocity.x = 0;
    _ball_velocity.y = 0;
    
    _player_serves_next = player_will_serve; // Store who will serve
}

PaddleGame::GameState PaddleGame::getState() const {
    return _current_state;
}

void PaddleGame::setState(GameState state) {
    _current_state = state;
}

int PaddleGame::getPlayerScore() const {
    return _player_score;
}

int PaddleGame::getAiScore() const {
    return _ai_score;
}

Coordinates PaddleGame::getPlayerPaddleCoordinates() const {
    return _player_paddle_coords;
}

Coordinates PaddleGame::getAiPaddleCoordinates() const {
    return _ai_paddle_coords;
}

Coordinates PaddleGame::getBallCoordinates() const {
    return _ball_coords;
}

void PaddleGame::movePlayerPaddle(bool move_up, bool start_moving) {
    if (start_moving) {
        if (move_up) {
            _player_paddle_velocity_y = -INITIAL_PADDLE_SPEED;
        } else {
            _player_paddle_velocity_y = INITIAL_PADDLE_SPEED;
        }
    } else {
        _player_paddle_velocity_y = 0;
    }
}

void PaddleGame::update() {
    // Handle ServeDelay state: wait for delay then serve ball
    if (_current_state == GameState::ServeDelay) {
        if (millis() - _serve_delay_start_time >= SERVE_DELAY_DURATION_MS) {
            if (_player_serves_next) {
                _ball_velocity.x = INITIAL_BALL_SPEED_X;
            } else {
                _ball_velocity.x = -INITIAL_BALL_SPEED_X;
            }
            
            // Set initial Y velocity
            _ball_velocity.y = INITIAL_BALL_SPEED_Y / 2; // Will be 1 if INITIAL_BALL_SPEED_Y is 2
            if (rand() % 2 == 0) { // Randomize Y direction
                _ball_velocity.y *= -1;
            }
            // Failsafe: if INITIAL_BALL_SPEED_Y was 0 or 1, ensure ball moves vertically.
            if (_ball_velocity.y == 0 && INITIAL_BALL_SPEED_Y >= 0) { 
                _ball_velocity.y = 1; 
            }

            setState(GameState::Playing);
        }
        return; // Don't do other updates during ServeDelay beyond checking time
    }

    if (_current_state != GameState::Playing) {
        return; // Do nothing if not in Playing, Paused, StartScreen, GameOver
    }

    // --- Player Paddle Movement ---
    _player_paddle_coords.y += _player_paddle_velocity_y;

    // Boundary checks for player paddle
    if (_player_paddle_coords.y < 0) {
        _player_paddle_coords.y = 0;
    }
    if (_player_paddle_coords.y > _play_area_height - PADDLE_HEIGHT) {
        _player_paddle_coords.y = _play_area_height - PADDLE_HEIGHT;
    }

    // --- Ball Movement ---
    _ball_coords.x += _ball_velocity.x;
    _ball_coords.y += _ball_velocity.y;

    // --- Basic Ball Collision with Top/Bottom Walls ---
    if (_ball_coords.y <= 0) {
        _ball_coords.y = 0;
        _ball_velocity.y = -_ball_velocity.y; // Reverse Y direction
    } else if (_ball_coords.y >= _play_area_height - BALL_DIAMETER) {
        _ball_coords.y = _play_area_height - BALL_DIAMETER;
        _ball_velocity.y = -_ball_velocity.y; // Reverse Y direction
    }

    // --- Scoring (Ball goes off Left/Right Edges) ---
    // Player scores if ball goes past AI (right edge)
    if (_ball_coords.x >= _play_area_width - BALL_DIAMETER) {
        _player_score++;
        if (_player_score >= 5) { // Example win condition
             setState(GameState::GameOver);
        } else {
            _player_serves_next = false; // AI serves next
            resetBall(_player_serves_next);
            setState(GameState::ServeDelay);
            _serve_delay_start_time = millis();
        }
    }
    // AI scores if ball goes past Player (left edge)
    else if (_ball_coords.x <= 0) {
        _ai_score++;
        if (_ai_score >= 5) { // Example win condition
            setState(GameState::GameOver);
        } else {
            _player_serves_next = false; // AI serves next
            resetBall(_player_serves_next);
            setState(GameState::ServeDelay);
            _serve_delay_start_time = millis();
        }
    }

    // --- AI Paddle Movement (Simple: try to follow the ball) ---
    // A more sophisticated AI would predict ball movement, have reaction times, etc.
    int16_t ai_paddle_center = _ai_paddle_coords.y + PADDLE_HEIGHT / 2;
    int16_t ball_center = _ball_coords.y + BALL_DIAMETER / 2;

    // AI only moves if the ball is significantly off-center
    if (ball_center > ai_paddle_center + AI_REACTION_THRESHOLD) {
        _ai_paddle_coords.y += AI_PADDLE_SPEED; // Move down
    } else if (ball_center < ai_paddle_center - AI_REACTION_THRESHOLD) {
        _ai_paddle_coords.y -= AI_PADDLE_SPEED; // Move up
    }

    // Boundary checks for AI paddle
    if (_ai_paddle_coords.y < 0) {
        _ai_paddle_coords.y = 0;
    }
    if (_ai_paddle_coords.y > _play_area_height - PADDLE_HEIGHT) {
        _ai_paddle_coords.y = _play_area_height - PADDLE_HEIGHT;
    }


    // --- Ball Collision with Paddles (Simplified AABB check) ---
    // Player Paddle Collision
    bool player_collided = 
        _ball_coords.x < _player_paddle_coords.x + PADDLE_WIDTH &&
        _ball_coords.x + BALL_DIAMETER > _player_paddle_coords.x &&
        _ball_coords.y < _player_paddle_coords.y + PADDLE_HEIGHT &&
        _ball_coords.y + BALL_DIAMETER > _player_paddle_coords.y;

    if (player_collided && _ball_velocity.x < 0) { // Check if ball is moving towards player
        _ball_coords.x = _player_paddle_coords.x + PADDLE_WIDTH; // Nudge ball out of paddle
        _ball_velocity.x = -_ball_velocity.x; // Reverse X direction
        // Optional: Modify Y velocity based on where it hit the paddle
        // int16_t hit_pos = (_ball_coords.y + BALL_DIAMETER / 2) - (_player_paddle_coords.y + PADDLE_HEIGHT / 2);
        // _ball_velocity.y = hit_pos / (PADDLE_HEIGHT / 4); // Example: more Y speed if hit near edge
    }

    // AI Paddle Collision
    bool ai_collided = 
        _ball_coords.x < _ai_paddle_coords.x + PADDLE_WIDTH &&
        _ball_coords.x + BALL_DIAMETER > _ai_paddle_coords.x &&
        _ball_coords.y < _ai_paddle_coords.y + PADDLE_HEIGHT &&
        _ball_coords.y + BALL_DIAMETER > _ai_paddle_coords.y;

    if (ai_collided && _ball_velocity.x > 0) { // Check if ball is moving towards AI
        _ball_coords.x = _ai_paddle_coords.x - BALL_DIAMETER; // Nudge ball out
        _ball_velocity.x = -_ball_velocity.x; // Reverse X direction
    }
}

PaddleGame::PlayerWinState PaddleGame::getPlayerWinState() const {
    if (_current_state == GameState::GameOver) {
        if (_player_score >= 5) { // Assuming 5 is the score to win
            return PLAYER_WON;
        } else if (_ai_score >= 5) {
            return AI_WON;
        }
    }
    return GAME_NOT_OVER; // Default if game isn't over or scores are unusual at game over
} 