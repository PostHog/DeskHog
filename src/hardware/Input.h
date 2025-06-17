#pragma once

#include <Arduino.h>
#include <Bounce2.h>

// Forward declare the external buttons array
extern Bounce2::Button buttons[];

class Input {
public:
    static const uint8_t BUTTON_DOWN = 0;    // BOOT button - pulled HIGH by default, LOW when pressed
    static const uint8_t BUTTON_CENTER = 1;  //pulled LOW by default, HIGH when pressed
    static const uint8_t BUTTON_UP = 2;      //pulled LOW by default, HIGH when pressed

    static void configureButtons() {
        // BOOT button has built-in pullup, active LOW
        buttons[BUTTON_DOWN].attach(BUTTON_DOWN, INPUT_PULLUP);
        buttons[BUTTON_DOWN].interval(5);
        buttons[BUTTON_DOWN].setPressedState(LOW);
        
        // Configure CENTER and UP buttons with pulldown, active HIGH
        buttons[BUTTON_CENTER].attach(BUTTON_CENTER, INPUT_PULLDOWN);
        buttons[BUTTON_CENTER].interval(5);
        buttons[BUTTON_CENTER].setPressedState(HIGH);
        
        buttons[BUTTON_UP].attach(BUTTON_UP, INPUT_PULLDOWN);
        buttons[BUTTON_UP].interval(5);
        buttons[BUTTON_UP].setPressedState(HIGH);
    }

    static void update() {
        buttons[BUTTON_DOWN].update();
        buttons[BUTTON_CENTER].update();
        buttons[BUTTON_UP].update();
        
        // Debug logging for button states
        static bool last_up_state = false;
        static bool last_center_state = false;
        static bool last_down_state = false;
        
        bool current_up_state = buttons[BUTTON_UP].pressed();
        bool current_center_state = buttons[BUTTON_CENTER].pressed();
        bool current_down_state = buttons[BUTTON_DOWN].pressed();
        
        if (current_up_state != last_up_state) {
            Serial.printf("[Input] UP button state changed: %d\n", current_up_state);
            last_up_state = current_up_state;
        }
        if (current_center_state != last_center_state) {
            Serial.printf("[Input] CENTER button state changed: %d\n", current_center_state);
            last_center_state = current_center_state;
        }
        if (current_down_state != last_down_state) {
            Serial.printf("[Input] DOWN button state changed: %d\n", current_down_state);
            last_down_state = current_down_state;
        }
    }

    static bool isDownPressed() { return buttons[BUTTON_DOWN].pressed(); }
    static bool isCenterPressed() { return buttons[BUTTON_CENTER].pressed(); }
    static bool isUpPressed() { return buttons[BUTTON_UP].pressed(); }

    static bool isDownReleased() { return buttons[BUTTON_DOWN].released(); }
    static bool isCenterReleased() { return buttons[BUTTON_CENTER].released(); }
    static bool isUpReleased() { return buttons[BUTTON_UP].released(); }
};

