#include "ui/CpuHogCard.h"
#include "Style.h"
#include "sprites/sprites.h"
#include <string.h>
#include <stdlib.h>

CpuHogCard::CpuHogCard(lv_obj_t* parent, CpuMonitor* cpu)
    : _cpu(cpu)
    , _card(nullptr)
    , _background(nullptr)
    , _anim_img(nullptr)
    , _label(nullptr)
    , _label_shadow(nullptr)
    , _currentBucket(-1)
    , _lastPollMs(0)
    , _lineLen(0) {

    // Root card - black background, matching the other cards.
    _card = lv_obj_create(parent);
    if (!_card) return;
    lv_obj_set_width(_card, lv_pct(100));
    lv_obj_set_height(_card, lv_pct(100));
    lv_obj_set_style_bg_color(_card, lv_color_black(), 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_pad_all(_card, 5, 0);
    lv_obj_set_style_margin_all(_card, 0, 0);

    // Green rounded panel.
    _background = lv_obj_create(_card);
    if (!_background) return;
    lv_obj_set_style_radius(_background, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(_background, lv_color_hex(0x0E7A00), 0);
    lv_obj_set_style_border_width(_background, 0, 0);
    lv_obj_set_style_pad_all(_background, 5, 0);
    lv_obj_set_width(_background, lv_pct(100));
    lv_obj_set_height(_background, lv_pct(100));

    // Walking hog animation (same sprite set as FriendCard).
    _anim_img = lv_animimg_create(_background);
    if (!_anim_img) return;
    lv_animimg_set_src(_anim_img, (const void**)walking_sprites, walking_sprites_count);
    lv_animimg_set_repeat_count(_anim_img, LV_ANIM_REPEAT_INFINITE);
    lv_img_set_zoom(_anim_img, 512);  // 256 = 100%, 512 = 200%
    lv_obj_align(_anim_img, LV_ALIGN_LEFT_MID, -10, 0);

    // Label + drop shadow for the CPU readout.
    _label_shadow = lv_label_create(_background);
    if (_label_shadow) {
        lv_obj_set_style_text_font(_label_shadow, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(_label_shadow, lv_color_black(), 0);
        lv_obj_set_style_text_align(_label_shadow, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(_label_shadow, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_label_shadow, lv_pct(60));
        lv_obj_align(_label_shadow, LV_ALIGN_RIGHT_MID, 0, 1);
    }
    _label = lv_label_create(_background);
    if (_label) {
        lv_obj_set_style_text_font(_label, Style::loudNoisesFont(), 0);
        lv_obj_set_style_text_color(_label, lv_color_white(), 0);
        lv_obj_set_style_text_align(_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(_label, lv_pct(60));
        lv_obj_align(_label, LV_ALIGN_RIGHT_MID, -1, 0);
    }

    // Start at whatever the CPU is doing right now.
    uint8_t load = _cpu ? _cpu->getLoadPercent() : 0;
    applySpeed(load);
    setLabel(load, _cpu && _cpu->hasFreshHostLoad());
}

CpuHogCard::~CpuHogCard() {
    if (isValidObject(_card)) {
        lv_obj_add_flag(_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(_card);
        _card = nullptr;
        _background = nullptr;
        _anim_img = nullptr;
        _label = nullptr;
        _label_shadow = nullptr;
    }
}

bool CpuHogCard::isValidObject(lv_obj_t* obj) const {
    return obj != nullptr;
}

void CpuHogCard::applySpeed(uint8_t loadPercent) {
    if (!isValidObject(_anim_img)) return;

    // Bucket the load so we only restart the animation on a real pace change.
    int bucket = loadPercent / kBucketPct;
    if (bucket == _currentBucket) return;
    _currentBucket = bucket;

    // Map load 0..100 -> duration kSlowestMs..kFastestMs (higher load = shorter
    // loop = faster run).
    int duration = kSlowestMs -
        ((kSlowestMs - kFastestMs) * (int)loadPercent) / 100;
    if (duration < kFastestMs) duration = kFastestMs;
    if (duration > kSlowestMs) duration = kSlowestMs;

    lv_animimg_set_duration(_anim_img, duration);
    lv_animimg_start(_anim_img);  // Restart with the new pace
}

void CpuHogCard::setLabel(uint8_t loadPercent, bool fromHost) {
    char buf[16];
    // "MAC" when a live host feed is driving it, "CPU" for the device's own load.
    snprintf(buf, sizeof(buf), "%s %u%%", fromHost ? "MAC" : "CPU", (unsigned)loadPercent);
    if (isValidObject(_label)) lv_label_set_text(_label, buf);
    if (isValidObject(_label_shadow)) lv_label_set_text(_label_shadow, buf);
}

void CpuHogCard::pollHostFeed() {
    // Drain any bytes from USB serial, assembling newline-terminated lines of
    // the form "CPU:nn" streamed by the Mac companion script.
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (_lineLen > 0) {
                _lineBuf[_lineLen] = '\0';
                if (strncmp(_lineBuf, "CPU:", 4) == 0) {
                    int v = atoi(_lineBuf + 4);
                    if (v < 0) v = 0;
                    if (v > 100) v = 100;
                    if (_cpu) _cpu->setHostLoad((uint8_t)v);
                }
                _lineLen = 0;
            }
        } else if (_lineLen < sizeof(_lineBuf) - 1) {
            _lineBuf[_lineLen++] = c;
        } else {
            _lineLen = 0;  // Overlong garbage; resync on next newline.
        }
    }
}

bool CpuHogCard::update() {
    // Called on the UI task while this card is active, so LVGL calls are safe.
    if (!_cpu) return true;

    // Poll the serial feed every tick so we don't drop bytes.
    pollHostFeed();

    uint32_t now = millis();
    if (now - _lastPollMs < kPollIntervalMs) {
        return true;  // Keep receiving updates, but don't over-sample the pace.
    }
    _lastPollMs = now;

    uint8_t load = _cpu->getLoadPercent();
    applySpeed(load);
    setLabel(load, _cpu->hasFreshHostLoad());
    return true;
}

bool CpuHogCard::handleButtonPress(uint8_t button_index) {
    // No interaction needed - it's a desk ornament. Let the nav stack handle
    // navigation buttons.
    return false;
}
