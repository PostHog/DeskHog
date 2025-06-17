/** @file sprites.h @brief Includes headers for WALKING animation sprites ONLY. */
#ifndef LVGL_WALKING_SPRITES_H
#define LVGL_WALKING_SPRITES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

// Include walking sprite headers
#include "sprite_Normal_Walking_01.h"
#include "sprite_Normal_Walking_02.h"
#include "sprite_Normal_Walking_03.h"
#include "sprite_Normal_Walking_04.h"
#include "sprite_Normal_Walking_05.h"
#include "sprite_Normal_Walking_06.h"
#include "sprite_Normal_Walking_07.h"
#include "sprite_Normal_Walking_08.h"
#include "sprite_Normal_Walking_09.h"
#include "sprite_Normal_Walking_10.h"
#include "sprite_Normal_Walking_11.h"

// Array of all walking animation sprites
extern const lv_img_dsc_t* walking_sprites[];
extern const uint8_t walking_sprites_count;

#ifdef __cplusplus
}
#endif

#endif /* LVGL_WALKING_SPRITES_H */
