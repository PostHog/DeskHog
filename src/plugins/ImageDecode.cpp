#include "plugins/ImageDecode.h"

#include <stdlib.h>
#include <string.h>

#include <lvgl.h>

// LVGL wraps lodepng's header in extern "C", which its C++ helpers cannot live in. They are not needed.
#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"
#include "src/libs/tjpgd/tjpgd.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

// lodepng is built without its own allocator (-DLODEPNG_NO_COMPILE_ALLOCATORS,
// see platformio.ini): the one LVGL gives it takes from LVGL's 32 KB heap,
// which a picture would exhaust at once. The ones at the end of this file put
// its buffers in PSRAM instead.
extern "C" {
void* lodepng_malloc(size_t size);
void* lodepng_realloc(void* ptr, size_t size);
void lodepng_free(void* ptr);
}

namespace {

// Is there room for a block this large? Pictures are decoded in PSRAM, which
// the cards' pixels and the plugins' own memory live in too, so a picture
// must leave a good part of it alone. (On the device a std::vector that cannot
// grow ends the program; this is also asked before one is filled.)
bool roomFor(size_t bytes) {
#ifdef ESP_PLATFORM
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    return heap_caps_get_free_size(caps) > bytes + 256 * 1024 && heap_caps_get_largest_free_block(caps) > bytes;
#else
    (void)bytes;
    return true;
#endif
}

void* bigAlloc(size_t size) {
    if (!roomFor(size)) return nullptr;
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

void* bigRealloc(void* ptr, size_t size) {
    if (!roomFor(size)) return nullptr;
#ifdef ESP_PLATFORM
    return heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return realloc(ptr, size);
#endif
}

const int kMaxSide = 240;                    // what a plugin may hold, each way
const size_t kMaxPngBytes = 362 * 1024;      // a PNG's pixels as stored in the file: 300 x 300 with transparency
const uint32_t kMaxJpegPixels = 4000 * 1000;
const size_t kJpegPoolBytes = 8192;

/**
 * Shrinks a picture as its pixels arrive, in any order: every pixel of the
 * result is the average of the source pixels that fall on it.
 */
class Downscaler {
public:
    ~Downscaler() { free(sums_); }

    bool begin(int sourceWidth, int sourceHeight, int maxWidth, int maxHeight) {
        sourceWidth_ = sourceWidth;
        sourceHeight_ = sourceHeight;
        if (maxWidth < 1) maxWidth = 1;
        if (maxHeight < 1) maxHeight = 1;
        if (maxWidth > kMaxSide) maxWidth = kMaxSide;
        if (maxHeight > kMaxSide) maxHeight = kMaxSide;
        width_ = sourceWidth;
        height_ = sourceHeight;
        // Fit inside the box, keeping the shape.
        if (width_ > maxWidth) {
            height_ = (int)((int64_t)sourceHeight * maxWidth / sourceWidth);
            width_ = maxWidth;
        }
        if (height_ > maxHeight) {
            width_ = (int)((int64_t)sourceWidth * maxHeight / sourceHeight);
            height_ = maxHeight;
        }
        if (width_ < 1) width_ = 1;
        if (height_ < 1) height_ = 1;

        // The sums are 16 bits wide, good for 256 source pixels each. A picture
        // shrunk more than 16 times each way is sampled every few pixels.
        const int ratioX = (sourceWidth + width_ - 1) / width_;
        const int ratioY = (sourceHeight + height_ - 1) / height_;
        const int ratio = ratioX > ratioY ? ratioX : ratioY;
        step_ = (ratio + 15) / 16;

        const size_t count = (size_t)width_ * height_;
        sums_ = static_cast<uint16_t*>(bigAlloc(count * 4 * sizeof(uint16_t)));
        if (!sums_) return false;
        memset(sums_, 0, count * 4 * sizeof(uint16_t));
        return true;
    }

    int width() const { return width_; }
    int height() const { return height_; }

    void add(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (x < 0 || y < 0 || x >= sourceWidth_ || y >= sourceHeight_) return;
        if (x % step_ != 0 || y % step_ != 0) return;
        const int tx = (int)((int64_t)x * width_ / sourceWidth_);
        const int ty = (int)((int64_t)y * height_ / sourceHeight_);
        uint16_t* sum = sums_ + ((size_t)ty * width_ + tx) * 4;
        // Colour weighs in by how solid it is, so see-through pixels do not darken an edge.
        sum[0] += (uint16_t)(r * a / 255);
        sum[1] += (uint16_t)(g * a / 255);
        sum[2] += (uint16_t)(b * a / 255);
        sum[3] += a;
    }

    bool finish(std::vector<uint32_t>& out) {
        const size_t count = (size_t)width_ * height_;
        if (!roomFor(count * sizeof(uint32_t))) return false;
        // How many source pixels land on each column and each row.
        std::vector<uint16_t> perColumn(width_, 0), perRow(height_, 0);
        for (int x = 0; x < sourceWidth_; x += step_) perColumn[(int64_t)x * width_ / sourceWidth_]++;
        for (int y = 0; y < sourceHeight_; y += step_) perRow[(int64_t)y * height_ / sourceHeight_]++;

        out.resize(count);
        for (int y = 0; y < height_; y++) {
            for (int x = 0; x < width_; x++) {
                const uint16_t* sum = sums_ + ((size_t)y * width_ + x) * 4;
                const uint32_t samples = (uint32_t)perColumn[x] * perRow[y];
                uint32_t pixel = 0;
                if (samples > 0 && sum[3] > 0) {
                    const uint32_t alpha = sum[3] / samples;
                    const uint32_t r = (uint32_t)sum[0] * 255 / sum[3];
                    const uint32_t g = (uint32_t)sum[1] * 255 / sum[3];
                    const uint32_t b = (uint32_t)sum[2] * 255 / sum[3];
                    pixel = (alpha << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
                }
                out[(size_t)y * width_ + x] = pixel;
            }
        }
        return true;
    }

private:
    int sourceWidth_ = 1, sourceHeight_ = 1;
    int width_ = 1, height_ = 1;
    int step_ = 1;
    uint16_t* sums_ = nullptr;
};

// ---------------------------------------------------------------------- PNG

uint32_t big32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

struct Freed {
    void* block = nullptr;
    ~Freed() { lodepng_free(block); }
};

uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = (int)a + b - c;
    const int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

// One sample of 1, 2, 4, 8 or 16 bits from a row. 16-bit samples are big-endian.
uint32_t sampleAt(const uint8_t* row, size_t index, int depth) {
    if (depth == 8) return row[index];
    if (depth == 16) return ((uint32_t)row[index * 2] << 8) | row[index * 2 + 1];
    const size_t bit = index * depth;
    return (row[bit / 8] >> (8 - depth - (int)(bit % 8))) & ((1u << depth) - 1);
}

uint8_t to8(uint32_t sample, int depth) {
    if (depth == 8) return (uint8_t)sample;
    if (depth == 16) return (uint8_t)(sample >> 8);
    return (uint8_t)(sample * 255 / ((1u << depth) - 1));
}

bool decodePng(uint8_t* data, size_t size, int maxWidth, int maxHeight, std::vector<uint32_t>& argb, int& width,
               int& height, std::string& error) {
    uint32_t w = 0, h = 0;
    int depth = 0, colorType = -1;
    uint8_t palette[256 * 3];
    size_t paletteCount = 0;
    uint8_t transparency[256];
    size_t transparencyBytes = 0;

    // The compressed pixels are spread over IDAT chunks. They are moved
    // together to the front of the file's own buffer, which saves a copy; what
    // they overwrite has been read by then.
    size_t packedSize = 0;

    size_t at = 8;
    bool ended = false;
    while (!ended && at + 12 <= size) {
        const uint32_t length = big32(data + at);
        const uint8_t* type = data + at + 4;
        const uint8_t* body = data + at + 8;
        if (length > size - at - 12) break;  // the chunk runs past the end of the file
        if (memcmp(type, "IHDR", 4) == 0 && length >= 13) {
            w = big32(body);
            h = big32(body + 4);
            depth = body[8];
            colorType = body[9];
            if (body[12] != 0) {
                error = "interlaced PNG pictures are not supported";
                return false;
            }
        } else if (memcmp(type, "PLTE", 4) == 0 && packedSize == 0) {
            paletteCount = length / 3 > 256 ? 256 : length / 3;
            memcpy(palette, body, paletteCount * 3);
        } else if (memcmp(type, "tRNS", 4) == 0 && packedSize == 0) {
            transparencyBytes = length > sizeof(transparency) ? sizeof(transparency) : length;
            memcpy(transparency, body, transparencyBytes);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            memmove(data + packedSize, body, length);  // always towards the front
            packedSize += length;
        } else if (memcmp(type, "IEND", 4) == 0) {
            ended = true;
        }
        at += 12 + (size_t)length;
    }

    int channels = 0;
    bool depthOk = false;
    switch (colorType) {
        case 0: channels = 1; depthOk = depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16; break;
        case 2: channels = 3; depthOk = depth == 8 || depth == 16; break;
        case 3: channels = 1; depthOk = (depth == 1 || depth == 2 || depth == 4 || depth == 8) && paletteCount; break;
        case 4: channels = 2; depthOk = depth == 8 || depth == 16; break;
        case 6: channels = 4; depthOk = depth == 8 || depth == 16; break;
        default: break;
    }
    if (w == 0 || h == 0 || !depthOk || packedSize == 0) {
        error = "not a PNG picture this device can read";
        return false;
    }
    const size_t bitsPerPixel = (size_t)channels * depth;
    const size_t rowBytes = ((size_t)w * bitsPerPixel + 7) / 8;
    const size_t step = bitsPerPixel >= 8 ? bitsPerPixel / 8 : 1;  // how far back "the pixel to the left" is
    if (w > 4096 || h > 4096 || (rowBytes + 1) * (size_t)h > kMaxPngBytes) {
        error = "PNG picture too large (about 300 x 300 at most)";
        return false;
    }
    const size_t expected = (rowBytes + 1) * (size_t)h;

    Freed pixels;
    size_t unpacked = 0;
    LodePNGDecompressSettings settings;
    lodepng_decompress_settings_init(&settings);
    settings.max_output_size = expected;
    unsigned char* out = nullptr;
    const unsigned failure = lodepng_zlib_decompress(&out, &unpacked, data, packedSize, &settings);
    pixels.block = out;
    if (failure || unpacked < expected) {
        error = failure == 83 ? "not enough memory for this picture" : "damaged PNG picture";
        return false;
    }

    Downscaler scaler;
    if (!scaler.begin((int)w, (int)h, maxWidth, maxHeight)) {
        error = "not enough memory for this picture";
        return false;
    }

    // A single colour may be marked as see-through (grey and RGB pictures).
    uint32_t key[3] = {0, 0, 0};
    bool hasKey = false;
    if (colorType == 0 && transparencyBytes >= 2) {
        key[0] = ((uint32_t)transparency[0] << 8) | transparency[1];
        hasKey = true;
    } else if (colorType == 2 && transparencyBytes >= 6) {
        for (int i = 0; i < 3; i++) key[i] = ((uint32_t)transparency[i * 2] << 8) | transparency[i * 2 + 1];
        hasKey = true;
    }

    const uint8_t* previous = nullptr;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* line = out + (size_t)y * (rowBytes + 1);
        const uint8_t filter = line[0];
        uint8_t* row = line + 1;
        if (filter > 4) {
            error = "damaged PNG picture";
            return false;
        }
        // Each row is stored as the difference from its neighbours; undo that.
        for (size_t i = 0; i < rowBytes; i++) {
            const uint8_t left = i >= step ? row[i - step] : 0;
            const uint8_t up = previous ? previous[i] : 0;
            const uint8_t upLeft = (previous && i >= step) ? previous[i - step] : 0;
            switch (filter) {
                case 1: row[i] = (uint8_t)(row[i] + left); break;
                case 2: row[i] = (uint8_t)(row[i] + up); break;
                case 3: row[i] = (uint8_t)(row[i] + ((left + up) >> 1)); break;
                case 4: row[i] = (uint8_t)(row[i] + paeth(left, up, upLeft)); break;
                default: break;
            }
        }
        previous = row;

        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (colorType == 3) {
                const uint32_t index = sampleAt(row, x, depth);
                if (index < paletteCount) {
                    r = palette[index * 3];
                    g = palette[index * 3 + 1];
                    b = palette[index * 3 + 2];
                }
                if (index < transparencyBytes) a = transparency[index];
            } else if (colorType == 0 || colorType == 4) {
                const uint32_t grey = sampleAt(row, (size_t)x * channels, depth);
                r = g = b = to8(grey, depth);
                if (colorType == 4) a = to8(sampleAt(row, (size_t)x * 2 + 1, depth), depth);
                if (hasKey && grey == key[0]) a = 0;
            } else {
                uint32_t rgb[3];
                for (int i = 0; i < 3; i++) rgb[i] = sampleAt(row, (size_t)x * channels + i, depth);
                r = to8(rgb[0], depth);
                g = to8(rgb[1], depth);
                b = to8(rgb[2], depth);
                if (colorType == 6) a = to8(sampleAt(row, (size_t)x * 4 + 3, depth), depth);
                if (hasKey && rgb[0] == key[0] && rgb[1] == key[1] && rgb[2] == key[2]) a = 0;
            }
            scaler.add((int)x, (int)y, r, g, b, a);
        }
    }

    lodepng_free(pixels.block);  // make room before the result is allocated
    pixels.block = nullptr;
    if (!scaler.finish(argb)) {
        error = "not enough memory for this picture";
        return false;
    }
    width = scaler.width();
    height = scaler.height();
    return true;
}

// --------------------------------------------------------------------- JPEG

struct JpegJob {
    const uint8_t* data;
    size_t size;
    size_t at;
    Downscaler* scaler;
};

size_t jpegRead(JDEC* decoder, uint8_t* buffer, size_t count) {
    JpegJob* job = static_cast<JpegJob*>(decoder->device);
    if (count > job->size - job->at) count = job->size - job->at;
    if (buffer) memcpy(buffer, job->data + job->at, count);
    job->at += count;
    return count;
}

// TJpgDec hands over the picture a block at a time. LVGL's copy of it writes
// each pixel as blue, green, red.
int jpegBlock(JDEC* decoder, void* bitmap, JRECT* rect) {
    JpegJob* job = static_cast<JpegJob*>(decoder->device);
    const uint8_t* rgb = static_cast<const uint8_t*>(bitmap);  // b, g, r
    for (int y = rect->top; y <= rect->bottom; y++) {
        for (int x = rect->left; x <= rect->right; x++) {
            job->scaler->add(x, y, rgb[2], rgb[1], rgb[0], 255);
            rgb += 3;
        }
    }
    return 1;
}

bool decodeJpeg(const uint8_t* data, size_t size, int maxWidth, int maxHeight, std::vector<uint32_t>& argb,
                int& width, int& height, std::string& error) {
    Freed pool;
    pool.block = lodepng_malloc(kJpegPoolBytes);
    if (!pool.block) {
        error = "not enough memory for this picture";
        return false;
    }
    Downscaler scaler;
    JpegJob job{data, size, 0, &scaler};
    JDEC decoder;
    JRESULT result = jd_prepare(&decoder, jpegRead, pool.block, kJpegPoolBytes, &job);
    if (result != JDR_OK) {
        error = result == JDR_FMT3   ? "this kind of JPEG picture (progressive) is not supported"
                : result == JDR_MEM1 ? "this JPEG picture needs more memory than the device sets aside"
                                     : "damaged JPEG picture";
        return false;
    }
    if (decoder.width == 0 || decoder.height == 0 || (uint32_t)decoder.width * decoder.height > kMaxJpegPixels) {
        error = "JPEG picture too large (4 megapixels at most)";
        return false;
    }
    if (!scaler.begin(decoder.width, decoder.height, maxWidth, maxHeight)) {
        error = "not enough memory for this picture";
        return false;
    }
    result = jd_decomp(&decoder, jpegBlock, 0);
    if (result != JDR_OK) {
        error = "damaged JPEG picture";
        return false;
    }
    if (!scaler.finish(argb)) {
        error = "not enough memory for this picture";
        return false;
    }
    width = scaler.width();
    height = scaler.height();
    return true;
}

}  // namespace

extern "C" {
void* lodepng_malloc(size_t size) { return bigAlloc(size); }
void* lodepng_realloc(void* ptr, size_t size) { return bigRealloc(ptr, size); }
void lodepng_free(void* ptr) { free(ptr); }
}

bool decodeImage(uint8_t* data, size_t size, int maxWidth, int maxHeight, std::vector<uint32_t>& argb, int& width,
                 int& height, std::string& error) {
    static const uint8_t pngMagic[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    argb.clear();
    width = height = 0;
    if (data && size >= 8 && memcmp(data, pngMagic, 8) == 0) {
        return decodePng(data, size, maxWidth, maxHeight, argb, width, height, error);
    }
    if (data && size >= 3 && data[0] == 0xFF && data[1] == 0xD8) {
        return decodeJpeg(data, size, maxWidth, maxHeight, argb, width, height, error);
    }
    error = "not a PNG or JPEG picture";
    return false;
}
