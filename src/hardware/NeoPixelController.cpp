#include "hardware/NeoPixelController.h"
#include <Arduino.h> // For pinMode, digitalWrite, delay, PI, sin, constrain, millis
// FastLED.h is included via NeoPixelController.h

// Define colors for different states
const CRGB NeoPixelController::BLINK_COLORS[] = {CRGB::Blue, CRGB::DeepPink, CRGB::Orange};
const int NeoPixelController::NUM_BLINK_COLORS = sizeof(NeoPixelController::BLINK_COLORS) / sizeof(CRGB);

const CRGB COLOR_IDLE = CRGB::Black; // Or a dim white e.g., CRGB(10,10,10)
const CRGB COLOR_WORK = CRGB(128, 64, 0); // Dim Orange (adjust R,G,B as needed)
const CRGB COLOR_BREAK = CRGB(0, 0, 128);   // Dim Blue (adjust R,G,B as needed)
const uint8_t NEOPIXEL_BRIGHTNESS = 50; // Overall brightness (0-255)

NeoPixelController::NeoPixelController()
    : _currentLedState(LedState::IDLE),
      _previousLedState(LedState::IDLE),
      lastUpdate(0),
      breathPhase(0.0f),
      // Initialize blink state members
      _blink_sweeps_todo(0),
      _blink_sweeps_done(0),
      _blink_color_idx(0),
      _blink_is_on_phase(false),
      _blink_phase_next_change_time(0),
      _blink_phase_duration_ms(0),
      _blink_original_brightness(NEOPIXEL_BRIGHTNESS) {
}

void NeoPixelController::begin() {
    // Power on the NeoPixel (board-specific, keep this logic)
    #if defined(NEOPIXEL_POWER) && defined(NEOPIXEL_POWER_ON)
      pinMode(NEOPIXEL_POWER, OUTPUT);
      digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
      delay(10);
    #elif defined(NEOPIXEL_POWER)
      pinMode(NEOPIXEL_POWER, OUTPUT);
      digitalWrite(NEOPIXEL_POWER, HIGH);
      delay(10);
    #endif

    // Initialize FastLED
    // Using NEOPIXEL_DATA_PIN from NeoPixelController.h
    FastLED.addLeds<WS2812B, NEOPIXEL_DATA_PIN, GRB>(leds, NUM_PIXELS);
    _blink_original_brightness = NEOPIXEL_BRIGHTNESS; // Store initial brightness for restoration
    FastLED.setBrightness(_blink_original_brightness);
    leds[0] = COLOR_IDLE; // Start with IDLE color
    FastLED.show();
    _currentLedState = LedState::IDLE; // Explicitly set initial state
}

void NeoPixelController::setLedState(LedState newState) {
    // Prevent changing state if currently blinking, unless specifically setting to BLINKING (which blinkLight does)
    if (_currentLedState == LedState::BLINKING && newState != LedState::BLINKING) {
        // If an external call tries to change state away from BLINKING, 
        // we might want to cancel the blink and apply the new state.
        // For now, blinkLight itself handles transition out of BLINKING.
        // This setLedState is for general state changes outside of blink completion.
        _currentLedState = newState; // Allow overriding blink if necessary for other logic
        FastLED.setBrightness(_blink_original_brightness); // Restore brightness if blink is cut short
    } else if (_currentLedState != LedState::BLINKING) {
        _currentLedState = newState;
    }
}

void NeoPixelController::update() {
    unsigned long currentTime = millis();
    // Minimal interval check, mostly for non-blinking states.
    if (_currentLedState != LedState::BLINKING && (currentTime - lastUpdate < UPDATE_INTERVAL_MS)) {
        return;
    }
    // lastUpdate = currentTime; // Only update for steady states, blink handles its own timing.

    if (_currentLedState == LedState::BLINKING) {
        if (currentTime >= _blink_phase_next_change_time) {
            _blink_phase_next_change_time = currentTime + _blink_phase_duration_ms;

            if (_blink_is_on_phase) { // Was ON, turn OFF
                leds[0] = CRGB::Black;
                _blink_is_on_phase = false;
            } else { // Was OFF, turn ON for next color/sweep or finish
                _blink_color_idx++;
                if (_blink_color_idx >= NUM_BLINK_COLORS) {
                    _blink_color_idx = 0;
                    _blink_sweeps_done++;
                }

                if (_blink_sweeps_done >= _blink_sweeps_todo) {
                    // Blinking finished
                    FastLED.setBrightness(_blink_original_brightness);
                    _currentLedState = _previousLedState; // Restore previous state
                    // The switch below will set the color for the new state.
                } else {
                    // Still blinking, set next color for ON phase
                    leds[0] = BLINK_COLORS[_blink_color_idx];
                    _blink_is_on_phase = true;
                }
            }
        }
    }

    // Set color based on current state (could have been changed by blink logic)
    // previousLedState is updated here if not blinking to correctly save state before next blink
    if (_currentLedState != LedState::BLINKING) {
        lastUpdate = currentTime; // Update timestamp for steady state logic
        _previousLedState = _currentLedState; 
        switch (_currentLedState) {
            case LedState::IDLE:
                leds[0] = COLOR_IDLE;
                break;
            case LedState::WORK:
                leds[0] = COLOR_WORK;
                // Optional: Add slow pulsing logic here for WORK state
                // breathPhase += BREATH_SPEED;
                // if (breathPhase >= BREATH_CYCLE) { breathPhase -= BREATH_CYCLE; }
                // float brightness_factor = (sin(breathPhase) + 1.0f) * 0.5f; // 0 to 1
                // leds[0] = COLOR_WORK; // Base color
                // leds[0].nscale8_video( (uint8_t)(128 + brightness_factor * 127) ); // Scale brightness
                break;
            case LedState::BREAK:
                leds[0] = COLOR_BREAK;
                // Optional: Add slow pulsing logic here for BREAK state
                break;
            default: // Should not include BLINKING here as it's handled above
                break;
        }
    }
    FastLED.show();
}

void NeoPixelController::blinkLight(int sweeps, int phase_duration_ms) {
    // If already blinking, and this is a new request, we could choose to ignore, restart, or queue.
    // For simplicity, let's restart if a new blink is requested.
    // if (_currentLedState == LedState::BLINKING) { return; } // Option: ignore if already blinking

    if (_currentLedState != LedState::BLINKING) { // Only save previous state if not already blinking
        _previousLedState = _currentLedState;
    }
    _currentLedState = LedState::BLINKING;

    _blink_sweeps_todo = sweeps;
    _blink_sweeps_done = 0;
    _blink_color_idx = 0;       // Start with the first color
    _blink_is_on_phase = true;  // Start with the light ON
    _blink_phase_duration_ms = phase_duration_ms;
    _blink_phase_next_change_time = millis() + _blink_phase_duration_ms;

    // Store current brightness if not already stored by a previous blinkLight call that was interrupted
    // Or, more simply, _blink_original_brightness is set at begin() and only restored by blink finishing.
    // If a blink interrupts another, the _blink_original_brightness is already the true original.
    if (_previousLedState != LedState::BLINKING) { // Only grab brightness if we weren't already blinking
         _blink_original_brightness = FastLED.getBrightness();
    }
    FastLED.setBrightness(255); // Max brightness for blink

    leds[0] = BLINK_COLORS[_blink_color_idx]; // Set initial color for the first ON phase
    // FastLED.show() will be handled by update()
} 