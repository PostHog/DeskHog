#pragma once

#include <Arduino.h>

/** The type every card in the stack has. */
static constexpr const char* kPluginCardType = "PLUGIN";

/**
 * One card in the stack, as saved in settings.
 *
 * Every card is a plugin card: `type` is "PLUGIN" and `config` names it as
 * "<connection id>:<card id>". Firmware before 0.2 had card types of its own
 * (INSIGHT, FRIEND, ...). Those are read as they were saved, and
 * PluginManager turns them into plugin cards at boot.
 */
struct CardConfig {
    String type = kPluginCardType;
    String config;  ///< "<connection id>:<card id>"
    int order = 0;  ///< Position in the stack
    String name;    ///< What the user called it

    CardConfig() = default;
    CardConfig(const String& type, const String& config, int order, const String& name)
        : type(type), config(config), order(order), name(name) {}

    bool isPlugin() const { return type == kPluginCardType; }
};
