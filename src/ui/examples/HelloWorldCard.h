#pragma once

#include <lvgl.h>
#include "../InputHandler.h"

class HelloWorldCard : public InputHandler {
public:
    HelloWorldCard(lv_obj_t* parent);
    ~HelloWorldCard();
    
    lv_obj_t* getCard() const { return _card; }
    
    bool handleButtonPress(uint8_t button_index) override;
    void prepareForRemoval() override { _card = nullptr; }

private:
    lv_obj_t* _card;
    lv_obj_t* _label;
};