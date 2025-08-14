/*
PNG decoder implementation for ESP32 using pngle library
Provides lodepng-compatible API using pngle as the backend
*/

#include "lodepng.h"
#include <pngle.h>
#include <stdlib.h>
#include <string.h>
#include <Arduino.h>

// Structure to hold decoding context
struct DecodingContext {
    unsigned char* output;
    unsigned width;
    unsigned height;
    unsigned currentPixel;
    bool error;
    unsigned targetColorType;
    unsigned targetBitDepth;
};

// Callback for pngle when image info is available
static void pngle_on_init(pngle_t *pngle, uint32_t w, uint32_t h) {
    DecodingContext* ctx = (DecodingContext*)pngle_get_user_data(pngle);
    if (!ctx) return;
    
    ctx->width = w;
    ctx->height = h;
    ctx->currentPixel = 0;
    
    // Allocate output buffer for RGBA data
    size_t bufferSize = w * h * 4; // 4 bytes per pixel (RGBA)
    ctx->output = (unsigned char*)malloc(bufferSize);
    
    if (!ctx->output) {
        ctx->error = true;
        Serial.printf("[lodepng] Failed to allocate %d bytes for %ux%u image\n", bufferSize, w, h);
    } else {
        Serial.printf("[lodepng] Allocated buffer for %ux%u image (%d bytes)\n", w, h, bufferSize);
        // Initialize to transparent
        memset(ctx->output, 0, bufferSize);
    }
}

// Callback for pngle when a pixel is decoded
static void pngle_on_draw(pngle_t *pngle, uint32_t x, uint32_t y, uint32_t w, uint32_t h, 
                          const uint8_t rgba[4]) {
    DecodingContext* ctx = (DecodingContext*)pngle_get_user_data(pngle);
    if (!ctx || !ctx->output || ctx->error) {
        return;
    }
    
    // pngle can call this with rectangles, not just single pixels
    // We need to handle the rectangle case
    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            
            // Calculate pixel position in the output buffer
            unsigned pixelIndex = (py * ctx->width + px) * 4;
            
            // Make sure we don't overflow
            if (pixelIndex + 3 < ctx->width * ctx->height * 4) {
                // Copy RGBA data
                ctx->output[pixelIndex + 0] = rgba[0]; // R
                ctx->output[pixelIndex + 1] = rgba[1]; // G
                ctx->output[pixelIndex + 2] = rgba[2]; // B
                ctx->output[pixelIndex + 3] = rgba[3]; // A
                ctx->currentPixel++;
            }
        }
    }
}

// Main decode function that mimics lodepng_decode_memory
unsigned lodepng_decode_memory(unsigned char** out, unsigned* w, unsigned* h,
                              const unsigned char* in, size_t insize,
                              unsigned colortype, unsigned bitdepth) {
    
    Serial.printf("[lodepng] Starting decode: %d bytes, colortype=%u, bitdepth=%u\n", 
                  insize, colortype, bitdepth);
    
    // Create pngle instance
    pngle_t* pngle = pngle_new();
    if (!pngle) {
        Serial.println("[lodepng] Failed to create pngle instance");
        return 1; // Memory allocation failed
    }
    
    // Setup decoding context
    DecodingContext ctx;
    ctx.output = nullptr;
    ctx.width = 0;
    ctx.height = 0;
    ctx.currentPixel = 0;
    ctx.error = false;
    ctx.targetColorType = colortype;
    ctx.targetBitDepth = bitdepth;
    
    // Set user data and callbacks
    pngle_set_user_data(pngle, &ctx);
    pngle_set_init_callback(pngle, pngle_on_init);
    pngle_set_draw_callback(pngle, pngle_on_draw);
    
    // Feed the PNG data to pngle
    int result = pngle_feed(pngle, in, insize);
    
    if (result < 0) {
        Serial.printf("[lodepng] pngle_feed error: %d\n", result);
        if (ctx.output) {
            free(ctx.output);
        }
        pngle_destroy(pngle);
        return 1; // Decoding error
    }
    
    // Check if we successfully decoded the image
    if (ctx.error || !ctx.output || ctx.width == 0 || ctx.height == 0) {
        Serial.printf("[lodepng] Decode failed: error=%d, output=%p, size=%ux%u\n", 
                     ctx.error, ctx.output, ctx.width, ctx.height);
        if (ctx.output) {
            free(ctx.output);
        }
        pngle_destroy(pngle);
        return 1;
    }
    
    // Success! Return the decoded data
    *out = ctx.output;
    *w = ctx.width;
    *h = ctx.height;
    
    Serial.printf("[lodepng] Successfully decoded %ux%u image (%u pixels processed)\n", 
                  ctx.width, ctx.height, ctx.currentPixel);
    
    pngle_destroy(pngle);
    return 0; // Success
}

// Convenience function for RGBA decoding
unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                          const unsigned char* in, size_t insize) {
    return lodepng_decode_memory(out, w, h, in, insize, LCT_RGBA, 8);
}

// Convenience function for RGB decoding
unsigned lodepng_decode24(unsigned char** out, unsigned* w, unsigned* h,
                          const unsigned char* in, size_t insize) {
    // For now, decode as RGBA and caller can ignore alpha
    return lodepng_decode32(out, w, h, in, insize);
}