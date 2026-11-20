#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <string>

#include "plugins/LvglGfx.h"
#include "ui/InputHandler.h"

class PluginConnection;
class PluginManager;

/**
 * One plugin card in the card stack: the canvas a connection paints on, and
 * the place its button presses arrive.
 *
 * The plugin itself runs in the card's PluginConnection, which may serve
 * several cards. Everything here runs on the UI task.
 */
class PluginCard : public InputHandler {
public:
    /** @param key "<connection id>:<card id>", as stored in the card stack. */
    PluginCard(PluginManager& manager, lv_obj_t* parent, const std::string& key, int width, int height);
    ~PluginCard() override;

    /** False when there was no memory for the card's pixels. */
    bool ok() const { return gfx_.ok(); }
    lv_obj_t* getCard() const { return gfx_.canvas(); }
    const std::string& key() const { return key_; }
    const std::string& connectionId() const { return connectionId_; }
    const std::string& cardId() const { return cardId_; }
    LvglGfx& gfx() { return gfx_; }

    PluginConnection* connection() const { return connection_; }
    void setConnection(PluginConnection* connection) { connection_ = connection; }

    /** Is this the card on screen right now? */
    bool onScreen() const;
    /** Keep painting the card for a while even when it is not on screen (someone is looking at it in a browser). */
    void wantFrames(uint32_t forMs);
    bool framesWanted() const;
    /** Paint the card if its plugin has something new for it. */
    void paintIfNeeded();
    /** How many times the card has been painted since its connection started. */
    uint32_t frames() const { return frames_; }
    void resetFrames() { frames_ = 0; }

    bool handleButtonPress(uint8_t button_index) override;
    void prepareForRemoval() override { gfx_.canvasDeleted(); }
    /** Called by the card stack while this is the card on screen. */
    bool update() override;

private:
    PluginManager& manager_;
    std::string key_;
    std::string connectionId_;
    std::string cardId_;
    LvglGfx gfx_;
    PluginConnection* connection_ = nullptr;

    uint32_t frames_ = 0;
    bool everActive_ = false;
    uint32_t lastActiveMs_ = 0;
    bool wanted_ = false;
    uint32_t wantedUntilMs_ = 0;
    bool paintedMissing_ = false;
};
