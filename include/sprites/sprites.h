/**
 * @file sprites.h
 * @brief Includes all LVGL sprite headers and arrays
 */

#ifndef LVGL_SPRITES_H
#define LVGL_SPRITES_H

#ifdef __cplusplus
extern "C" {
#endif

// Include all sprite headers
#include "sprite_Normal_Walking_01.h"
#include "sprite_Normal_Walking_02.h"
#include "sprite_Normal_Walking_03.h"
#include "sprite_Normal_Walking_04.h"
#include "sprite_Normal_Walking_05.h"
#include "sprite_Normal_Walking_06.h"

// Array of all walking sprites
extern const lv_img_dsc_t* walking_sprites[];
extern const uint8_t walking_sprites_count;

#ifdef __cplusplus
}
#endif

#endif /* LVGL_SPRITES_H */
