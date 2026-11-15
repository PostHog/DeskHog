#pragma once

#include <lvgl.h>

#include "PluginHost.h"

/**
 * Card size and text measurements, with the device's fonts. Drawing calls do
 * nothing. A plugin uses this when it measures text while no card is being
 * drawn.
 *
 * Only LVGL is used here, no Arduino, so the desktop simulator builds this
 * same file.
 */
class LvglMetrics : public deskhog::Gfx {
public:
    LvglMetrics(int width, int height) : width_(width), height_(height) {}

    int width() const override { return width_; }
    int height() const override { return height_; }
    void fillRect(int, int, int, int, uint32_t, int) override {}
    void strokeRect(int, int, int, int, uint32_t, int) override {}
    void line(int, int, int, int, uint32_t, int) override {}
    void text(int, int, const char*, uint32_t, deskhog::Font, deskhog::Align) override {}
    void image(int, int, const uint32_t*, int, int, int) override {}
    void clip(int, int, int, int) override {}
    int textWidth(const char* text, deskhog::Font font) override;
    int textHeight(deskhog::Font font) override;

protected:
    static const lv_font_t* lvFont(deskhog::Font font);

    int width_;
    int height_;
};

/**
 * Drawing surface for one plugin card: an LVGL canvas with its own pixel
 * buffer. The canvas object is the card that goes into the card stack.
 */
class LvglGfx : public LvglMetrics {
public:
    LvglGfx(lv_obj_t* parent, int width, int height);
    ~LvglGfx() override;

    /** False when the pixel buffer could not be allocated. */
    bool ok() const { return canvas_ != nullptr; }
    lv_obj_t* canvas() const { return canvas_; }

    /** Call when LVGL deletes the canvas object itself (the card stack does). */
    void canvasDeleted() { canvas_ = nullptr; }

    /** The card's pixels: RGB565, row by row, width() * 2 bytes per row. */
    const uint8_t* pixels() const { return static_cast<const uint8_t*>(buffer_); }
    size_t pixelBytes() const { return static_cast<size_t>(width_) * height_ * 2; }

    /** Drawing calls only do something between beginFrame() and endFrame(). */
    void beginFrame();
    void endFrame();

    void fillRect(int x, int y, int w, int h, uint32_t rgb, int radius) override;
    void strokeRect(int x, int y, int w, int h, uint32_t rgb, int lineWidth) override;
    void line(int x1, int y1, int x2, int y2, uint32_t rgb, int lineWidth) override;
    void text(int x, int y, const char* text, uint32_t rgb, deskhog::Font font, deskhog::Align align) override;
    void image(int x, int y, const uint32_t* argb, int width, int height, int scale) override;
    void clip(int x, int y, int w, int h) override;

private:
    /** Start (or restart) the layer, with the current clip rectangle. */
    void openLayer();

    /** Render what has been queued so far. See the note in LvglGfx.cpp. */
    void queued();

    lv_obj_t* canvas_ = nullptr;
    void* buffer_ = nullptr;
    lv_layer_t layer_;
    bool inFrame_ = false;
    int pending_ = 0;
    bool clipped_ = false;
    lv_area_t clipArea_ = {0, 0, 0, 0};
};
