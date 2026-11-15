#include "plugins/PluginCard.h"

#include "hardware/Input.h"
#include "plugins/PluginConnection.h"
#include "plugins/PluginManager.h"

namespace {

// How long after the stack last called update() the card still counts as on screen.
const uint32_t kActiveWindowMs = 300;

}  // namespace

PluginCard::PluginCard(PluginManager& manager, lv_obj_t* parent, const std::string& key, int width, int height)
    : manager_(manager), key_(key), gfx_(parent, width, height) {
    if (!PluginManager::splitKey(key, connectionId_, cardId_)) {
        connectionId_ = key;
        cardId_ = "1";
    }
}

PluginCard::~PluginCard() { manager_.cardDestroyed(this); }

bool PluginCard::onScreen() const { return everActive_ && millis() - lastActiveMs_ < kActiveWindowMs; }

bool PluginCard::update() {
    everActive_ = true;
    lastActiveMs_ = millis();
    return true;
}

void PluginCard::wantFrames(uint32_t forMs) {
    wanted_ = true;
    wantedUntilMs_ = millis() + forMs;
}

bool PluginCard::framesWanted() const { return wanted_ && static_cast<int32_t>(wantedUntilMs_ - millis()) > 0; }

void PluginCard::paintIfNeeded() {
    if (!connection_) return;
    deskhog::LuaPlugin& lua = connection_->lua();

    if (!lua.hasCard(cardId_)) {
        // The plugin no longer offers this kind of card. Say so, once.
        if (paintedMissing_) return;
        paintedMissing_ = true;
        gfx_.beginFrame();
        gfx_.fillRect(0, 0, gfx_.width(), gfx_.height(), 0x000000, 0);
        gfx_.text(8, 6, "Card unavailable", 0xE74C3C, deskhog::Font::Small, deskhog::Align::Left);
        gfx_.text(8, 28, "The plugin no longer has\nthis kind of card.", 0xCCCCCC, deskhog::Font::Small,
                  deskhog::Align::Left);
        gfx_.endFrame();
        frames_++;
        return;
    }
    paintedMissing_ = false;

    if (!lua.needsDraw(cardId_)) return;
    gfx_.beginFrame();
    lua.draw(cardId_);
    gfx_.endFrame();
    frames_++;
}

bool PluginCard::handleButtonPress(uint8_t button_index) {
    if (!connection_ || button_index > Input::BUTTON_UP) return false;

    // Escape hatch: with the centre button held, up always goes to the previous
    // card, even if the plugin wants every press for itself. (Centre + down is
    // taken: the main loop uses that pair for power off.)
    if (button_index == Input::BUTTON_UP && buttons[Input::BUTTON_CENTER].isPressed()) return false;

    return connection_->lua().button(cardId_, static_cast<deskhog::Button>(button_index));
}
