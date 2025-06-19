#include "HomeAssistantParser.h"
#include <cstring>
#include <cstdlib>

HomeAssistantParser::HomeAssistantParser(const char* json) 
    : _doc(1024)  // Start with 1KB, will auto-expand if needed
    , _valid(false) {
    
    if (!json || strlen(json) == 0) {
        Serial.println("[HomeAssistantParser] Empty or null JSON provided");
        return;
    }

    DeserializationError error = deserializeJson(_doc, json);
    if (error) {
        Serial.printf("[HomeAssistantParser] JSON parse error: %s\n", error.c_str());
        return;
    }

    // Basic validation - check for required fields
    if (!_doc["entity_id"].is<const char*>() || !_doc["state"]) {
        Serial.println("[HomeAssistantParser] Missing required fields: entity_id or state");
        return;
    }

    _valid = true;
}

bool HomeAssistantParser::isValid() const {
    return _valid;
}

HomeAssistantParser::EntityType HomeAssistantParser::getEntityType() const {
    if (!_valid) return EntityType::ENTITY_NOT_SUPPORTED;

    const char* entity_id = _doc["entity_id"];
    if (!entity_id) return EntityType::ENTITY_NOT_SUPPORTED;

    // Determine type based on entity_id prefix
    if (strncmp(entity_id, "sensor.", 7) == 0) {
        // Check if it's a binary sensor by looking at device_class or state
        const char* device_class = _doc["attributes"]["device_class"];
        const char* state = _doc["state"];
        
        if (state && (strcmp(state, "on") == 0 || strcmp(state, "off") == 0 ||
                      strcmp(state, "true") == 0 || strcmp(state, "false") == 0)) {
            return EntityType::BINARY_SENSOR;
        }
        
        if (device_class && (strcmp(device_class, "motion") == 0 || 
                            strcmp(device_class, "door") == 0 ||
                            strcmp(device_class, "window") == 0)) {
            return EntityType::BINARY_SENSOR;
        }
        
        return EntityType::NUMERIC_SENSOR;
    }
    else if (strncmp(entity_id, "binary_sensor.", 14) == 0) {
        return EntityType::BINARY_SENSOR;
    }
    else if (strncmp(entity_id, "switch.", 7) == 0) {
        return EntityType::SWITCH;
    }
    else if (strncmp(entity_id, "light.", 6) == 0) {
        return EntityType::LIGHT;
    }

    return EntityType::ENTITY_NOT_SUPPORTED;
}

bool HomeAssistantParser::getEntityId(char* buffer, size_t bufferSize) const {
    if (!_valid || !buffer || bufferSize == 0) return false;
    
    const char* entity_id = _doc["entity_id"];
    if (!entity_id) return false;
    
    return copyJsonString(entity_id, buffer, bufferSize);
}

bool HomeAssistantParser::getFriendlyName(char* buffer, size_t bufferSize) const {
    if (!_valid || !buffer || bufferSize == 0) return false;
    
    const char* friendly_name = _doc["attributes"]["friendly_name"];
    if (!friendly_name) {
        // Fallback to entity_id if no friendly name
        return getEntityId(buffer, bufferSize);
    }
    
    return copyJsonString(friendly_name, buffer, bufferSize);
}

double HomeAssistantParser::getNumericState() const {
    if (!_valid) return 0.0;
    
    // Try to get as number first
    if (_doc["state"].is<double>()) {
        return _doc["state"].as<double>();
    }
    
    // If it's a string, try to parse it
    const char* state_str = _doc["state"];
    if (state_str) {
        char* endptr;
        double value = strtod(state_str, &endptr);
        
        // Check if the entire string was parsed (no trailing characters)
        if (*endptr == '\0' || *endptr == ' ') {
            return value;
        }
    }
    
    return 0.0;
}

bool HomeAssistantParser::getStateString(char* buffer, size_t bufferSize) const {
    if (!_valid || !buffer || bufferSize == 0) return false;
    
    const char* state = _doc["state"];
    if (!state) return false;
    
    return copyJsonString(state, buffer, bufferSize);
}

bool HomeAssistantParser::getUnitOfMeasurement(char* buffer, size_t bufferSize) const {
    if (!_valid || !buffer || bufferSize == 0) return false;
    
    const char* unit = _doc["attributes"]["unit_of_measurement"];
    if (!unit) {
        buffer[0] = '\0';  // Empty string if no unit
        return true;
    }
    
    return copyJsonString(unit, buffer, bufferSize);
}

bool HomeAssistantParser::getDeviceClass(char* buffer, size_t bufferSize) const {
    if (!_valid || !buffer || bufferSize == 0) return false;
    
    const char* device_class = _doc["attributes"]["device_class"];
    if (!device_class) {
        buffer[0] = '\0';  // Empty string if no device class
        return true;
    }
    
    return copyJsonString(device_class, buffer, bufferSize);
}

bool HomeAssistantParser::isNumericState() const {
    if (!_valid) return false;
    
    // Check if state is directly a number
    if (_doc["state"].is<double>()) {
        return true;
    }
    
    // Check if state string can be parsed as a number
    const char* state_str = _doc["state"];
    if (state_str) {
        char* endptr;
        strtod(state_str, &endptr);
        
        // Return true if the entire string was parsed (no trailing non-numeric characters)
        return (*endptr == '\0' || *endptr == ' ');
    }
    
    return false;
}

bool HomeAssistantParser::isAvailable() const {
    if (!_valid) return false;
    
    const char* state = _doc["state"];
    if (!state) return false;
    
    return (strcmp(state, "unavailable") != 0 && 
            strcmp(state, "unknown") != 0);
}

bool HomeAssistantParser::copyJsonString(const char* value, char* buffer, size_t bufferSize) const {
    if (!value || !buffer || bufferSize == 0) return false;
    
    size_t len = strlen(value);
    if (len >= bufferSize) {
        // String too long, truncate
        strncpy(buffer, value, bufferSize - 1);
        buffer[bufferSize - 1] = '\0';
        return false;  // Indicate truncation
    }
    
    strcpy(buffer, value);
    return true;
}
