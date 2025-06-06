#pragma once

#include <FastLED.h>

// Define the data pin for the NeoPixel. 
// This should match PIN_NEOPIXEL from pins_arduino.h for the board (33 for Adafruit ESP32-S3 Reverse TFT)
#ifndef NEOPIXEL_DATA_PIN // Allow override from build flags if needed
  #define NEOPIXEL_DATA_PIN 33 
#endif

class NeoPixelController {
public:
    enum class LedState { IDLE, WORK, BREAK, BLINKING };

    NeoPixelController();
    void begin();
    void update();
    void blinkLight(int times, int delay_ms);
    void setLedState(LedState newState);

private:
    LedState _currentLedState; // Current state of the LED
    LedState _previousLedState; // State before blinking

    // For non-blocking blink
    int _blink_sweeps_todo;
    int _blink_sweeps_done;
    int _blink_color_idx;
    bool _blink_is_on_phase;
    unsigned long _blink_phase_next_change_time;
    int _blink_phase_duration_ms;
    uint8_t _blink_original_brightness;

    static const CRGB BLINK_COLORS[]; 
    static const int NUM_BLINK_COLORS;

    static constexpr uint8_t NUM_PIXELS = 1;
    static constexpr uint16_t UPDATE_INTERVAL_MS = 16;  // ~60fps
    static constexpr float BREATH_SPEED = 0.0167f * .75f ;
    static constexpr float BREATH_CYCLE = 2.0f * PI;
    static constexpr float COLOR_VARIANCE = 0.2f;
    // static constexpr uint8_t DATA_PIN = 33; // Explicitly define data pin for FastLED

    CRGB leds[NUM_PIXELS]; // Changed from Adafruit_NeoPixel pixel;
    unsigned long lastUpdate;
    float breathPhase;
}; 