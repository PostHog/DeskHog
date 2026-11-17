#pragma once

#include <Arduino.h>

#include <map>
#include <string>
#include <vector>

#include "LuaPlugin.h"
#include "PluginHost.h"
#include "plugins/LvglGfx.h"

class PluginCard;
class PluginManager;
struct PluginConnectionRecord;

/**
 * A running connection: one Lua program, serving every card of the connection
 * that is in the card stack. It is also what the program can reach on the
 * device (PluginServices).
 *
 * UI task only.
 */
class PluginConnection : public deskhog::PluginServices {
public:
    PluginConnection(PluginManager& manager, const std::string& id, uint32_t runtimeId, int cardWidth, int cardHeight);
    ~PluginConnection() override;

    const std::string& id() const { return id_; }
    const std::string& plugin() const { return plugin_; }
    uint32_t runtimeId() const { return runtimeId_; }
    deskhog::LuaPlugin& lua() { return lua_; }
    const std::vector<PluginCard*>& cards() const { return cards_; }

    /**
     * Load the plugin from storage and run it, without any cards yet.
     * @param record The saved connection, or nullptr if it no longer exists.
     */
    void start(const PluginConnectionRecord* record);
    /** Start again and give every attached card back to the new program. */
    void restart(const PluginConnectionRecord* record);

    /** A card for this connection appeared in the card stack. */
    void attach(PluginCard& card, const PluginConnectionRecord* record);
    /** That card left the stack. Its state is kept for a moment in case the stack is only being rebuilt. */
    void detach(PluginCard& card);
    /** Forget cards that stayed away. @return true when nothing is left and the connection can go. */
    bool cleanUp(uint32_t nowMs);
    /**
     * Ask the program for a list of choices (see LuaPlugin::choices). The
     * answer goes to PluginManager::finishChoices. Also keeps a connection
     * without cards running for a while: it was started just to answer this,
     * and the card it is being asked for is probably about to be added.
     */
    void askChoices(const std::string& function, const std::string& job, const deskhog::LuaPlugin::ChoiceContext& context);

    // PluginServices
    uint32_t millis() override;
    int64_t epochSeconds() override;
    bool buttonHeld(deskhog::Button button) override;
    void ledSet(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs) override;
    void ledOff() override;
    bool httpStart(int requestId, const deskhog::HttpRequest& request) override;
    bool storeGet(const std::string& key, std::string& json) override;
    bool storeSet(const std::string& key, const std::string& json) override;
    void log(const std::string& line) override;
    std::string timeZone() override;
    deskhog::NetStatus netStatus() override;
    uint32_t entropy() override;
    void notify(const std::string& cardId, const deskhog::Notice& notice) override;
    void choicesDone(int requestId, const std::vector<deskhog::Choice>& choices, const std::string& error) override;

private:
    void addToProgram(PluginCard& card, const PluginConnectionRecord* record);
    void releaseLed();

    PluginManager& manager_;
    std::string id_;
    std::string plugin_;
    uint32_t runtimeId_;
    LvglMetrics metrics_;
    deskhog::LuaPlugin lua_;

    std::vector<PluginCard*> cards_;
    std::map<std::string, uint32_t> detachedAtMs_;  ///< Card id -> when its card left the stack
    uint32_t emptySinceMs_ = 0;
    std::map<std::string, std::string> store_;
    bool ledHeld_ = false;  ///< This connection set the LED with no time limit
    std::map<int, std::string> choiceRequests_;  ///< Request id -> the manager's job that waits for it
    int nextChoiceRequest_ = 1;
    bool kept_ = false;        ///< Stay around without cards until keepUntilMs_
    uint32_t keepUntilMs_ = 0;
};
