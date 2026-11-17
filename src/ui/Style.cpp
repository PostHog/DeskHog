#include "Style.h"
#include "esp_heap_caps.h"
#include <Arduino.h>
#include "fonts/fonts.h"

// Initialize static members
const lv_font_t* Style::_label_font = nullptr;
const lv_font_t* Style::_value_font = nullptr;
const lv_font_t* Style::_large_value_font = nullptr;
const lv_font_t* Style::_loud_noises_font = nullptr;
const lv_font_t* Style::_tiny_font = nullptr;
bool Style::_fonts_initialized = false;

void Style::initFonts() {
    if (_fonts_initialized) return;

    Serial.println("Initializing custom fonts...");
    Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    
    // Use the precompiled LVGL fonts
    Serial.println("Loading LVGL-compatible fonts...");
    
    // First font - Regular for labels
    _label_font = &font_label;
    Serial.println("Label font loaded (Regular 15pt)");
    
    Serial.printf("PSRAM after label font: %d bytes\n", ESP.getFreePsram());
    
    // Second font - SemiBold for values
    _value_font = &font_value;
    Serial.println("Value font loaded (SemiBold 16pt)");
    
    Serial.printf("PSRAM after value font: %d bytes\n", ESP.getFreePsram());
    
    // Third font - SemiBold for large values
    _large_value_font = &font_value_large;
    Serial.println("Large value font loaded (SemiBold 36pt)");

    // Fourth font - LoudNoises for decorative text
    _loud_noises_font = &font_loud_noises;
    // Fifth font - small Regular, for dense lists in plugins
    _tiny_font = &font_tiny;
    Serial.println("LoudNoises font loaded (20pt)");

    _fonts_initialized = true;
    Serial.printf("After font init - Free PSRAM: %d bytes\n", ESP.getFreePsram());
    Serial.printf("After font init - Free heap: %d bytes\n", ESP.getFreeHeap());
} 