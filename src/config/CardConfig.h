#pragma once

#include <Arduino.h>
#include <functional>
#include <lvgl.h>
#include <vector>
#include <map>

/**
 * @brief Enum for different types of configuration fields
 */
enum class ConfigFieldType {
    TEXT,       ///< Single-line text input
    SELECT,     ///< Dropdown selection from predefined options
    TEXTAREA,   ///< Multi-line text input
    BOOLEAN     ///< Checkbox/toggle for true/false values
};

/**
 * @brief Represents a single configuration field definition
 */
struct ConfigField {
    String key;                         ///< Unique identifier for this config field
    String inputLabel;                  ///< Label shown to user for this field
    String uiDescription;               ///< Detailed description/help text
    ConfigFieldType type;               ///< Type of input field to render
    std::vector<String> options;       ///< Available options (for SELECT type)
    String defaultValue;                ///< Default value for the field
    bool required;                      ///< Whether this field is mandatory
    
    /**
     * @brief Default constructor
     */
    ConfigField() : key(""), inputLabel(""), uiDescription(""), type(ConfigFieldType::TEXT),
                   defaultValue(""), required(false) {}
    
    /**
     * @brief Constructor with parameters
     */
    ConfigField(const String& k, const String& label, const String& desc,
               ConfigFieldType t, const String& def = "", bool req = false)
        : key(k), inputLabel(label), uiDescription(desc), type(t),
          defaultValue(def), required(req) {}
    
    /**
     * @brief Constructor for SELECT type with options
     */
    ConfigField(const String& k, const String& label, const String& desc,
               const std::vector<String>& opts, const String& def = "", bool req = false)
        : key(k), inputLabel(label), uiDescription(desc), type(ConfigFieldType::SELECT),
          options(opts), defaultValue(def), required(req) {}
};

/**
 * @brief Enum to uniquely identify each type of card available in the system
 */
enum class CardType {
    INSIGHT,    ///< PostHog insight visualization card
    FRIEND      ///< Walking animation/encouragement card
    // New card types can be added here
};

/**
 * @brief Represents an instance of a configured card
 * 
 * This struct represents a card that has been added by the user and configured.
 * A list of these will be stored in persistent memory.
 */
struct CardConfig {
    CardType type;                      ///< The type of card (enum value)
    std::map<String, String> config;    ///< Dynamic configuration key-value pairs
    int order;                          ///< Display order in the card stack
    String name;                        ///< Human-readable name (e.g., "PostHog Insight", "Walking Animation")

    /**
     * @brief Default constructor
     */
    CardConfig() : type(CardType::INSIGHT), order(0), name("") {}
    
    /**
     * @brief Constructor with parameters
     */
    CardConfig(CardType t, const std::map<String, String>& c, int o, const String& n) 
        : type(t), config(c), order(o), name(n) {}
    
    
    /**
     * @brief Get a configuration value by key
     * @param key The configuration key
     * @param defaultValue Default value if key not found
     * @return The configuration value or default
     */
    String getConfig(const String& key, const String& defaultValue = "") const {
        auto it = config.find(key);
        return (it != config.end()) ? it->second : defaultValue;
    }
    
    /**
     * @brief Set a configuration value
     * @param key The configuration key
     * @param value The configuration value
     */
    void setConfig(const String& key, const String& value) {
        config[key] = value;
    }
};

/**
 * @brief Represents an available type of card that a user can choose to add
 * 
 * These will be defined in CardController and represent the "menu" of 
 * card types that users can select from in the web UI.
 */
struct CardDefinition {
    CardType type;                                  ///< The type of card this definition describes
    String name;                                    ///< Human-readable name (e.g., "PostHog Insight", "Walking Animation")
    bool allowMultiple;                             ///< Can the user add more than one of this card type?
    String uiDescription;                           ///< Description shown to user in web UI
    std::vector<ConfigField> configFields;         ///< Dynamic configuration fields for this card type
    
    // Factory function to create an instance of the card's UI
    std::function<lv_obj_t*(const std::map<String, String>& configValues)> factory;
    
    
    /**
     * @brief Default constructor
     */
    CardDefinition() : type(CardType::INSIGHT), name(""), allowMultiple(false), uiDescription("") {}
    
    /**
     * @brief Constructor with dynamic config fields
     */
    CardDefinition(CardType t, const String& n, bool multiple, const String& description,
                  const std::vector<ConfigField>& fields)
        : type(t), name(n), allowMultiple(multiple), uiDescription(description), configFields(fields) {}
    
    
    /**
     * @brief Check if this card type requires any configuration
     */
    bool needsConfiguration() const {
        return !configFields.empty();
    }
    
    /**
     * @brief Get default configuration values for this card type
     */
    std::map<String, String> getDefaultConfig() const {
        std::map<String, String> defaults;
        for (const auto& field : configFields) {
            if (!field.defaultValue.isEmpty()) {
                defaults[field.key] = field.defaultValue;
            }
        }
        return defaults;
    }
};

/**
 * @brief Helper function to convert CardType enum to string
 * @param type The CardType to convert
 * @return String representation of the card type
 */
inline String cardTypeToString(CardType type) {
    switch (type) {
        case CardType::INSIGHT: return "INSIGHT";
        case CardType::FRIEND: return "FRIEND";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Helper function to convert string to CardType enum
 * @param str The string to convert
 * @return CardType enum value, defaults to INSIGHT if string not recognized
 */
inline CardType stringToCardType(const String& str) {
    if (str == "INSIGHT") return CardType::INSIGHT;
    if (str == "FRIEND") return CardType::FRIEND;
    return CardType::INSIGHT; // Default fallback
}