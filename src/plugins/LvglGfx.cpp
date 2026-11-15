#include "plugins/LvglGfx.h"

#include <stdlib.h>
#include <string.h>

#include "Style.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

using deskhog::Align;
using deskhog::Font;

namespace {

void* allocatePixels(size_t size) {
#ifdef ESP_PLATFORM
    // 63 KB per card: keep it out of the scarce internal RAM.
    void* block = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (block) return block;
#endif
    return malloc(size);
}

// LVGL does not render canvas drawing as it is requested. Each call queues a
// task on LVGL's own heap, which is only 32 KB on the device and shared with
// every widget, and the queue is rendered when the layer is finished. A frame
// with a few hundred shapes would exhaust that heap, and LVGL does not survive
// running out. So the queue is rendered every few calls.
const int kMaxQueued = 8;

// Longest text one call will draw. LVGL copies the text onto its heap.
const size_t kMaxTextBytes = 255;

// Larger radii only cost memory: nothing on a 240px screen can show them.
const int kMaxRadius = 150;

}  // namespace

LvglGfx::LvglGfx(lv_obj_t* parent, int width, int height) : LvglMetrics(width, height) {
    const size_t size = LV_CANVAS_BUF_SIZE(width, height, 16, LV_DRAW_BUF_STRIDE_ALIGN);
    buffer_ = allocatePixels(size);
    if (!buffer_) return;
    memset(buffer_, 0, size);  // black until the plugin's first frame

    canvas_ = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas_, buffer_, width, height, LV_COLOR_FORMAT_RGB565);
}

LvglGfx::~LvglGfx() {
    // If the canvas still exists it is about to lose its pixels, so it has to go first.
    if (canvas_) lv_obj_delete(canvas_);
    free(buffer_);
}

void LvglGfx::beginFrame() {
    if (!canvas_ || inFrame_) return;
    inFrame_ = true;
    pending_ = 0;
    clipped_ = false;
    openLayer();
}

void LvglGfx::openLayer() {
    lv_canvas_init_layer(canvas_, &layer_);
    if (!clipped_) return;
    // Drawing calls take the layer's clip rectangle as it is when they are made.
    // (Worked out by hand: LVGL's own intersect function is private in 9.2.)
    lv_area_t& area = layer_._clip_area;
    area.x1 = LV_MAX(area.x1, clipArea_.x1);
    area.y1 = LV_MAX(area.y1, clipArea_.y1);
    area.x2 = LV_MIN(area.x2, clipArea_.x2);
    area.y2 = LV_MIN(area.y2, clipArea_.y2);
    if (area.x1 > area.x2 || area.y1 > area.y2) area = {0, 0, -1, -1};  // nothing is visible
}

void LvglGfx::queued() {
    if (++pending_ < kMaxQueued) return;
    pending_ = 0;
    lv_canvas_finish_layer(canvas_, &layer_);
    openLayer();
}

void LvglGfx::clip(int x, int y, int w, int h) {
    if (!inFrame_) return;
    clipped_ = w > 0 && h > 0;
    clipArea_ = {x, y, x + w - 1, y + h - 1};
    // Render what was drawn under the old rectangle, then start again with the new one.
    lv_canvas_finish_layer(canvas_, &layer_);
    pending_ = 0;
    openLayer();
}

void LvglGfx::image(int x, int y, const uint32_t* argb, int width, int height, int scale) {
    if (!inFrame_ || !argb || width <= 0 || height <= 0) return;

    // LVGL reads pixels through an image descriptor. 0xAARRGGBB words are
    // exactly its ARGB8888 layout on this (little-endian) machine.
    lv_image_dsc_t source;
    memset(&source, 0, sizeof(source));
    source.header.magic = LV_IMAGE_HEADER_MAGIC;
    source.header.cf = LV_COLOR_FORMAT_ARGB8888;
    source.header.w = width;
    source.header.h = height;
    source.header.stride = width * 4;
    source.data_size = static_cast<uint32_t>(width) * height * 4;
    source.data = reinterpret_cast<const uint8_t*>(argb);

    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = &source;
    dsc.opa = LV_OPA_COVER;
    if (scale > 1) {
        dsc.scale_x = 256 * scale;
        dsc.scale_y = 256 * scale;
        dsc.pivot.x = 0;  // grow from the top left corner
        dsc.pivot.y = 0;
        dsc.antialias = 0;  // keep pixel art crisp
    }
    lv_area_t area = {x, y, x + width - 1, y + height - 1};
    lv_draw_image(&layer_, &dsc, &area);

    // The descriptor lives on this stack frame, so render before returning.
    pending_ = kMaxQueued;
    queued();
}

void LvglGfx::endFrame() {
    if (!inFrame_) return;
    inFrame_ = false;
    if (!canvas_) return;
    lv_canvas_finish_layer(canvas_, &layer_);
    lv_obj_invalidate(canvas_);
}

void LvglGfx::fillRect(int x, int y, int w, int h, uint32_t rgb, int radius) {
    if (!inFrame_ || w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(rgb);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = radius > kMaxRadius ? kMaxRadius : radius;
    lv_area_t area = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer_, &dsc, &area);
    queued();
}

void LvglGfx::strokeRect(int x, int y, int w, int h, uint32_t rgb, int lineWidth) {
    if (!inFrame_ || w <= 0 || h <= 0 || lineWidth <= 0) return;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_TRANSP;
    dsc.border_color = lv_color_hex(rgb);
    dsc.border_opa = LV_OPA_COVER;
    dsc.border_width = lineWidth;
    lv_area_t area = {x, y, x + w - 1, y + h - 1};
    lv_draw_rect(&layer_, &dsc, &area);
    queued();
}

void LvglGfx::line(int x1, int y1, int x2, int y2, uint32_t rgb, int lineWidth) {
    if (!inFrame_ || lineWidth <= 0) return;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(rgb);
    dsc.width = lineWidth;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.p1.x = x1;
    dsc.p1.y = y1;
    dsc.p2.x = x2;
    dsc.p2.y = y2;
    lv_draw_line(&layer_, &dsc);
    queued();
}

void LvglGfx::text(int x, int y, const char* text, uint32_t rgb, Font font, Align align) {
    if (!inFrame_ || !text || !*text) return;

    // Keep the copy LVGL makes small. Cut on a character boundary, so a
    // multi-byte UTF-8 character is never split.
    char clipped[kMaxTextBytes + 1];
    size_t length = strlen(text);
    if (length > kMaxTextBytes) {
        length = kMaxTextBytes;
        while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) length--;
        memcpy(clipped, text, length);
        clipped[length] = '\0';
        text = clipped;
    }

    const lv_font_t* lvfont = lvFont(font);
    lv_point_t size;
    lv_text_get_size(&size, text, lvfont, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);

    int left = x;
    if (align == Align::Center) left = x - size.x / 2;
    if (align == Align::Right) left = x - size.x;

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.text_local = 1;  // LVGL keeps its own copy: the Lua string may be gone by the time it renders
    dsc.font = lvfont;
    dsc.color = lv_color_hex(rgb);
    dsc.opa = LV_OPA_COVER;
    dsc.flag = LV_TEXT_FLAG_EXPAND;  // never wrap: plugins break lines themselves with \n
    dsc.align = align == Align::Center ? LV_TEXT_ALIGN_CENTER : align == Align::Right ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_LEFT;
    lv_area_t area = {left, y, left + size.x, y + size.y};
    lv_draw_label(&layer_, &dsc, &area);
    // Render text right away: its copy is the largest thing a task can hold.
    pending_ = kMaxQueued;
    queued();
}

int LvglMetrics::textWidth(const char* text, Font font) {
    if (!text || !*text) return 0;
    lv_point_t size;
    lv_text_get_size(&size, text, lvFont(font), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
    return size.x;
}

int LvglMetrics::textHeight(Font font) { return lv_font_get_line_height(lvFont(font)); }

const lv_font_t* LvglMetrics::lvFont(Font font) {
    switch (font) {
        case Font::Small: return Style::labelFont();
        case Font::Medium: return Style::valueFont();
        case Font::Large: return Style::largeValueFont();
        case Font::Loud: return Style::loudNoisesFont();
    }
    return Style::labelFont();
}
