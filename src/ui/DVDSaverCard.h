#pragma once

#include <lvgl.h>
#include "sprites/logo/sprite_posthog_logo_white.h"
#include <vector>

class DVDSaverCard {
public:
    DVDSaverCard(lv_obj_t* parent, uint16_t screenWidth, uint16_t screenHeight);
    ~DVDSaverCard();

    lv_obj_t* getCard() const;

private:
    static void animation_timer_cb(lv_timer_t* timer);
    void update_animation();
    void change_logo_color();

    lv_obj_t* _card;
    lv_obj_t* _logo_img;
    
    int16_t _pos_x;
    int16_t _pos_y;
    int16_t _vel_x;
    int16_t _vel_y;

    uint16_t _screenWidth;
    uint16_t _screenHeight;
    uint16_t _img_width;
    uint16_t _img_height;

    lv_timer_t* _animation_timer;

    std::vector<lv_color_t> _logo_colors;
    int _current_color_index;
}; 