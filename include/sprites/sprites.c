/**
 * @file sprites.c
 * @brief Contains arrays of sprite pointers for each subdirectory
 */

#include "sprites.h"

// Array of all pokemon sprites
const lv_img_dsc_t* pokemon_sprites[] = {
    &sprite_bulbasaur,
    &sprite_charmander,
    &sprite_pikachu,
    &sprite_squirtle,
};

// Number of sprites in the pokemon animation
const uint8_t pokemon_sprites_count = sizeof(pokemon_sprites) / sizeof(pokemon_sprites[0]);

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

