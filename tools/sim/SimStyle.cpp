// Desktop stand-in for src/ui/Style.cpp, which logs through Arduino's Serial.
// It hands out the same fonts the device uses.

#include "Style.h"

const lv_font_t* Style::_label_font = nullptr;
const lv_font_t* Style::_value_font = nullptr;
const lv_font_t* Style::_large_value_font = nullptr;
const lv_font_t* Style::_loud_noises_font = nullptr;
const lv_font_t* Style::_tiny_font = nullptr;
bool Style::_fonts_initialized = false;

void Style::initFonts() {
    if (_fonts_initialized) return;
    _label_font = &font_label;
    _value_font = &font_value;
    _large_value_font = &font_value_large;
    _loud_noises_font = &font_loud_noises;
    _tiny_font = &font_tiny;
    _fonts_initialized = true;
}
