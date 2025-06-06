/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --font /Users/joemartin/Documents/GitHub/DeskHog/fonts_to_convert/unscii-16-full.otf --bpp 1 --size 16 -o src/fonts/lv_unscii_16_custom_symbols.c --format lvgl --range 0xEC3E,0xEC47,0xEC28,0xEC29,0xEC34,0xEC00,0xE846,0x8E3D,0x8E3E,0x062B,0x8E3F,0x4DD6,0x263B --no-kerning --no-compress
 ******************************************************************************/

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_UNSCII_16_CUSTOM_SYMBOLS
#define LV_UNSCII_16_CUSTOM_SYMBOLS 1
#endif

#if LV_UNSCII_16_CUSTOM_SYMBOLS

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+062B "ث" */
    0x18, 0x0, 0x66, 0x0, 0x81, 0x81, 0x81, 0xff,
    0x7e,

    /* U+263B "☻" */
    0x7e, 0xff, 0xff, 0xdb, 0xff, 0xff, 0xc3, 0xe7,
    0xff, 0xff, 0x7e,

    /* U+4DD6 "䷖" */
    0xff, 0xfc, 0x0, 0xe, 0x1, 0xc0, 0x0, 0xe0,
    0x1c, 0x0, 0x0, 0x0, 0x38, 0x7, 0x0, 0x3,
    0x80, 0x70, 0x0, 0x38, 0x7,

    /* U+8E3D "踽" */
    0x0, 0x6, 0x7c, 0x1e, 0x7f, 0xfe, 0x47, 0xe0,
    0x44, 0x20, 0x45, 0xfe, 0x7d, 0x26, 0x19, 0x26,
    0x19, 0xfe, 0x5c, 0x20, 0x5b, 0xff, 0x5a, 0x23,
    0x5a, 0x2b, 0x5e, 0xfb, 0xf2, 0xb, 0x2, 0xf,
    0x2, 0x6,

    /* U+8E3E "踾" */
    0x7d, 0xfe, 0x88, 0x1, 0x13, 0xf2, 0x24, 0x27,
    0xc8, 0x43, 0x1f, 0x86, 0x0, 0xc, 0xff, 0x5f,
    0x22, 0xb2, 0x45, 0x67, 0xfa, 0xf9, 0x17, 0xf2,
    0x3e, 0x3f, 0xc0, 0x40, 0x80,

    /* U+8E3F "踿" */
    0x0, 0xc, 0x78, 0xcc, 0x7b, 0xcc, 0x4b, 0xc,
    0x49, 0x2f, 0x79, 0x2f, 0x7b, 0xef, 0x1b, 0xee,
    0x1f, 0x6c, 0x1f, 0xc, 0x5f, 0xde, 0x5f, 0x1e,
    0x59, 0x1e, 0x5f, 0x3e, 0x7f, 0x36, 0xf1, 0x76,
    0x1, 0x62,

    /* U+E846 "" */
    0x1, 0x1, 0xff, 0x10, 0x10, 0x10, 0xff, 0x1,
    0x1, 0x1, 0xff, 0x10, 0x10, 0x10, 0xff, 0x1,

    /* U+EC00 "" */
    0x7, 0x7, 0xf7, 0x7f, 0x7, 0x30, 0x3b, 0x31,
    0xf0, 0x33, 0x3, 0xb3, 0x1f, 0x3, 0x30, 0x3b,
    0x31, 0xf0, 0x30, 0x0,

    /* U+EC28 "" */
    0x38, 0x38, 0x38, 0xf8, 0xc8, 0xfe, 0x3e, 0x3f,
    0x1b, 0x3f, 0x3c, 0x7c, 0x6c, 0xec, 0xcc, 0xcc,

    /* U+EC29 "" */
    0x38, 0x70, 0xe1, 0xc1, 0x2, 0x3f, 0xff, 0x10,
    0x20, 0xe1, 0xc7, 0xcf, 0xbf, 0xff,

    /* U+EC34 "" */
    0xc1, 0xe0, 0xfd, 0xfe, 0xff, 0xfb, 0xf9, 0xfc,
    0x38, 0x1c, 0xe, 0xf, 0x86, 0xcf, 0x3f, 0x1f,
    0x8e,

    /* U+EC3E "" */
    0xe0, 0x70, 0x3b, 0x1d, 0x8f, 0xe3, 0xf1, 0xf8,
    0xfc, 0x7e, 0x3f, 0xdf, 0xc6, 0x63, 0x30,

    /* U+EC47 "" */
    0xff, 0xf8, 0x61, 0xff, 0xf8, 0x61, 0xff, 0xf8,
    0x61, 0xff, 0xf8, 0x61
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 128, .box_w = 8, .box_h = 9, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 9, .adv_w = 128, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 20, .adv_w = 128, .box_w = 14, .box_h = 12, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 41, .adv_w = 128, .box_w = 16, .box_h = 17, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 75, .adv_w = 128, .box_w = 15, .box_h = 15, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 104, .adv_w = 128, .box_w = 16, .box_h = 17, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 128, .box_w = 8, .box_h = 16, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 154, .adv_w = 128, .box_w = 9, .box_h = 17, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 174, .adv_w = 128, .box_w = 8, .box_h = 16, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 128, .box_w = 7, .box_h = 16, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 128, .box_w = 9, .box_h = 15, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 221, .adv_w = 128, .box_w = 9, .box_h = 13, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 236, .adv_w = 128, .box_w = 6, .box_h = 16, .ofs_x = 1, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x2010, 0x47ab, 0x8812, 0x8813, 0x8814, 0xe21b, 0xe5d5,
    0xe5fd, 0xe5fe, 0xe609, 0xe613, 0xe61c
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 1579, .range_length = 58909, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 13, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_unscii_16_custom_symbols = {
#else
lv_font_t lv_unscii_16_custom_symbols = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 17,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if LV_UNSCII_16_CUSTOM_SYMBOLS*/

