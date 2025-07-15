/**
 * @file sprites.c
 * @brief Contains arrays of sprite pointers for each subdirectory
 */

#include "sprites.h"

// Array of all walking sprites
const lv_img_dsc_t* walking_sprites[] = {
    &sprite_Normal_Walking_01,
    &sprite_Normal_Walking_02,
    &sprite_Normal_Walking_03,
    &sprite_Normal_Walking_04,
    &sprite_Normal_Walking_05,
    &sprite_Normal_Walking_06,
};

// Number of sprites in the walking animation
const uint8_t walking_sprites_count = sizeof(walking_sprites) / sizeof(walking_sprites[0]);

