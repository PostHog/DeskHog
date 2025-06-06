#include <lvgl.h>
#include "ui/PomodoroCard.h"
#include "Style.h"
#include "hardware/NeoPixelController.h"
#include <Arduino.h>
#include <string> // Required for std::string

extern NeoPixelController* neoPixelController;

// Define static members for rainbow colors
const uint32_t PomodoroCard::RAINBOW_COLORS[] = {
    0xFF0000,  // Red
    0xFF7F00,  // Orange
    0xFFFF00,  // Yellow
    0x00FF00,  // Green
    0x0000FF,  // Blue
    0x4B0082,  // Indigo
    0x9400D3   // Violet
};
const size_t PomodoroCard::RAINBOW_COLORS_COUNT = sizeof(PomodoroCard::RAINBOW_COLORS) / sizeof(PomodoroCard::RAINBOW_COLORS[0]);

// Define static members for mode-specific background colors
const lv_color_t PomodoroCard::WORK_BG_COLOR = lv_color_hex(0xE07A5F); // Desaturated Orange/Red (Terracotta/Coral)
const lv_color_t PomodoroCard::BREAK_BG_COLOR = lv_color_hex(0x3A5A7A); // Desaturated Blue

PomodoroCard::PomodoroCard(lv_obj_t* parent)
    : _card(nullptr)
    , _background(nullptr)
    , _label(nullptr)
    , _label_shadow(nullptr)
    , _tally_label(nullptr)
    , _progress_arc(nullptr)
    , _timer(nullptr)
    , _effects_timer(nullptr)
    , _is_running(false)
    , _is_work_mode(true)
    , _remaining_seconds(WORK_TIME)
    , _completed_work_sessions(0)
{
    // Create main card with black background
    _card = lv_obj_create(parent);
    if (!_card) return;
    
    // Set card size and style
    lv_obj_set_width(_card, lv_pct(100));
    lv_obj_set_height(_card, lv_pct(100));
    lv_obj_set_style_bg_color(_card, lv_color_black(), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all(_card, 5, 0);
    lv_obj_set_style_margin_all(_card, 0, 0);
    
    // Create background container with rounded corners
    _background = lv_obj_create(_card);
    if (!_background) return;
    
    // Make background container fill parent completely
    lv_obj_set_style_radius(_background, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_background, WORK_BG_COLOR, 0); // Initial color: work mode
    lv_obj_set_style_border_width(_background, 0, 0);
    lv_obj_set_style_pad_all(_background, 5, 0);
    
    lv_obj_set_width(_background, lv_pct(100));
    lv_obj_set_height(_background, lv_pct(100));
    
    // Create circular progress bar (arc)
    _progress_arc = lv_arc_create(_background);
    if (_progress_arc) {
        lv_obj_set_size(_progress_arc, 95, 95); // Adjust size as needed
        lv_obj_align(_progress_arc, LV_ALIGN_CENTER, 0, 0);
        lv_arc_set_rotation(_progress_arc, 270); // Start at 12 o'clock
        lv_arc_set_bg_angles(_progress_arc, 0, 360);
        lv_arc_set_value(_progress_arc, 100); // Initial value (full)
        lv_obj_remove_style(_progress_arc, NULL, LV_PART_KNOB); // No knob
        lv_obj_set_style_arc_width(_progress_arc, 8, LV_PART_MAIN); // Track width
        lv_obj_set_style_arc_color(_progress_arc, lv_color_hex(0x303030), LV_PART_MAIN); // Track color (darker gray)
        lv_obj_set_style_arc_width(_progress_arc, 8, LV_PART_INDICATOR); // Indicator width
        lv_obj_set_style_arc_color(_progress_arc, lv_color_white(), LV_PART_INDICATOR); // Indicator color (white)
        lv_obj_move_background(_progress_arc); // Ensure it's behind labels
    }
    
    // Create shadow label (black, 1px offset)
    _label_shadow = lv_label_create(_background);
    if (_label_shadow) {
        lv_obj_set_style_text_font(_label_shadow, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(_label_shadow, lv_color_black(), 0);
        lv_obj_align(_label_shadow, LV_ALIGN_CENTER, 0, 1);
    }
    
    // Create main label (white, no shadow offset)
    _label = lv_label_create(_background);
    if (_label) {
        lv_obj_set_style_text_font(_label, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(_label, lv_color_white(), 0);
        lv_obj_align(_label, LV_ALIGN_CENTER, 0, 0);
    }
    
    // Create tally label
    _tally_label = lv_label_create(_card);
    if (_tally_label) {
        lv_obj_set_style_text_font(_tally_label, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(_tally_label, lv_color_white(), 0);
        lv_obj_align(_tally_label, LV_ALIGN_TOP_LEFT, 5, 5);
    }
    
    // Initialize display (including tally)
    updateDisplay();
    updateTallyDisplay();

    if (neoPixelController) {
        neoPixelController->setLedState(NeoPixelController::LedState::IDLE);
    }
}

PomodoroCard::~PomodoroCard() {
    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }
    if (_effects_timer) {
        lv_timer_del(_effects_timer);
        _effects_timer = nullptr;
    }
    
    if (isValidObject(_card)) {
        lv_obj_add_flag(_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(_card);
        
        _card = nullptr;
        _background = nullptr;
        _label = nullptr;
        _label_shadow = nullptr;
        _tally_label = nullptr;
        _progress_arc = nullptr;
    }
}

void PomodoroCard::updateDisplay() {
    if (!_label || !_label_shadow) return;

    if (_is_running) {
        _remaining_seconds--;
        if (_remaining_seconds < 0) { 
            stopTimer(); 
            switchMode();

            Serial.println("Timer expired. Scheduling effects.");
            if (_effects_timer) {
                lv_timer_del(_effects_timer);
                _effects_timer = nullptr;
            }
            _effects_timer = lv_timer_create(effects_timer_cb, 100, this);
            lv_timer_set_repeat_count(_effects_timer, 1);
            
            int minutes_after_switch = _remaining_seconds / 60; 
            int seconds_after_switch = _remaining_seconds % 60;
            char time_str_after_switch[6];
            snprintf(time_str_after_switch, sizeof(time_str_after_switch), "%02d:%02d", minutes_after_switch, seconds_after_switch);
            lv_label_set_text(_label, time_str_after_switch);
            lv_label_set_text(_label_shadow, time_str_after_switch);

            if (_progress_arc) {
                 lv_arc_set_value(_progress_arc, 100);
            }
            return; 
        }
    }
    
    if (_progress_arc) {
        int total_seconds_in_mode = _is_work_mode ? WORK_TIME : BREAK_TIME;
        if (total_seconds_in_mode == 0) total_seconds_in_mode = 1; 

        int16_t progress_percent = (_remaining_seconds >= 0) ? 
                                   (int16_t)((_remaining_seconds / (float)total_seconds_in_mode) * 100) : 0;
        lv_arc_set_value(_progress_arc, progress_percent);
    }
    
    int minutes = _remaining_seconds / 60;
    int seconds = _remaining_seconds % 60;
    char time_str[6];
    snprintf(time_str, sizeof(time_str), "%02d:%02d", minutes, seconds);
    
    lv_label_set_text(_label, time_str);
    lv_label_set_text(_label_shadow, time_str);

    if (!_is_running && _background) {
        lv_color_t current_mode_bg_color = _is_work_mode ? WORK_BG_COLOR : BREAK_BG_COLOR;
        lv_obj_set_style_bg_color(_background, current_mode_bg_color, 0);
    }
}

void PomodoroCard::startTimer() {
    if (_is_running) return;
    
    _is_running = true;
    updateDisplay(); 

    if (neoPixelController) {
        neoPixelController->setLedState(_is_work_mode ? NeoPixelController::LedState::WORK : NeoPixelController::LedState::BREAK);
    }

    if (!_timer) {
        _timer = lv_timer_create([](lv_timer_t* timer) {
            auto* card = static_cast<PomodoroCard*>(lv_timer_get_user_data(timer));
            if (card) card->updateDisplay(); // Add null check for safety
        }, 1000, this);  // Update every second
    }
}

void PomodoroCard::stopTimer() {
    _is_running = false;
    if (_timer) {
        lv_timer_del(_timer);
        _timer = nullptr;
    }
    if (_effects_timer) { 
        lv_timer_del(_effects_timer);
        _effects_timer = nullptr;
    }
    if (neoPixelController) {
        neoPixelController->setLedState(NeoPixelController::LedState::IDLE);
    }
}

// Custom animation callback to set background color from RAINBOW_COLORS array
void PomodoroCard::anim_set_bg_color_cb(void * var, int32_t v) {
    lv_obj_t* obj = static_cast<lv_obj_t*>(var);
    if (obj && v >= 0 && v < static_cast<int32_t>(RAINBOW_COLORS_COUNT)) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(RAINBOW_COLORS[v]), 0);
    }
}

// Animation ready callback to restore original background color
void PomodoroCard::anim_ready_cb_restore_color(lv_anim_t *a) {
    PomodoroCard* card = static_cast<PomodoroCard*>(a->user_data);
    if (card && card->_background) { // Ensure card and its background are valid
        lv_color_t restore_color = card->_is_work_mode ? WORK_BG_COLOR : BREAK_BG_COLOR;
        lv_obj_set_style_bg_color(card->_background, restore_color, 0);
    } else {
        // Fallback: if user_data is not set or card is invalid, try to use a default
        // This might happen if the animation is somehow triggered without proper setup.
        lv_obj_t* obj = static_cast<lv_obj_t*>(a->var); // Get the animated object (background)
        if (obj) {
             // We don't know the mode here, so default to WORK_BG_COLOR or a neutral color.
             // However, with user_data, this path should ideally not be taken.
            lv_obj_set_style_bg_color(obj, WORK_BG_COLOR, 0); 
        }
    }
}

void PomodoroCard::flashRainbow() {
    if (!_background) return; // Ensure _background is valid

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _background);
    lv_anim_set_user_data(&a, this); // Pass PomodoroCard instance
    lv_anim_set_exec_cb(&a, anim_set_bg_color_cb); // Use the custom callback
    lv_anim_set_values(&a, 0, RAINBOW_COLORS_COUNT - 1); // Animate through indices of RAINBOW_COLORS
    lv_anim_set_time(&a, 2000);  // 2 seconds for full rainbow cycle
    lv_anim_set_repeat_count(&a, 2);  // Flash twice
    lv_anim_set_playback_time(&a, 0); // No playback
    lv_anim_set_ready_cb(&a, anim_ready_cb_restore_color); // Set the ready callback
    lv_anim_start(&a);
}

void PomodoroCard::switchMode() {
    if (_is_work_mode) {
        _completed_work_sessions++;
        updateTallyDisplay();
    }
    _is_work_mode = !_is_work_mode;
    _remaining_seconds = _is_work_mode ? WORK_TIME : BREAK_TIME;

    if (_background) { 
        lv_color_t new_bg_color = _is_work_mode ? WORK_BG_COLOR : BREAK_BG_COLOR;
        lv_obj_set_style_bg_color(_background, new_bg_color, 0);
    }

    if (_progress_arc) {
        lv_arc_set_value(_progress_arc, 100);
    }
    updateDisplay();

    if (_is_running && neoPixelController) {
         neoPixelController->setLedState(_is_work_mode ? NeoPixelController::LedState::WORK : NeoPixelController::LedState::BREAK);
    }
}

bool PomodoroCard::handleButtonPress(uint8_t button_index) {
    if (button_index == 1) {  // Center button
        if (_is_running) {
            stopTimer();
        } else {
            startTimer();
        }
        return true;
    }
    return false;
}

bool PomodoroCard::isValidObject(lv_obj_t* obj) const {
    return obj != nullptr && lv_obj_is_valid(obj);
}

void PomodoroCard::effects_timer_cb(lv_timer_t* timer) {
    PomodoroCard* card = static_cast<PomodoroCard*>(lv_timer_get_user_data(timer));
    if (card) {
        // Nullify the pointer as LVGL will delete the one-shot timer automatically
        if (card->_effects_timer == timer) {
             card->_effects_timer = nullptr;
        }
        card->executePostTimerEffects();
    }
}

void PomodoroCard::executePostTimerEffects() {
    Serial.println("Executing post-timer effects. Flashing rainbow (screen).");
    flashRainbow(); // Screen effect
    
    Serial.println("Blinking NeoPixel.");
    if (neoPixelController) {
        neoPixelController->blinkLight(2, 250); // Blink 2 times, 250ms on, 250ms off for each color in sequence
    }

    // Timer will be started for the new mode by the effects_timer_cb after this function returns
    // and startTimer will set the correct WORK/BREAK LED state.
}

void PomodoroCard::updateTallyDisplay() {
    if (!_tally_label) return;

    std::string tally_str = "";
    if (_completed_work_sessions == 0) {
        lv_label_set_text(_tally_label, "");
        return;
    }

    for (int i = 1; i <= _completed_work_sessions; ++i) {
        if (i % 5 == 0) {
            tally_str += "/"; // The 5th mark crosses the previous four implicitly by its position
        } else {
            tally_str += "|";
        }
        // Add a space after a full set of 5, but not if it's the very last mark
        if (i % 5 == 0 && i != _completed_work_sessions) {
             tally_str += " "; 
        }
    }
    lv_label_set_text(_tally_label, tally_str.c_str());
} 