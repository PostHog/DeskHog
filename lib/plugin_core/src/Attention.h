#pragma once

// What happens when a plugin calls notify(). The plugin only asks; this decides.
//
//   - The card comes to the front if its "May come to the front" setting
//     allows it, but never while the card on screen is being played (it used a
//     button press in the last 5 seconds), and at most once per 30 seconds per
//     card. Requests that have to wait collapse into one.
//   - Otherwise the request waits until someone arrives at the card.
//   - Either way the card then shows the text as a banner for 4 seconds, and
//     the LED flashes when the request comes in.
//
// No Lua and no hardware in here, so the firmware, the simulator and the
// tests share it. Not thread safe: UI task only. Cards are named by any text
// the host likes; the firmware uses "<connection>:<card>".

#include <stdint.h>
#include <functional>
#include <string>
#include <vector>

#include "PluginHost.h"

namespace deskhog {

class Attention {
public:
    static const uint32_t kBannerMs = 4000;
    static const uint32_t kPlayingMs = 5000;    ///< How long after a used press a card counts as being played
    static const uint32_t kRateMs = 30000;      ///< Between two interruptions by the same card
    static const uint32_t kBetweenMs = 5000;    ///< Between two interruptions by different cards
    static const uint32_t kFlashOnMs = 250;     ///< The LED flashes kFlashes times, this long each
    static const uint32_t kFlashEveryMs = 500;
    static const int kFlashes = 3;
    static const uint32_t kDefaultLed = 0xFFFFFF;
    static const size_t kMaxText = 120;         ///< Bytes of banner text that are kept
    static const size_t kMaxPending = 16;

    /** A request that is waiting for its card to be on screen. */
    struct Pending {
        std::string key;
        std::string text;
        uint32_t sinceMs = 0;
        int count = 1;  ///< How many notify() calls it stands for
    };

    /** What the host should do now. */
    struct Actions {
        std::string show;                  ///< Bring this card to the front, if not ""
        std::vector<std::string> repaint;  ///< Cards whose banner appeared or went away
        bool led = false;                  ///< Light the LED in ledRgb for ledMs
        uint32_t ledRgb = 0;
        uint32_t ledMs = 0;
    };

    /**
     * A card asked for attention.
     * @param mayInterrupt The card's "May come to the front" setting, as it is now.
     */
    void request(const std::string& key, const Notice& notice, bool mayInterrupt, uint32_t nowMs);

    /**
     * Someone is using the card on screen: it used a button press, or a button
     * is being held. Also takes a banner away, so it is never in the way.
     */
    void busy(uint32_t nowMs);

    /**
     * Run every frame.
     * @param onScreen     The card on screen, or "" if that is not a plugin card.
     * @param mayInterrupt Asked for each waiting card: may it come to the front?
     */
    Actions tick(const std::string& onScreen, uint32_t nowMs, const std::function<bool(const std::string&)>& mayInterrupt);

    /** The banner to draw over this card right now, or nullptr. */
    const std::string* banner(const std::string& key) const;

    const std::vector<Pending>& pending() const { return pending_; }

    /** Forget everything about cards whose name starts with `prefix`. */
    void forget(const std::string& prefix);

private:
    struct Shown {
        std::string key;
        uint32_t atMs;
    };

    bool playing(uint32_t nowMs) const { return everBusy_ && nowMs - lastBusyMs_ < kPlayingMs; }
    bool canInterrupt(const std::string& key, uint32_t nowMs) const;
    void endBanner(Actions* actions);

    std::vector<Pending> pending_;
    std::vector<Shown> shown_;  ///< When each card last interrupted
    std::string onScreen_;      ///< As of the last tick()

    bool bannerOn_ = false;
    std::string bannerKey_;
    std::string bannerText_;
    uint32_t bannerSinceMs_ = 0;
    bool bannerChanged_ = false;  ///< busy() took it away; the next tick() repaints

    bool everBusy_ = false;
    uint32_t lastBusyMs_ = 0;
    bool everInterrupted_ = false;
    uint32_t lastInterruptMs_ = 0;

    int flashesLeft_ = 0;
    uint32_t flashRgb_ = 0;
    uint32_t nextFlashMs_ = 0;
};

/** Paint a notify() banner over a card: a bar along the top with up to two lines of text. */
void drawBanner(Gfx& gfx, const std::string& text);

/** Cut text to at most `maxBytes` without splitting a UTF-8 character. */
std::string clipUtf8(const std::string& text, size_t maxBytes);

}  // namespace deskhog
