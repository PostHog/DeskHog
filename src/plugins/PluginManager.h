#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Attention.h"
#include "LuaPlugin.h"
#include "PluginHost.h"
#include "config/CardConfig.h"
#include "plugins/LvglGfx.h"

class ConfigManager;
class DisplayInterface;
class EventQueue;
class NeoPixelController;
class PluginCard;
class PluginConnection;

// The three levels:
//
//   plugin      the code, a Lua file on flash                    posthog.lua
//   connection  a plugin plus one set of shared settings         "PostHog · Work"
//   card        one screen of a connection, with its own         "Signups today"
//               settings
//
// A plugin that defines several kinds of card (a `cards` table) can show many
// cards per connection; they share one running program. A plain single-card
// plugin gets a connection of its own for every card, so two copies of a game
// do not share their state.
//
// A card in the card stack is stored as "<connection id>:<card id>".

using PluginSettings = deskhog::LuaPlugin::Settings;

struct PluginCardRecord {
    std::string id;
    std::string kind;
    PluginSettings settings;
    /// "May come to the front": whether notify() may put this card on screen.
    /// Every plugin card has this setting; it is the host's, not the plugin's.
    bool mayInterrupt = true;
};

/** A connection as saved on flash. */
struct PluginConnectionRecord {
    std::string id;
    std::string plugin;
    std::string name;
    PluginSettings settings;
    std::vector<PluginCardRecord> cards;
    int nextCard = 1;

    const PluginCardRecord* card(const std::string& cardId) const {
        for (const PluginCardRecord& record : cards) {
            if (record.id == cardId) return &record;
        }
        return nullptr;
    }
};

/** What a plugin file says about itself. */
struct PluginMeta {
    std::string title;
    std::string description;  ///< From the plugin's `plugin` table, as are the next two
    std::string version;
    std::string author;
    uint32_t hash = 0;  ///< Fingerprint of the source, see SourceHash.h
    std::string error;  ///< Set when the file does not even load
    bool multiCard = false;
    bool message = false;  ///< It has a global message(), for messages pushed to a connection
    std::vector<deskhog::LuaPlugin::ConfigField> config;
    std::vector<deskhog::LuaPlugin::CardKind> kinds;
};

struct PluginInfo {
    String name;
    size_t size = 0;
};

/** How a running card is doing, for the web API. */
struct PluginCardState {
    uint32_t frames = 0;  ///< Times painted since the connection started
    bool onScreen = false;
    std::string title;    ///< What the plugin calls the card, if it says
};

struct PluginConnectionState {
    bool running = false;
    bool failed = false;
    std::string error;
    size_t memory = 0;
    std::map<std::string, PluginCardState> cards;
};

/** A notify() that waits for someone to arrive at its card, for the web API. */
struct PluginNotification {
    std::string key;   ///< "<connection>:<card>"
    std::string text;
    int count = 1;     ///< How many notify() calls it stands for
    uint32_t ageMs = 0;
};

/** Memory and stack headroom, for the web API. Sizes are in bytes. */
struct PluginSystemInfo {
    size_t heapFree = 0;         ///< Internal RAM free now
    size_t heapLowest = 0;       ///< The least internal RAM that was ever free
    size_t psramFree = 0;
    size_t uiStackUnused = 0;    ///< Stack the UI task (which runs plugins) has never touched
    size_t httpStackUnused = 0;  ///< The same for the network task
    size_t lvglHeapTotal = 0;
    size_t lvglHeapPeak = 0;     ///< The most of LVGL's own heap ever in use
    size_t cards = 0;
};

/**
 * Owns everything around Lua plugins that is not the Lua runtime itself:
 * plugin files and saved connections on flash, the running connections and
 * their cards, their network requests, and what the web API reports.
 *
 * Threading: plugins only ever run on the UI task. The web server and the
 * network task never touch a plugin. They change the saved records (under a
 * lock) and leave commands and results in queues, which tick() works through
 * on the UI task.
 */
class PluginManager {
public:
    static constexpr size_t MAX_SOURCE_BYTES = 64 * 1024;
    static constexpr size_t MAX_RESPONSE_BYTES = 48 * 1024;
    /// For requests that keep only part of the answer (the `only` option of http.get).
    static constexpr size_t MAX_FILTERED_RESPONSE_BYTES = 256 * 1024;
    static constexpr size_t MAX_LOG_LINES = 30;
    static constexpr size_t MAX_STORE_KEYS = 32;
    static constexpr size_t MAX_STORE_BYTES = 8 * 1024;  ///< A connection's saved data, in total
    static constexpr size_t MAX_PLUGIN_CARDS = 12;
    static constexpr size_t MAX_CONNECTIONS = 16;
    static constexpr size_t MAX_MESSAGE_BYTES = 4096;  ///< A message pushed to a plugin
    static constexpr size_t MAX_QUEUED_MESSAGES = 8;   ///< Per connection, waiting for the UI task
    /// All messages waiting together. They sit in internal RAM, which WiFi needs too.
    static constexpr size_t MAX_QUEUED_MESSAGE_BYTES = 16 * 1024;
    static constexpr size_t MAX_CHOICE_JOBS = 4;       ///< Lists of choices being fetched or waiting to be picked up

    PluginManager(ConfigManager& configManager, NeoPixelController& led, EventQueue& eventQueue);

    /** Mount storage, install the bundled plugins on first boot, start the network task. */
    void begin();
    void setDisplayInterface(DisplayInterface* display) { display_ = display; }
    /** How to bring a card to the front. Called on the UI task with the LVGL mutex held. */
    void setShowCard(std::function<void(lv_obj_t*)> showCard) { showCard_ = std::move(showCard); }

    // ---- UI task only -------------------------------------------------

    /** Create the card stored as `key`. Call with the LVGL mutex held. */
    PluginCard* createCard(lv_obj_t* parent, const String& key, int width, int height);
    void cardDestroyed(PluginCard* card);

    /** Run once per UI loop, without the LVGL mutex held. */
    void tick();

    // ---- Called by running connections, on the UI task -----------------

    bool enqueueHttp(uint32_t connectionRuntimeId, int requestId, const deskhog::HttpRequest& request);
    void appendLog(const std::string& plugin, const std::string& line);
    NeoPixelController& led() { return led_; }
    /** A plugin called notify() for the card stored as `key`. */
    void requestAttention(const std::string& key, const deskhog::Notice& notice);
    /** The card on screen used a button press: it is being played. */
    void pressUsed();
    /** The notify() banner to paint over a card right now, or nullptr. */
    const std::string* banner(const std::string& key) const { return attention_.banner(key); }
    /** A plugin answered (or failed to answer) a request for a list of choices. */
    void finishChoices(const std::string& job, const std::vector<deskhog::Choice>& choices, const std::string& error);
    std::map<std::string, std::string> loadStore(const std::string& connectionId);
    bool saveStore(const std::string& connectionId, const std::map<std::string, std::string>& values);

    // ---- Safe from any task -------------------------------------------

    // Plugin files
    std::vector<PluginInfo> list();
    bool exists(const String& name);
    bool readSource(const String& name, String& out);
    /**
     * Save a plugin and restart everything that runs it.
     * @param addFirstCard If nothing shows the plugin yet, add a card for it, so
     *                     whoever is writing it sees it run. Installing from a
     *                     plugin directory passes false: there the person adds
     *                     the cards they want, with the settings they want.
     */
    bool writeSource(const String& name, const uint8_t* data, size_t length, String& error, bool addFirstCard = true);
    /** Remove a plugin with its connections and cards. */
    bool remove(const String& name);
    std::map<std::string, PluginMeta> metas();
    std::vector<String> logLines(const String& plugin);

    // Connections and cards
    std::vector<PluginConnectionRecord> connections();
    bool connection(const std::string& id, PluginConnectionRecord& out);
    /** @return the new connection's id, or "" with `error` set. */
    std::string createConnection(const std::string& plugin, const std::string& name, const PluginSettings& settings,
                                 String& error);
    /** Change a connection's name and settings. Settings given replace saved ones; others stay. */
    bool updateConnection(const std::string& id, const std::string* name, const PluginSettings& settings);
    /** Delete a connection and take its cards out of the card stack. */
    bool removeConnection(const std::string& id);
    /**
     * Add a card to the end of the card stack.
     * @param connectionId An existing connection, or "" to make a new one for `plugin`.
     * @return the card's key ("<connection>:<card>"), or "" with `error` set.
     */
    std::string addCard(const std::string& plugin, const std::string& connectionId, const std::string& connectionName,
                        const PluginSettings& connectionSettings, const std::string& kind, const std::string& cardName,
                        const PluginSettings& cardSettings, String& error, bool mayInterrupt = true);
    /** @param mayInterrupt The card's "May come to the front" setting, or nullptr to leave it. */
    bool updateCard(const std::string& key, const std::string* cardName, const PluginSettings& cardSettings,
                    const bool* mayInterrupt = nullptr);
    /**
     * Save the card stack: `keys` are "<TYPE>|<config>" in the new order. Cards
     * that are not listed are removed.
     */
    bool saveLayout(const std::vector<std::string>& keys);

    /** The card stack as saved, built-in cards included, in order. */
    std::vector<CardConfig> cardStack();

    /**
     * Where the web page and the command line tool look for plugins to install:
     * "owner/repo" on GitHub, or the address of an index.json. The device itself
     * never fetches it.
     */
    String directorySource();
    static const char* defaultDirectorySource();
    /** Give "" to go back to the default. */
    bool setDirectorySource(const String& source, String& error);

    /**
     * The device's time zone, which plugins tell local time by: a POSIX TZ
     * string. "UTC0" until one is picked.
     */
    std::string timeZone();
    /** The name the zone was picked by on the web page, e.g. "Europe/Brussels". May be "". */
    std::string timeZoneName();
    /**
     * @param posix A POSIX TZ string; "" goes back to UTC.
     * @param name  What to show for it. The device does not check that the two belong together.
     */
    bool setTimeZone(const String& posix, const String& name, String& error);

    // Live state
    std::map<std::string, PluginConnectionState> states();
    PluginSystemInfo systemInfo();
    void storageInfo(size_t& usedBytes, size_t& totalBytes);
    /**
     * Copy a card's pixels (RGB565). Also asks for the card to be kept painted for a while.
     * @param frames Set to how often the card was painted so far: a picture's version.
     * @param have   The version the caller already has. If the card is still at it,
     *               nothing is copied and `out` stays empty.
     */
    bool copyCardPixels(const std::string& key, std::vector<uint8_t>& out, int& width, int& height,
                        uint32_t& frames, const uint32_t* have = nullptr);
    /** Press a button for a card, as if on the device. `button` is 0 down, 1 centre, 2 up. */
    bool press(const std::string& key, uint8_t button);
    /** Bring a card to the front. */
    bool show(const std::string& key);
    /**
     * Hand a message to a plugin: to one card (its kind's message(card, msg))
     * or, with `cardId` "", to the connection (the global message(msg)). It is
     * delivered on the UI task a moment later.
     * @return an HTTP status: 200 queued, 404 nothing there to take it, 409 the
     *         connection is not running, 429 too many are waiting. `error` says more.
     */
    int sendMessage(const std::string& connectionId, const std::string& cardId, const std::string& body, String& error);
    /**
     * The list a plugin offers for a setting that has `choices`. Fetching it
     * takes the plugin a while (it may ask the network), and happens on the UI
     * task. So the first call only starts it and returns 202; ask again until
     * the answer is there.
     * @param kind The card kind the setting belongs to, or "" for a setting of the connection.
     * @param form What the form that asks holds right now, saved or not. The plugin's
     *             function gets it as its second argument. Asking again for the
     *             same list has to give the same `form`.
     * @return an HTTP status: 200 with `out` filled, 202 still working, 404 no
     *         such connection or setting, 429 too busy, 502 the plugin could not say.
     */
    int choices(const std::string& connectionId, const std::string& kind, const std::string& field,
                const PluginSettings& form, std::vector<deskhog::Choice>& out, String& error);
    /** Requests from notify() that are waiting for someone to arrive at their card. */
    std::vector<PluginNotification> notifications();
    /** The key of the first card that runs `plugin`, or "". */
    std::string firstCardOf(const std::string& plugin);

    static bool splitKey(const std::string& key, std::string& connectionId, std::string& cardId);

private:
    friend class PluginConnection;

    struct HttpJob {
        uint32_t runtimeId;
        int requestId;
        deskhog::HttpRequest request;
    };
    struct HttpResult {
        uint32_t runtimeId;
        int requestId;
        deskhog::HttpResponse response;
    };
    enum class CommandKind { PluginChanged, ConnectionChanged, RefreshAll, Press, Show, Repaint, Message, Choices };
    struct Command {
        CommandKind kind;
        std::string target;
        uint8_t button = 0;  ///< For Press: which button. For PluginChanged: 1 to add a first card.
        std::string card;    ///< For Message: the card it is for, or "" for the connection
        std::string body;    ///< For Message: what was sent. For Choices: the plugin's function
        std::string job;     ///< For Choices: which entry of choiceJobs_ waits for the answer
        deskhog::LuaPlugin::ChoiceContext context;  ///< For Choices: the form that asks
    };
    struct ChoiceJob {
        bool done = false;
        std::vector<deskhog::Choice> choices;
        std::string error;
        uint32_t atMs = 0;  ///< When it started, then when it finished
    };

    static void httpTaskEntry(void* arg);
    void httpLoop();
    void perform(const deskhog::HttpRequest& request, deskhog::HttpResponse& response);
    void startNetworkServices();

    void installBundledPlugins();
    void loadConnections();
    void migrateOldCards();
    void migrateBuiltInPostHog();
    void pruneOrphans();
    void inspectPlugin(const std::string& name);
    void runCommand(const Command& command);
    void restart(PluginConnection& connection);
    PluginConnection* runningConnection(const std::string& id);
    PluginCard* runningCard(const std::string& key);
    void publishStates(uint32_t now);
    void queue(CommandKind kind, const std::string& target, uint8_t button = 0);
    void runAttention(uint32_t now);
    bool mayInterrupt(const std::string& key);
    void startChoices(const Command& command);

    // Record helpers. Call with lock_ held.
    bool saveRecord(const PluginConnectionRecord& record);
    std::string freeConnectionId(const std::string& plugin) const;
    void deleteRecordFiles(const std::string& id);

    static String sourcePath(const String& name) { return "/plugins/" + name + ".lua"; }
    static String connectionPath(const std::string& id) { return String("/connections/") + id.c_str() + ".json"; }
    static String storePath(const std::string& id) { return String("/data/") + id.c_str() + ".json"; }

    ConfigManager& configManager_;
    NeoPixelController& led_;
    EventQueue& eventQueue_;
    DisplayInterface* display_ = nullptr;
    std::function<void(lv_obj_t*)> showCard_;
    bool storageReady_ = false;

    // UI task only
    std::vector<std::unique_ptr<PluginConnection>> running_;
    std::vector<PluginCard*> cards_;
    uint32_t nextRuntimeId_ = 1;
    int cardWidth_ = 233;
    int cardHeight_ = 135;
    bool inspectedAll_ = false;
    deskhog::Attention attention_;  ///< What becomes of notify(): see Attention.h
    bool wifiWasUp_ = false;
    uint32_t lastWifiCheckMs_ = 0;
    uint32_t lastMeasureMs_ = 0;
    uint32_t lastPublishMs_ = 0;

    // Network task
    TaskHandle_t httpTask_ = nullptr;
    QueueHandle_t jobs_ = nullptr;
    QueueHandle_t results_ = nullptr;
    std::atomic<bool> networkServicesWanted_{false};
    bool networkServicesStarted_ = false;

    std::atomic<uint32_t> uiStackUnused_{0};
    std::atomic<uint32_t> lvglHeapTotal_{0};
    std::atomic<uint32_t> lvglHeapPeak_{0};
    std::atomic<uint32_t> cardCount_{0};

    SemaphoreHandle_t lock_ = nullptr;  ///< Guards everything below
    std::vector<Command> commands_;
    std::map<std::string, PluginConnectionRecord> records_;
    std::map<std::string, PluginMeta> metas_;
    std::map<std::string, PluginConnectionState> states_;
    std::map<std::string, std::deque<std::string>> logs_;
    std::map<std::string, ChoiceJob> choiceJobs_;  ///< By "<connection>/<function>"
    std::vector<PluginNotification> notifications_;
    std::string timeZone_ = "UTC0";
    std::string timeZoneName_;
};
