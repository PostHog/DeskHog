#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

/**
 * Turn a PNG or JPEG file into pixels for a plugin (gfx.fetchImage), scaled
 * down to fit a box. It never scales up.
 *
 * Only LVGL's bundled decoders are used here (lodepng's inflate and TJpgDec),
 * no Arduino, so the desktop simulator builds this same file and shows the
 * same picture as the device.
 *
 * What it reads: PNG that is not interlaced, up to about 300 x 300 pixels;
 * baseline JPEG up to 4 megapixels. Progressive JPEG is not supported.
 *
 * @param data   The file. It is used as working space: do not read it afterwards.
 * @param argb   Receives width * height pixels as 0xAARRGGBB, row by row.
 * @param error  Says why, when this returns false.
 */
bool decodeImage(uint8_t* data, size_t size, int maxWidth, int maxHeight, std::vector<uint32_t>& argb, int& width,
                 int& height, std::string& error);
