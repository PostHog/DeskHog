/*
LodePNG version 20201017

Copyright (c) 2005-2020 Lode Vandevenne

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/

#ifndef LODEPNG_H
#define LODEPNG_H

#include <stddef.h> /*for size_t*/

#ifdef __cplusplus
extern "C" {
#endif

/*
Decodes PNG data from memory to raw pixel data.
out: Output parameter. Pointer to buffer that will contain the raw pixel data.
     Must be freed after usage with free(*out).
w: Output parameter. Pointer to width of the image.
h: Output parameter. Pointer to height of the image.
in: Memory buffer with the PNG file.
insize: size of the memory buffer.
colortype: the desired color type for the raw output image. See explanation on PNG color types.
bitdepth: the desired bit depth for the raw output image. See explanation on PNG bit depth.
Return value: LodePNG error code (0 means no error).
*/
unsigned lodepng_decode_memory(unsigned char** out, unsigned* w, unsigned* h,
                              const unsigned char* in, size_t insize,
                              unsigned colortype, unsigned bitdepth);

/*Same as lodepng_decode_memory, but always decodes to 32-bit RGBA raw image*/
unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                          const unsigned char* in, size_t insize);

/*Same as lodepng_decode_memory, but always decodes to 24-bit RGB raw image*/
unsigned lodepng_decode24(unsigned char** out, unsigned* w, unsigned* h,
                          const unsigned char* in, size_t insize);

typedef enum LodePNGColorType {
  LCT_GREY = 0, /*grayscale: 1,2,4,8,16 bit*/
  LCT_RGB = 2, /*RGB: 8,16 bit*/
  LCT_PALETTE = 3, /*palette: 1,2,4,8 bit*/
  LCT_GREY_ALPHA = 4, /*grayscale with alpha: 8,16 bit*/
  LCT_RGBA = 6, /*RGB with alpha: 8,16 bit*/
  /*LCT_MAX_OCTET_VALUE lets the compiler allow this enum to represent any invalid
  byte value from 0 to 255 that could be present in an invalid PNG file header. Do
  not use, compare with or set the name LCT_MAX_OCTET_VALUE, instead either use
  the valid color type names above, or numeric values like 1 or 7 when checking for
  particular disallowed color type byte values, or cast to integer to print it.*/
  LCT_MAX_OCTET_VALUE = 255
} LodePNGColorType;

#ifdef __cplusplus
}
#endif

#endif /*LODEPNG_H*/