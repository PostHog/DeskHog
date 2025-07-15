#include "ui/examples/HelloWorldCard.h"
#include "Style.h"

HelloWorldCard::HelloWorldCard(lv_obj_t* parent) : _card(nullptr), _label(nullptr) {
    _card = lv_obj_create(parent); // Create the card container
    lv_obj_set_size(_card, LV_PCT(100), LV_PCT(100)); // Make it fill the entire screen
    lv_obj_set_style_bg_color(_card, lv_color_black(), 0); // Set background to black
    
    _label = lv_label_create(_card); // Create a label on the card
    lv_label_set_text(_label, "Hello, world!"); // Set the text content
    lv_obj_set_style_text_color(_label, lv_color_white(), 0); // Set text color to white
    lv_obj_set_style_text_font(_label, Style::largeValueFont(), 0); // Use the large font from Style
    lv_obj_center(_label); // Center the label on the card
}

bool HelloWorldCard::handleButtonPress(uint8_t button_index) {
    // Toggle between black and white backgrounds
    static bool isBlack = true;
    
    if (isBlack) {
        lv_obj_set_style_bg_color(_card, lv_color_white(), 0);
        lv_obj_set_style_text_color(_label, lv_color_black(), 0);
    } else {
        lv_obj_set_style_bg_color(_card, lv_color_black(), 0);
        lv_obj_set_style_text_color(_label, lv_color_white(), 0);
    }
    
    isBlack = !isBlack; // Toggle the state
    
    return true; // We handled the button press
}

HelloWorldCard::~HelloWorldCard() {
    // This function cleans up the card object when it's no longer needed
    // Card deletion is managed by CardNavigationStack
    // If the card happens to still exist, we'll delete it here
    if (_card) {
        lv_obj_del_async(_card);
        _card = nullptr;
    }
}