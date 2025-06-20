#pragma once

#define ARDUINOJSON_DEFAULT_NESTING_LIMIT 20
#include <ArduinoJson.h>
#include <Arduino.h>

/**
 * @class HomeAssistantParser
 * @brief Parser for Home Assistant entity state data
 * 
 * Handles parsing and data extraction from Home Assistant entity state JSON responses.
 * Supports various entity types including numeric sensors, binary sensors, switches, 
 * lights, and cover entities with unit extraction and state information.
 * 
 * Example JSON structure:
 * {
 *   "entity_id": "sensor.outdoor_temperature",
 *   "state": "23.5",
 *   "attributes": {
 *     "unit_of_measurement": "°C",
 *     "friendly_name": "Outdoor Temperature",
 *     "device_class": "temperature"
 *   },
 *   "last_changed": "2025-06-19T10:30:00.000000+00:00",
 *   "last_updated": "2025-06-19T10:30:00.000000+00:00"
 * }
 */
class HomeAssistantParser {
public:
    /**
     * @enum EntityType
     * @brief Supported entity types for visualization
     */
    enum class EntityType {
        NUMERIC_SENSOR,       ///< Numeric sensor (temperature, humidity, etc.)
        BINARY_SENSOR,        ///< Binary sensor (on/off, true/false)
        SWITCH,               ///< Switch entity (on/off state)
        LIGHT,                ///< Light entity (on/off with brightness)
        COVER,                ///< Cover entity (blinds, garage doors, curtains, etc.)
        ENTITY_NOT_SUPPORTED  ///< Unsupported or unrecognized entity type
    };

    /**
     * @brief Constructor - parses JSON data
     * @param json Raw JSON string to parse
     * 
     * Initializes parser with JSON data and attempts to parse entity state.
     * Uses isValid() to check if parsing was successful.
     */
    HomeAssistantParser(const char* json);

    /**
     * @brief Destructor
     */
    ~HomeAssistantParser() = default;

    /**
     * @brief Check if JSON was parsed successfully
     * @return true if valid, false otherwise
     */
    bool isValid() const;

    /**
     * @brief Get entity type based on entity_id and attributes
     * @return EntityType enum value
     */
    EntityType getEntityType() const;

    /**
     * @brief Get entity ID
     * @param buffer Buffer to store entity ID
     * @param bufferSize Size of the buffer
     * @return true if successful, false otherwise
     */
    bool getEntityId(char* buffer, size_t bufferSize) const;

    /**
     * @brief Get friendly name from attributes
     * @param buffer Buffer to store friendly name
     * @param bufferSize Size of the buffer
     * @return true if successful, false otherwise
     */
    bool getFriendlyName(char* buffer, size_t bufferSize) const;

    /**
     * @brief Get numeric state value
     * @return Numeric value if parseable, 0.0 if not numeric
     */
    double getNumericState() const;

    /**
     * @brief Get state as string
     * @param buffer Buffer to store state string
     * @param bufferSize Size of the buffer
     * @return true if successful, false otherwise
     */
    bool getStateString(char* buffer, size_t bufferSize) const;

    /**
     * @brief Get unit of measurement from attributes
     * @param buffer Buffer to store unit
     * @param bufferSize Size of the buffer
     * @return true if successful, false otherwise
     */
    bool getUnitOfMeasurement(char* buffer, size_t bufferSize) const;

    /**
     * @brief Get device class from attributes
     * @param buffer Buffer to store device class
     * @param bufferSize Size of the buffer
     * @return true if successful, false otherwise
     */
    bool getDeviceClass(char* buffer, size_t bufferSize) const;

    /**
     * @brief Check if entity state is numeric
     * @return true if state can be parsed as a number
     */
    bool isNumericState() const;

    /**
     * @brief Check if entity is available/online
     * @return true if state is not "unavailable" or "unknown"
     */
    bool isAvailable() const;

    /**
     * @brief Get cover position (0-100 percentage)
     * @return Position percentage, -1 if not available or not a cover
     */
    int getCoverPosition() const;

    /**
     * @brief Get cover tilt position (0-100 percentage)
     * @return Tilt position percentage, -1 if not available or not a cover
     */
    int getCoverTiltPosition() const;

    /**
     * @brief Check if cover is opening
     * @return true if cover state is "opening"
     */
    bool isCoverOpening() const;

    /**
     * @brief Check if cover is closing
     * @return true if cover state is "closing"
     */
    bool isCoverClosing() const;

    /**
     * @brief Check if cover is open
     * @return true if cover state is "open"
     */
    bool isCoverOpen() const;

    /**
     * @brief Check if cover is closed
     * @return true if cover state is "closed"
     */
    bool isCoverClosed() const;

private:
    DynamicJsonDocument _doc;  ///< JSON document for parsing
    bool _valid;               ///< Parsing success flag
    
    /**
     * @brief Helper to safely copy string from JSON to buffer
     * @param value JSON string value
     * @param buffer Destination buffer
     * @param bufferSize Size of destination buffer
     * @return true if successful, false if truncated or failed
     */
    bool copyJsonString(const char* value, char* buffer, size_t bufferSize) const;
};
