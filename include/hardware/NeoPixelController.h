#pragma once

#include <Arduino.h>
#include <atomic>
#include <FastLED.h>

// Define the data pin for the NeoPixel. 
// This should match PIN_NEOPIXEL from pins_arduino.h for the board (33 for Adafruit ESP32-S3 Reverse TFT)
#ifndef NEOPIXEL_DATA_PIN // Allow override from build flags if needed
  #define NEOPIXEL_DATA_PIN 33 
#endif

class NeoPixelController {
public:
    NeoPixelController();
    void begin();
    void update();

    /**
     * Show a fixed color instead of the breathing animation. Safe to call from any task.
     * @param durationMs How long to hold it; 0 holds it until clearOverride().
     */
    void setOverride(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs);

    /** Go back to the breathing animation. */
    void clearOverride();

private:
    static constexpr uint8_t NUM_PIXELS = 1;
    static constexpr uint16_t UPDATE_INTERVAL_MS = 16;  // ~60fps
    static constexpr float BREATH_SPEED = 0.0167f * .75f ;
    static constexpr float BREATH_CYCLE = 2.0f * PI;
    static constexpr float COLOR_VARIANCE = 0.2f;
    // static constexpr uint8_t DATA_PIN = 33; // Explicitly define data pin for FastLED

    CRGB leds[NUM_PIXELS]; // Changed from Adafruit_NeoPixel pixel;

    static constexpr uint32_t OVERRIDE_ACTIVE = 0x01000000;
    std::atomic<uint32_t> overrideColor{0};  ///< OVERRIDE_ACTIVE | 0xRRGGBB, or 0
    std::atomic<uint32_t> overrideUntil{0};  ///< millis() deadline, or 0 for none
    unsigned long lastUpdate;
    float breathPhase;
}; 