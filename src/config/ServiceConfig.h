#pragma once

#include <Arduino.h>
#include <vector>
#include <map>

/**
 * @brief Enum to identify different service types
 */
enum class ServiceType {
    POSTHOG,
    HOME_ASSISTANT
    // New services can be added here
};

/**
 * @brief Configuration field types for services
 */
enum class ServiceFieldType {
    TEXT,           // Single-line text input
    PASSWORD,       // Hidden password fields
    SELECT,         // Dropdown selection from predefined options
    BOOLEAN         // Checkbox for true/false values
};

/**
 * @brief Individual configuration field for services
 */
struct ServiceField {
    String key;                         // Unique identifier for this config field
    String label;                       // Label shown to user for this field
    String description;                 // Detailed description/help text
    ServiceFieldType type;              // Type of input field to render
    String defaultValue;                // Default value for the field
    bool required;                      // Whether this field is mandatory
    bool sensitive;                     // For masking in UI (passwords, API keys)
    std::vector<String> options;       // Available options (for SELECT type)
    
    /**
     * @brief Default constructor
     */
    ServiceField() : type(ServiceFieldType::TEXT), required(false), sensitive(false) {}
    
    /**
     * @brief Constructor with parameters
     */
    ServiceField(const String& k, const String& l, const String& desc,
                ServiceFieldType t, const String& def = "", bool req = false, bool sens = false)
        : key(k), label(l), description(desc), type(t), defaultValue(def), required(req), sensitive(sens) {}
    
    /**
     * @brief Constructor for SELECT type with options
     */
    ServiceField(const String& k, const String& l, const String& desc,
                const std::vector<String>& opts, const String& def = "", bool req = false)
        : key(k), label(l), description(desc), type(ServiceFieldType::SELECT),
          options(opts), defaultValue(def), required(req), sensitive(false) {}
};

/**
 * @brief Service configuration instance
 */
struct ServiceConfig {
    ServiceType type;                   // Service type
    std::map<String, String> config;   // Dynamic configuration key-value pairs
    
    /**
     * @brief Default constructor
     */
    ServiceConfig() : type(ServiceType::POSTHOG) {}
    
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
 * @brief Service definition for available services
 */
struct ServiceDefinition {
    ServiceType type;                   // Service type
    String name;                        // Display name
    String description;                 // Service description
    std::vector<ServiceField> fields;  // Configuration fields
    
    /**
     * @brief Default constructor
     */
    ServiceDefinition() : type(ServiceType::POSTHOG) {}
    
    /**
     * @brief Constructor with parameters
     */
    ServiceDefinition(ServiceType t, const String& n, const String& desc,
                     const std::vector<ServiceField>& f)
        : type(t), name(n), description(desc), fields(f) {}
};

/**
 * @brief Helper function to convert ServiceType enum to string
 * @param type The ServiceType to convert
 * @return String representation of the service type
 */
inline String serviceTypeToString(ServiceType type) {
    switch (type) {
        case ServiceType::POSTHOG: return "POSTHOG";
        case ServiceType::HOME_ASSISTANT: return "HOME_ASSISTANT";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Helper function to convert string to ServiceType enum
 * @param str The string to convert
 * @return ServiceType enum value, defaults to POSTHOG if string not recognized
 */
inline ServiceType stringToServiceType(const String& str) {
    if (str == "POSTHOG") return ServiceType::POSTHOG;
    if (str == "HOME_ASSISTANT") return ServiceType::HOME_ASSISTANT;
    return ServiceType::POSTHOG; // Default fallback
}