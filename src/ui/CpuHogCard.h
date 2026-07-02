#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include "ui/InputHandler.h"
#include "hardware/CpuMonitor.h"

/**
 * @class CpuHogCard
 * @brief A desk-ornament card: Max runs on a wheel, faster as CPU load rises.
 *
 * Reuses the walking sprite animation from FriendCard, but instead of a fixed
 * loop duration the run speed is driven live by CpuMonitor - idle CPU gives a
 * lazy amble, a pegged CPU gives a frantic sprint. A small label shows the
 * current load percentage.
 *
 * The animation speed is bucketed (see kBucketPct) so we only restart the
 * LVGL animation when the pace changes meaningfully, avoiding per-frame jitter.
 */
class CpuHogCard : public InputHandler {
public:
    /**
     * @param parent  LVGL parent object
     * @param cpu     Shared CPU load monitor (not owned by this card)
     */
    CpuHogCard(lv_obj_t* parent, CpuMonitor* cpu);
    ~CpuHogCard();

    lv_obj_t* getCard() const { return _card; }

    bool handleButtonPress(uint8_t button_index) override;
    bool update() override;  ///< Polls CPU load and retunes the run speed
    void prepareForRemoval() override { _card = nullptr; }

private:
    // Run-speed mapping: loop duration in ms at 0% and 100% CPU. Shorter loop
    // means the six-frame walk cycle plays faster, i.e. the hog runs faster.
    static constexpr int kSlowestMs = 1500;  ///< Idle amble
    static constexpr int kFastestMs = 180;   ///< Full-tilt sprint
    static constexpr int kBucketPct = 5;     ///< Only retune per 5% load change
    static constexpr uint32_t kPollIntervalMs = 250;  ///< CPU sampling cadence

    bool isValidObject(lv_obj_t* obj) const;
    void applySpeed(uint8_t loadPercent);
    void setLabel(uint8_t loadPercent, bool fromHost);
    void pollHostFeed();  ///< Read "CPU:nn" lines from USB serial (Mac feed)

    CpuMonitor* _cpu;          ///< Borrowed CPU monitor
    lv_obj_t* _card;           ///< Root container
    lv_obj_t* _background;     ///< Green rounded panel
    lv_obj_t* _anim_img;       ///< Walking sprite animation
    lv_obj_t* _label;          ///< "CPU nn%" text
    lv_obj_t* _label_shadow;   ///< Drop shadow for the label

    int _currentBucket;        ///< Last applied speed bucket (-1 == unset)
    uint32_t _lastPollMs;      ///< millis() of last CPU poll

    // Line assembly for the "CPU:nn" host feed on USB serial.
    char _lineBuf[24];
    uint8_t _lineLen;
};
