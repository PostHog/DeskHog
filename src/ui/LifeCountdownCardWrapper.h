#pragma once

#include <lvgl.h>
#include "ui/InputHandler.h"
#include "LifeCountdownCard.h"

class LifeCountdownCardWrapper : public InputHandler {
public:
    LifeCountdownCardWrapper(lv_obj_t* parent);
    ~LifeCountdownCardWrapper();
    
    lv_obj_t* getCard();
    bool handleButtonPress(uint8_t button_index) override;
    void prepareForRemoval() override;
    bool update() override;

private:
    LifeCountdownCard* countdownGame;
    lv_obj_t* cardContainer;
    bool markedForRemoval;
};