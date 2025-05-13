#include "ui/ClockCard.h"
#include "Style.h"
#include "fonts.h"
#include <time.h> // For time functions

ClockCard::ClockCard(lv_obj_t* parent)
    : _card(nullptr),
      _timeLabel(nullptr),
      _dayBackgroundColor(lv_color_hex(0x87CEEB)),   // Sky Blue
      _nightBackgroundColor(lv_color_hex(0x131862))  // Dark Blue
{

    _card = lv_obj_create(parent);
    if (!_card) return;

    // Style the card (e.g., black background, full size)
    lv_obj_set_size(_card, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_card, lv_color_black(), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all(_card, 5, 0);
    lv_obj_align(_card, LV_ALIGN_CENTER, 0, 0);

    // Create the time label
    _timeLabel = lv_label_create(_card);
    if (!_timeLabel) return;

    // Style the time label
    lv_obj_set_style_text_font(_timeLabel, Style::largeValueFont(), 0);
    lv_obj_set_style_text_color(_timeLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(_timeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(_timeLabel, LV_LABEL_LONG_WRAP);
    lv_obj_align(_timeLabel, LV_ALIGN_CENTER, 0, 0);

    // Initial text before time sync
    lv_label_set_text(_timeLabel, "--:--");

    // Set initial background to day color, will be updated with first time update
    // Or, determine current hour here if possible and set initial color more accurately.
    // For now, let's assume it will be quickly updated.
    if (_card) {
        updateBackgroundColor(12); // Default to a daytime hour for initial paint
    }
}

ClockCard::~ClockCard() {
    if (isValidObject(_card)) {
        lv_obj_add_flag(_card, LV_OBJ_FLAG_HIDDEN); // Hide before deleting
        lv_obj_del_async(_card);
        _card = nullptr;
        _timeLabel = nullptr; // Label is a child, will be deleted with card
    }
}

lv_obj_t* ClockCard::getCard() {
    return _card;
}

void ClockCard::updateTime(const String& newTime) {
    if (!_timeLabel) return;
    lv_label_set_text(_timeLabel, newTime.c_str());

    // Parse hour from newTime (assuming HH:MM format)
    if (newTime.length() >= 2 && newTime.indexOf(':') > 0) {
        String hourStr = newTime.substring(0, newTime.indexOf(':'));
        int currentHour = hourStr.toInt();
        updateBackgroundColor(currentHour);
    } else if (newTime == "--:--") {
        // Default to a neutral/day color if time is not yet synced
        updateBackgroundColor(12); // Assume midday if no time
    }
}

bool ClockCard::handleButtonPress(uint8_t button_index) {
    // Clock card does not respond to button presses
    return false;
}

bool ClockCard::isValidObject(lv_obj_t* obj) const {
    return obj && lv_obj_is_valid(obj);
}

// Helper function to update background color based on hour
void ClockCard::updateBackgroundColor(int currentHour) {
    if (!_card) return;

    // Define day as 6 AM (6) to 5 PM (17)
    // Adjust these hours as needed
    const int dayStartHour = 6;
    const int nightStartHour = 18; // Or dayEndHour = 17, then else is night

    if (currentHour >= dayStartHour && currentHour < nightStartHour) {
        lv_obj_set_style_bg_color(_card, _dayBackgroundColor, 0);
    } else {
        lv_obj_set_style_bg_color(_card, _nightBackgroundColor, 0);
    }
    // It might be good to invalidate the card to ensure it redraws, if LVGL doesn't do it automatically on style change.
    // lv_obj_invalidate(_card); 
} 