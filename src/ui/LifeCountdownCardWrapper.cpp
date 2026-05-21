#include "LifeCountdownCardWrapper.h"
#include <Arduino.h>

LifeCountdownCardWrapper::LifeCountdownCardWrapper(lv_obj_t* parent) {
    markedForRemoval = false;
    
    // Create the card container
    cardContainer = lv_obj_create(parent);
    lv_obj_set_size(cardContainer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cardContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cardContainer, 0, 0);
    lv_obj_clear_flag(cardContainer, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create the actual Life Countdown Card
    lifecycleCard = new LifeCountdownCard();
    lifecycleCard->init();
    
    // Create LVGL labels for display
    // Large number in center
    labelNumber = lv_label_create(cardContainer);
    lv_obj_set_style_text_font(labelNumber, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(labelNumber, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(labelNumber, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(labelNumber, "---");
    
    // Unit label below number
    labelUnit = lv_label_create(cardContainer);
    lv_obj_set_style_text_font(labelUnit, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(labelUnit, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(labelUnit, LV_ALIGN_CENTER, 0, 30);
    lv_label_set_text(labelUnit, "hours");
    
    // Context text at bottom
    labelContext = lv_label_create(cardContainer);
    lv_obj_set_style_text_font(labelContext, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(labelContext, lv_color_hex(0x888888), 0);
    lv_obj_align(labelContext, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_label_set_text(labelContext, "until 5pm");
    
    // Mode indicator in bottom right
    labelMode = lv_label_create(cardContainer);
    lv_obj_set_style_text_font(labelMode, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(labelMode, lv_color_hex(0x666666), 0);
    lv_obj_align(labelMode, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_label_set_text(labelMode, "1/7");
    
    Serial.println("LifeCountdownCard initialized");
}

LifeCountdownCardWrapper::~LifeCountdownCardWrapper() {
    if (lifecycleCard) {
        delete lifecycleCard;
        lifecycleCard = nullptr;
    }
    
    if (!markedForRemoval && cardContainer) {
        lv_obj_del(cardContainer);
        cardContainer = nullptr;
    }
}

lv_obj_t* LifeCountdownCardWrapper::getCard() {
    return cardContainer;
}

bool LifeCountdownCardWrapper::handleButtonPress(uint8_t button_index) {
    // Center button (index 1) cycles through modes
    if (button_index == 1) {
        lifecycleCard->handleInput();
        return true;
    }
    return false;
}

void LifeCountdownCardWrapper::prepareForRemoval() {
    markedForRemoval = true;
}

bool LifeCountdownCardWrapper::update() {
    if (!lifecycleCard) {
        return true;
    }
    
    // Update the card logic
    lifecycleCard->update();
    
    // Get current values and update display
    // This is a simplified version - you'll enhance this
    
    // For now, just update with placeholder text
    // In a full implementation, you'd call methods on lifecycleCard
    // to get the actual countdown values
    
    static char numberBuffer[16];
    snprintf(numberBuffer, sizeof(numberBuffer), "42");
    lv_label_set_text(labelNumber, numberBuffer);
    
    lv_label_set_text(labelUnit, "days");
    lv_label_set_text(labelContext, "until Friday");
    lv_label_set_text(labelMode, "2/7");
    
    return true; // Continue receiving updates
}