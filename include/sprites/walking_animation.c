/**
 * @file walking_animation.c
 * @brief Contains the sprite array for the 'walking_animation' group.
 */

#include "walking_animation.h"

// Array of sprites for 'walking_animation'
const lv_img_dsc_t* walking_animation[] = {
    &sprite_Normal_Walking_01,
    &sprite_Normal_Walking_03,
    &sprite_Normal_Walking_04,
    &sprite_Normal_Walking_05,
    &sprite_Normal_Walking_06,
    &sprite_Normal_Walking_07,
    &sprite_Normal_Walking_08,
    &sprite_Normal_Walking_09,
    &sprite_Normal_Walking_10,
    &sprite_Normal_Walking_11,
};

// Number of sprites in the 'walking_animation' array
const uint8_t walking_animation_count = sizeof(walking_animation) / sizeof(walking_animation[0]);
