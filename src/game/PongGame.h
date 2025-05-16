#ifndef PONG_GAME_H
#define PONG_GAME_H

#include <cstdint> // For int16_t etc.

// A simple struct for 2D coordinates
struct Coordinates {
    int16_t x;
    int16_t y;
};

class PongGame {
public:
    PongGame(int16_t play_area_width, int16_t play_area_height);
    ~PongGame();

    void update();
    void reset();

    // Placeholder for game states
    enum class GameState {
        StartScreen,
        Playing,
        Paused,
        ServeDelay, // New state for delay before serve
        GameOver
    };

    // Enum to indicate win state when game is over
    enum PlayerWinState {
        PLAYER_WON,
        AI_WON,
        GAME_NOT_OVER // Should not happen if getState() is GameOver, but good for completeness
    };

    GameState getState() const;
    PlayerWinState getPlayerWinState() const;
    void setState(GameState state);

    // Placeholder for paddle/ball positions and scores
    // These will be fleshed out later
    int getPlayerScore() const;
    int getAiScore() const;

    Coordinates getPlayerPaddleCoordinates() const;
    Coordinates getAiPaddleCoordinates() const;
    Coordinates getBallCoordinates() const;

    // Methods to control player paddle based on input
    void movePlayerPaddle(bool move_up, bool start_moving);

    // Constants for game elements, used for physics and boundaries
    // These should ideally match the UI rendering dimensions from PongCard.h
    static constexpr int16_t PADDLE_WIDTH = 5;  // Was 4
    static constexpr int16_t PADDLE_HEIGHT = 30; // Was 25
    static constexpr int16_t BALL_DIAMETER = 5;  // Was 4

private:
    void resetBall(bool player_will_serve); // Renamed parameter for clarity

    GameState _current_state;
    int _player_score;
    int _ai_score;

    uint32_t _serve_delay_start_time; // When the serve delay began
    bool _player_serves_next;       // True if player serves after delay, false if AI

    int16_t _play_area_width;
    int16_t _play_area_height;

    Coordinates _player_paddle_coords;
    int16_t _player_paddle_velocity_y; // Current speed and direction of player paddle

    Coordinates _ai_paddle_coords;
    // AI paddle movement will be calculated in update()

    Coordinates _ball_coords;
    Coordinates _ball_velocity; // x and y components of ball's velocity

    // Speeds (pixels per update cycle, can be tuned)
    static constexpr int16_t INITIAL_PADDLE_SPEED = 3;
    static constexpr int16_t AI_PADDLE_SPEED = 1;          // AI moves slowly
    static constexpr int16_t AI_REACTION_THRESHOLD = 20; // Massively increased. AI reacts extremely late.
    static constexpr int16_t INITIAL_BALL_SPEED_X = 1;
    static constexpr int16_t INITIAL_BALL_SPEED_Y = 2;
    static constexpr uint32_t SERVE_DELAY_DURATION_MS = 1500; // 1.5 second delay before serve
};

#endif // PONG_GAME_H 