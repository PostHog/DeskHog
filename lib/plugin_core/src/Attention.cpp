#include "Attention.h"

namespace deskhog {

std::string clipUtf8(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
    return text.substr(0, end);
}

bool Attention::canInterrupt(const std::string& key, uint32_t nowMs) const {
    if (playing(nowMs)) return false;
    if (everInterrupted_ && nowMs - lastInterruptMs_ < kBetweenMs) return false;
    for (const Shown& shown : shown_) {
        if (shown.key == key && nowMs - shown.atMs < kRateMs) return false;
    }
    return true;
}

void Attention::request(const std::string& key, const Notice& notice, bool mayInterrupt, uint32_t nowMs) {
    const bool onScreen = !key.empty() && key == onScreen_;
    Pending* entry = nullptr;
    for (Pending& candidate : pending_) {
        if (candidate.key == key) entry = &candidate;
    }
    const bool waiting = entry != nullptr;
    if (!entry) {
        if (pending_.size() >= kMaxPending) pending_.erase(pending_.begin());
        pending_.push_back(Pending());
        entry = &pending_.back();
        entry->key = key;
        entry->sinceMs = nowMs;
        entry->count = 0;
    }
    // Requests that wait collapse into one: the latest text wins.
    entry->count++;
    if (!notice.text.empty() || !waiting) entry->text = clipUtf8(notice.text, kMaxText);

    // The LED: in the plugin's colour if it gave one. Without one it only
    // flashes when the card cannot simply come to the front.
    const bool comesNow = onScreen || (mayInterrupt && !waiting && canInterrupt(key, nowMs));
    if (notice.hasLed || !comesNow) {
        flashRgb_ = notice.hasLed ? notice.ledRgb : kDefaultLed;
        if (flashesLeft_ == 0) nextFlashMs_ = nowMs;
        flashesLeft_ = kFlashes;
    }
}

void Attention::busy(uint32_t nowMs) {
    everBusy_ = true;
    lastBusyMs_ = nowMs;
    if (bannerOn_) {
        bannerOn_ = false;
        bannerChanged_ = true;
    }
}

void Attention::endBanner(Actions* actions) {
    if (!bannerOn_) return;
    bannerOn_ = false;
    if (actions) actions->repaint.push_back(bannerKey_);
}

Attention::Actions Attention::tick(const std::string& onScreen, uint32_t nowMs,
                                   const std::function<bool(const std::string&)>& mayInterrupt) {
    Actions actions;
    onScreen_ = onScreen;
    if (bannerChanged_) {
        bannerChanged_ = false;
        actions.repaint.push_back(bannerKey_);
    }
    if (bannerOn_ && (bannerKey_ != onScreen || nowMs - bannerSinceMs_ >= kBannerMs)) endBanner(&actions);

    // Someone is at a card that has something waiting: show it now.
    for (auto it = pending_.begin(); it != pending_.end(); ++it) {
        if (onScreen.empty() || it->key != onScreen) continue;
        const std::string text = it->text;
        pending_.erase(it);
        if (!text.empty()) {
            bannerOn_ = true;
            bannerKey_ = onScreen;
            bannerText_ = text;
            bannerSinceMs_ = nowMs;
            actions.repaint.push_back(onScreen);
        }
        break;
    }

    // Bring one waiting card to the front, the one that has waited longest.
    for (const Pending& waiting : pending_) {
        if (!canInterrupt(waiting.key, nowMs)) continue;
        if (mayInterrupt && !mayInterrupt(waiting.key)) continue;
        actions.show = waiting.key;
        everInterrupted_ = true;
        lastInterruptMs_ = nowMs;
        bool known = false;
        for (Shown& shown : shown_) {
            if (shown.key != waiting.key) continue;
            shown.atMs = nowMs;
            known = true;
        }
        if (!known) {
            if (shown_.size() >= kMaxPending) shown_.erase(shown_.begin());
            shown_.push_back(Shown{waiting.key, nowMs});
        }
        break;
    }

    if (flashesLeft_ > 0 && static_cast<int32_t>(nowMs - nextFlashMs_) >= 0) {
        flashesLeft_--;
        nextFlashMs_ = nowMs + kFlashEveryMs;
        actions.led = true;
        actions.ledRgb = flashRgb_;
        actions.ledMs = kFlashOnMs;
    }
    return actions;
}

const std::string* Attention::banner(const std::string& key) const {
    return bannerOn_ && bannerKey_ == key ? &bannerText_ : nullptr;
}

void Attention::forget(const std::string& prefix) {
    const auto matches = [&prefix](const std::string& key) { return key.compare(0, prefix.size(), prefix) == 0; };
    for (auto it = pending_.begin(); it != pending_.end();) it = matches(it->key) ? pending_.erase(it) : it + 1;
    for (auto it = shown_.begin(); it != shown_.end();) it = matches(it->key) ? shown_.erase(it) : it + 1;
    if (bannerOn_ && matches(bannerKey_)) bannerOn_ = false;
}

// ------------------------------------------------------------------ drawing

namespace {

const uint32_t kBannerFill = 0x1D1F27;
const uint32_t kBannerAccent = 0xF54E00;
const uint32_t kBannerText = 0xFFFFFF;

// The longest start of `text` (from `from`) that fits in `width`, cut at a
// space when there is one. Returns where the next line starts.
size_t fitLine(Gfx& gfx, const std::string& text, size_t from, int width, std::string& line) {
    size_t end = from;
    size_t lastSpace = std::string::npos;
    while (end < text.size() && text[end] != '\n') {
        size_t next = end + 1;
        while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) next++;
        if (gfx.textWidth(text.substr(from, next - from).c_str(), Font::Small) > width) break;
        if (text[end] == ' ') lastSpace = end;
        end = next;
    }
    size_t next = end;
    if (end < text.size() && text[end] == '\n') {
        next = end + 1;
    } else if (end < text.size()) {
        if (text[end] == ' ') {
            next = end + 1;  // the line ends exactly at a space
        } else if (lastSpace != std::string::npos && lastSpace > from) {
            end = lastSpace;
            next = lastSpace + 1;
        } else if (end == from) {
            // One character wider than the bar: take it anyway, or this never ends.
            end = from + 1;
            while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end++;
            next = end;
        }
    }
    line = text.substr(from, end - from);
    return next;
}

}  // namespace

void drawBanner(Gfx& gfx, const std::string& text) {
    if (text.empty()) return;
    const int margin = 4;
    const int pad = 8;
    const int stripe = 4;
    const int width = gfx.width() - 2 * margin;
    const int inner = width - stripe - 2 * pad;
    const int lineHeight = gfx.textHeight(Font::Small);

    std::string first, second;
    size_t next = fitLine(gfx, text, 0, inner, first);
    if (next < text.size()) {
        next = fitLine(gfx, text, next, inner, second);
        if (next < text.size()) {
            // More than two lines: end the second with dots.
            while (!second.empty() && gfx.textWidth((second + "...").c_str(), Font::Small) > inner) {
                size_t cut = second.size() - 1;
                while (cut > 0 && (static_cast<unsigned char>(second[cut]) & 0xC0) == 0x80) cut--;
                second.erase(cut);
            }
            second += "...";
        }
    }
    const int lines = second.empty() ? 1 : 2;
    const int height = lines * lineHeight + 2 * 6;

    gfx.clip(0, 0, 0, 0);  // whatever the plugin left set
    // An orange box with a dark one inside it: a thin outline all round and a
    // thicker edge on the left, so the bar stands out on any card.
    gfx.fillRect(margin, margin, width, height, kBannerAccent, 6);
    gfx.fillRect(margin + stripe, margin + 1, width - stripe - 1, height - 2, kBannerFill, 5);
    gfx.text(margin + stripe + pad, margin + 6, first.c_str(), kBannerText, Font::Small, Align::Left);
    if (lines == 2) {
        gfx.text(margin + stripe + pad, margin + 6 + lineHeight, second.c_str(), kBannerText, Font::Small, Align::Left);
    }
}

}  // namespace deskhog
