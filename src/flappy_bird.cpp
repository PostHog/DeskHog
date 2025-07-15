#include "flappy_bird.h"
#include "Arduino.h" // For random, etc.
#include "Style.h"   // For Style::loudNoisesFont()

FlappyBirdGame::FlappyBirdGame()
    : main_container(nullptr), bird_obj(nullptr), 
      start_message_label(nullptr), game_over_message_label(nullptr), score_label(nullptr),
      bird_y(BIRD_SIZE / 2), bird_velocity(0.0f), 
      current_game_state(GameState::PRE_GAME), score(0) {
    Serial.println("[FlappyBird] Constructor called"); // DEBUG
    for (int i = 0; i < PIPE_COUNT; ++i) {
        // pipes[i].top_asterisk_obj = nullptr;     // Old label pointer
        // pipes[i].bottom_asterisk_obj = nullptr;  // Old label pointer
        pipes[i].top_pipe_obj = nullptr;     // Initialize new member
        pipes[i].bottom_pipe_obj = nullptr;  // Initialize new member
        pipes[i].scored = false;
    }
}

FlappyBirdGame::~FlappyBirdGame() {
    Serial.println("[FlappyBird] Destructor called"); // DEBUG
    cleanup();
}

void FlappyBirdGame::setup(lv_obj_t* parent_screen) {
    Serial.println("[FlappyBird] setup() called"); // DEBUG
    current_game_state = GameState::PRE_GAME;
    score = 0;
    Serial.printf("[FlappyBird] Game state set to PRE_GAME, score reset to %d\n", score); // DEBUG

    if (!main_container) { 
        Serial.println("[FlappyBird] Creating main_container"); // DEBUG
        main_container = lv_obj_create(parent_screen);
        lv_obj_remove_style_all(main_container);
        lv_obj_set_size(main_container, FB_SCREEN_WIDTH, FB_SCREEN_HEIGHT);
        // Sunset gradient background
        lv_obj_set_style_bg_grad_dir(main_container, LV_GRAD_DIR_VER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(main_container, lv_color_hex(0xFFA500), LV_PART_MAIN);      // Orange (top)
        lv_obj_set_style_bg_grad_color(main_container, lv_color_hex(0x4B0082), LV_PART_MAIN); // Dark Indigo (bottom)
        lv_obj_set_style_bg_opa(main_container, LV_OPA_COVER, LV_PART_MAIN); // Ensure opaque background
        lv_obj_clear_flag(main_container, LV_OBJ_FLAG_SCROLLABLE);
    }

    if (!bird_obj) { 
        Serial.println("[FlappyBird] Creating bird_obj as square"); // DEBUG
        bird_obj = lv_obj_create(main_container); 
        
        lv_obj_set_size(bird_obj, BIRD_SIZE, BIRD_SIZE);
        lv_obj_set_style_bg_color(bird_obj, lv_color_hex(0xFF4500), LV_PART_MAIN); // Vibrant Orange-Red bird
        lv_obj_set_style_radius(bird_obj, LV_RADIUS_CIRCLE, LV_PART_MAIN); 
        lv_obj_set_style_border_width(bird_obj, 1, LV_PART_MAIN); // 1px border
        lv_obj_set_style_border_color(bird_obj, lv_color_black(), LV_PART_MAIN); // Black border
        
        lv_obj_align(bird_obj, LV_ALIGN_CENTER, BIRD_X_POSITION - FB_SCREEN_WIDTH/2 , 0); 
    }
    bird_y = BIRD_SIZE / 2;
    bird_velocity = 0.0f;
    lv_obj_set_y(bird_obj, bird_y - BIRD_SIZE / 2);
    Serial.printf("[FlappyBird] Bird initialized: logical_bird_y_center=%d, velocity=%.2f\n", bird_y, bird_velocity); // DEBUG

    reset_and_initialize_pipes();

    if (!start_message_label) {
        Serial.println("[FlappyBird] Creating start_message_label"); // DEBUG
        start_message_label = lv_label_create(main_container);
        lv_obj_set_style_text_font(start_message_label, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(start_message_label, lv_color_white(), 0); 
        lv_label_set_text(start_message_label, "Press Center!");
        lv_obj_align(start_message_label, LV_ALIGN_CENTER, 0, 0);
        // Add text shadow
        lv_obj_set_style_text_opa(start_message_label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(start_message_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_x(start_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_y(start_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_opa(start_message_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT); // Semi-transparent shadow
    }
    lv_obj_clear_flag(start_message_label, LV_OBJ_FLAG_HIDDEN);
    Serial.println("[FlappyBird] Start message visible"); // DEBUG

    if (game_over_message_label) { // Ensure shadow style is applied if game_over_message_label already exists
        lv_obj_add_flag(game_over_message_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_opa(game_over_message_label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(game_over_message_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_x(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_y(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_opa(game_over_message_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        Serial.println("[FlappyBird] Game Over message hidden and styled"); // DEBUG
    }

    if (!score_label) {
        Serial.println("[FlappyBird] Creating score_label"); // DEBUG
        score_label = lv_label_create(main_container);
        lv_obj_set_style_text_font(score_label, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(score_label, lv_color_white(), 0); 
        lv_obj_align(score_label, LV_ALIGN_TOP_LEFT, 5, 5);
        // Add text shadow
        lv_obj_set_style_text_opa(score_label, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_color(score_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_x(score_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_ofs_y(score_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_opa(score_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text_fmt(score_label, "Score: %d", score);
    lv_obj_clear_flag(score_label, LV_OBJ_FLAG_HIDDEN); 
}

void FlappyBirdGame::reset_and_initialize_pipes() {
    Serial.println("[FlappyBird] reset_and_initialize_pipes() called"); // DEBUG
    srand(time(NULL)); 
    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i].x_position = FB_SCREEN_WIDTH + (i * HORIZONTAL_SPACING_BETWEEN_PIPES);
        int available_height_for_gap = FB_SCREEN_HEIGHT - (2 * MIN_PIPE_HEIGHT) - PIPE_GAP_HEIGHT;
        if(available_height_for_gap < 0) available_height_for_gap = 0;
        pipes[i].gap_y_top = MIN_PIPE_HEIGHT + (rand() % (available_height_for_gap + 1));
        pipes[i].scored = false;

        // Create Top Pipe Rectangle
        if (!pipes[i].top_pipe_obj) {
            pipes[i].top_pipe_obj = lv_obj_create(main_container);
            lv_obj_remove_style_all(pipes[i].top_pipe_obj); 
            lv_obj_set_style_bg_color(pipes[i].top_pipe_obj, lv_color_hex(0x008000), LV_PART_MAIN); // Green
            lv_obj_set_style_bg_opa(pipes[i].top_pipe_obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(pipes[i].top_pipe_obj, 1, LV_PART_MAIN); // 1px border
            lv_obj_set_style_border_color(pipes[i].top_pipe_obj, lv_color_black(), LV_PART_MAIN); // Black border
        }
        lv_obj_set_size(pipes[i].top_pipe_obj, PIPE_WIDTH, pipes[i].gap_y_top);
        lv_obj_set_pos(pipes[i].top_pipe_obj, (int)pipes[i].x_position, 0);

        // Create Bottom Pipe Rectangle
        if (!pipes[i].bottom_pipe_obj) {
            pipes[i].bottom_pipe_obj = lv_obj_create(main_container);
            lv_obj_remove_style_all(pipes[i].bottom_pipe_obj);
            lv_obj_set_style_bg_color(pipes[i].bottom_pipe_obj, lv_color_hex(0x008000), LV_PART_MAIN); // Green
            lv_obj_set_style_bg_opa(pipes[i].bottom_pipe_obj, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_width(pipes[i].bottom_pipe_obj, 1, LV_PART_MAIN); // 1px border
            lv_obj_set_style_border_color(pipes[i].bottom_pipe_obj, lv_color_black(), LV_PART_MAIN); // Black border
        }
        int bottom_pipe_y = pipes[i].gap_y_top + PIPE_GAP_HEIGHT;
        int bottom_pipe_height = FB_SCREEN_HEIGHT - bottom_pipe_y;
        lv_obj_set_size(pipes[i].bottom_pipe_obj, PIPE_WIDTH, bottom_pipe_height);
        lv_obj_set_pos(pipes[i].bottom_pipe_obj, (int)pipes[i].x_position, bottom_pipe_y);
    }
}

void FlappyBirdGame::loop() {
    // Serial.printf("[FlappyBird] loop(), state: %d\n", (int)current_game_state); // DEBUG - Very noisy
    switch (current_game_state) {
        case GameState::PRE_GAME:
            if (Input::isCenterPressed()) {
                Serial.println("[FlappyBird] Center pressed in PRE_GAME - starting game"); // DEBUG
                current_game_state = GameState::ACTIVE;
                if (start_message_label) {
                    lv_obj_add_flag(start_message_label, LV_OBJ_FLAG_HIDDEN);
                }
                bird_y = BIRD_SIZE / 2;
                bird_velocity = 0.0f;
            }
            break;

        case GameState::ACTIVE:
            handle_input();      
            update_game_state(); 
            render();            
            break;

        case GameState::GAME_OVER:
            if (Input::isCenterPressed()) {
                Serial.println("[FlappyBird] Center pressed in GAME_OVER - restarting to PRE_GAME"); // DEBUG
                setup(lv_obj_get_parent(main_container)); 
            }
            break;
    }
}

void FlappyBirdGame::handle_input() {
    if (current_game_state != GameState::ACTIVE) return;
    if (Input::isCenterPressed()) {
        Serial.println("[FlappyBird] Flap! (Center pressed in ACTIVE state)"); // DEBUG
        bird_velocity = -1.59375f;
    }
}

void FlappyBirdGame::update_game_state() {
    if (current_game_state != GameState::ACTIVE) return;

    bird_velocity += 0.0204f;
    bird_y += (int)bird_velocity;

    const int Y_VISUAL_OFFSET = (FB_SCREEN_HEIGHT / 2) - (BIRD_SIZE / 2);

    // Calculate visual bird edges
    int visual_bird_center_y = bird_y + Y_VISUAL_OFFSET;
    int visual_bird_top_edge = visual_bird_center_y - BIRD_SIZE / 2;
    int visual_bird_bottom_edge = visual_bird_center_y + BIRD_SIZE / 2;

    // Clamp bird's visual position to prevent going off the top of the screen before collision check
    if (visual_bird_top_edge < 0) {
        bird_y = -Y_VISUAL_OFFSET + (BIRD_SIZE / 2); // Adjust logical bird_y so visual top is 0
        bird_velocity = 0;      
        Serial.println("[FlappyBird] Bird visually clamped to top screen edge.");
        // Recalculate visual edges after clamping for immediate collision check integrity
        visual_bird_center_y = bird_y + Y_VISUAL_OFFSET;
        visual_bird_top_edge = visual_bird_center_y - BIRD_SIZE / 2;
        visual_bird_bottom_edge = visual_bird_center_y + BIRD_SIZE / 2;
    }

    // --- Explicit Boundary Collision Checks (using visual coordinates) ---

    // Check Top Boundary
    if (visual_bird_top_edge <= 0) {
        Serial.printf("[FlappyBird] COLLISION TYPE: TOP BOUNDARY! visual_top_edge=%d <= 0. Game Over.\n", visual_bird_top_edge);
        current_game_state = GameState::GAME_OVER;
        if (!game_over_message_label) {
            game_over_message_label = lv_label_create(main_container);
            lv_obj_set_style_text_font(game_over_message_label, Style::loudNoisesFont(), 0);
            lv_obj_set_style_text_color(game_over_message_label, lv_color_white(), 0);
            lv_label_set_text(game_over_message_label, "Game Over!");
            lv_obj_align(game_over_message_label, LV_ALIGN_CENTER, 0, 0); 
            // Add text shadow
            lv_obj_set_style_text_opa(game_over_message_label, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_color(game_over_message_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_ofs_x(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_ofs_y(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_opa(game_over_message_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_obj_clear_flag(game_over_message_label, LV_OBJ_FLAG_HIDDEN);
        if (start_message_label) lv_obj_add_flag(start_message_label, LV_OBJ_FLAG_HIDDEN);
        return; 
    }

    // Check Bottom Boundary
    if (visual_bird_bottom_edge >= FB_SCREEN_HEIGHT) {
        Serial.printf("[FlappyBird] COLLISION TYPE: BOTTOM BOUNDARY! visual_bottom_edge=%d >= %d. Game Over.\n", visual_bird_bottom_edge, FB_SCREEN_HEIGHT);
        current_game_state = GameState::GAME_OVER;
         if (!game_over_message_label) {
            game_over_message_label = lv_label_create(main_container);
            lv_obj_set_style_text_font(game_over_message_label, Style::loudNoisesFont(), 0);
            lv_obj_set_style_text_color(game_over_message_label, lv_color_white(), 0);
            lv_label_set_text(game_over_message_label, "Game Over!");
            lv_obj_align(game_over_message_label, LV_ALIGN_CENTER, 0, 0); 
            // Add text shadow
            lv_obj_set_style_text_opa(game_over_message_label, LV_OPA_COVER, 0);
            lv_obj_set_style_shadow_color(game_over_message_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_ofs_x(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_ofs_y(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_opa(game_over_message_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_obj_clear_flag(game_over_message_label, LV_OBJ_FLAG_HIDDEN);
        if (start_message_label) lv_obj_add_flag(start_message_label, LV_OBJ_FLAG_HIDDEN);
        return; 
    }

    // --- Pipe Collision Checks (using visual bird coordinates against visual pipe coordinates) ---
    for (int i = 0; i < PIPE_COUNT; ++i) {
        pipes[i].x_position -= PIPE_MOVE_SPEED;
        
        int bird_left = BIRD_X_POSITION - BIRD_SIZE / 2; 
        int bird_right = BIRD_X_POSITION + BIRD_SIZE / 2;
        
        int pipe_left = (int)pipes[i].x_position;
        int pipe_right = (int)pipes[i].x_position + PIPE_WIDTH;
        int top_pipe_visual_bottom_edge = pipes[i].gap_y_top; 
        int bottom_pipe_visual_top_edge = pipes[i].gap_y_top + PIPE_GAP_HEIGHT; 

        if (bird_right > pipe_left && bird_left < pipe_right) {
            if (visual_bird_top_edge < top_pipe_visual_bottom_edge || visual_bird_bottom_edge > bottom_pipe_visual_top_edge) {
                Serial.printf("[FlappyBird] COLLISION TYPE: PIPE %d! VisualBird(T:%d, B:%d) vs PipeGap(T:%d, B:%d). Game Over.\n", 
                              i, visual_bird_top_edge, visual_bird_bottom_edge, top_pipe_visual_bottom_edge, bottom_pipe_visual_top_edge); 
                current_game_state = GameState::GAME_OVER;
                 if (!game_over_message_label) {
                    game_over_message_label = lv_label_create(main_container);
                    lv_obj_set_style_text_font(game_over_message_label, Style::loudNoisesFont(), 0);
                    lv_obj_set_style_text_color(game_over_message_label, lv_color_white(), 0);
                    lv_label_set_text(game_over_message_label, "Game Over!");
                    lv_obj_align(game_over_message_label, LV_ALIGN_CENTER, 0, 0); 
                    // Add text shadow
                    lv_obj_set_style_text_opa(game_over_message_label, LV_OPA_COVER, 0);
                    lv_obj_set_style_shadow_color(game_over_message_label, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_shadow_ofs_x(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_shadow_ofs_y(game_over_message_label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_shadow_opa(game_over_message_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                lv_obj_clear_flag(game_over_message_label, LV_OBJ_FLAG_HIDDEN);
                if (start_message_label) lv_obj_add_flag(start_message_label, LV_OBJ_FLAG_HIDDEN);
                return; 
            }
        }

        if (!pipes[i].scored && bird_left > pipe_right) {
            score++;
            pipes[i].scored = true;
            Serial.printf("[FlappyBird] Pipe %d scored! Score: %d\n", i, score); 
        }

        if (pipes[i].x_position + PIPE_WIDTH < 0) {
            float max_x = 0;
            for(int j=0; j < PIPE_COUNT; ++j) {
                if (j == i) continue;
                if (pipes[j].x_position > max_x) max_x = pipes[j].x_position;
            }
            pipes[i].x_position = max_x + HORIZONTAL_SPACING_BETWEEN_PIPES;

            int available_height_for_gap = FB_SCREEN_HEIGHT - (2 * MIN_PIPE_HEIGHT) - PIPE_GAP_HEIGHT;
            if(available_height_for_gap < 0) available_height_for_gap = 0;
            pipes[i].gap_y_top = MIN_PIPE_HEIGHT + (rand() % (available_height_for_gap + 1));
            pipes[i].scored = false;

            if (pipes[i].top_pipe_obj) {
                 lv_obj_set_size(pipes[i].top_pipe_obj, PIPE_WIDTH, pipes[i].gap_y_top);
                 lv_obj_set_pos(pipes[i].top_pipe_obj, (int)pipes[i].x_position, 0); 
            }
             if (pipes[i].bottom_pipe_obj) {
                 int bottom_pipe_y = pipes[i].gap_y_top + PIPE_GAP_HEIGHT;
                 int bottom_pipe_height = FB_SCREEN_HEIGHT - bottom_pipe_y;
                 lv_obj_set_size(pipes[i].bottom_pipe_obj, PIPE_WIDTH, bottom_pipe_height);
                 lv_obj_set_pos(pipes[i].bottom_pipe_obj, (int)pipes[i].x_position, bottom_pipe_y);
             }
        }
    }
}

void FlappyBirdGame::render() {
    // Serial.println("[FlappyBird] render() called"); // DEBUG - Very noisy
    if (bird_obj) {
        lv_obj_set_y(bird_obj, bird_y - BIRD_SIZE / 2);
    }

    for (int i = 0; i < PIPE_COUNT; ++i) {
        if (pipes[i].top_pipe_obj) {
            lv_obj_set_x(pipes[i].top_pipe_obj, (int)pipes[i].x_position);
        }
        if (pipes[i].bottom_pipe_obj) {
            lv_obj_set_x(pipes[i].bottom_pipe_obj, (int)pipes[i].x_position);
        }
    }

    if (score_label) {
        lv_label_set_text_fmt(score_label, "Score: %d", score);
    }
}

void FlappyBirdGame::cleanup() {
    Serial.println("[FlappyBird] cleanup() called"); // DEBUG
    if (main_container) {
        lv_obj_del(main_container); // This deletes children too (bird, pipes, labels)
        main_container = nullptr;
        bird_obj = nullptr; 
        start_message_label = nullptr;
        game_over_message_label = nullptr;
        score_label = nullptr;
        for (int i = 0; i < PIPE_COUNT; ++i) {
            // pipes[i].top_asterisk_obj = nullptr;     // Old
            // pipes[i].bottom_asterisk_obj = nullptr;  // Old
            pipes[i].top_pipe_obj = nullptr;     // Null out new members
            pipes[i].bottom_pipe_obj = nullptr;  // Null out new members
        }
    }
}

lv_obj_t* FlappyBirdGame::get_main_container() {
    return main_container;
} 