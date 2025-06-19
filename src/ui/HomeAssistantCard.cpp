#include "HomeAssistantCard.h"
#include "Style.h"
#include "NumberFormat.h"
#include <algorithm>

HomeAssistantCard::HomeAssistantCard(lv_obj_t* parent, ConfigManager& config, EventQueue& eventQueue,
                                   const String& entityId, uint16_t width, uint16_t height)
    : _config(config)
    , _event_queue(eventQueue)
    , _entity_id(entityId)
    , _current_title("")
    , _card(nullptr)
    , _title_label(nullptr)
    , _value_label(nullptr)
    , _unit_label(nullptr)
    , _content_container(nullptr)
    , _current_type(HomeAssistantParser::EntityType::ENTITY_NOT_SUPPORTED) {
    
    Serial.printf("[HomeAssistantCard-%s] DEBUG: Creating HomeAssistant card with dimensions %dx%d\n", 
                  _entity_id.c_str(), width, height);
    
    _card = lv_obj_create(parent);
    if (!_card) {
        Serial.printf("[HomeAssistantCard-%s] CRITICAL: Failed to create card base object!\n", _entity_id.c_str());
        return;
    }
    lv_obj_set_size(_card, width, height);
    lv_obj_set_style_bg_color(_card, Style::backgroundColor(), 0);
    lv_obj_set_style_pad_all(_card, 0, 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_radius(_card, 0, 0);

    lv_obj_t* flex_col = lv_obj_create(_card);
    if (!flex_col) { 
        Serial.printf("[HomeAssistantCard-%s] CRITICAL: Failed to create flex_col!\n", _entity_id.c_str());
        return; 
    }
    lv_obj_set_size(flex_col, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_all(flex_col, 5, 0);
    lv_obj_set_style_pad_row(flex_col, 5, 0);
    lv_obj_set_flex_flow(flex_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(flex_col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(flex_col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(flex_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(flex_col, 0, 0);

    _title_label = lv_label_create(flex_col);
    if (!_title_label) { 
        Serial.printf("[HomeAssistantCard-%s] CRITICAL: Failed to create _title_label!\n", _entity_id.c_str());
        return; 
    }
    lv_obj_set_width(_title_label, lv_pct(100)); 
    lv_obj_set_style_text_color(_title_label, Style::labelColor(), 0);
    lv_obj_set_style_text_font(_title_label, Style::labelFont(), 0);
    lv_label_set_long_mode(_title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(_title_label, "Loading...");

    _content_container = lv_obj_create(flex_col);
    if (!_content_container) { 
        Serial.printf("[HomeAssistantCard-%s] CRITICAL: Failed to create _content_container!\n", _entity_id.c_str());
        return; 
    }
    lv_obj_set_width(_content_container, lv_pct(100));
    lv_obj_set_flex_grow(_content_container, 1);
    lv_obj_set_style_bg_opa(_content_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(_content_container, 0, 0);
    lv_obj_set_style_pad_all(_content_container, 0, 0);
    lv_obj_set_flex_flow(_content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_content_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create value and unit labels
    _value_label = lv_label_create(_content_container);
    if (!_value_label) {
        Serial.printf("[HomeAssistantCard-%s] CRITICAL: Failed to create _value_label!\n", _entity_id.c_str());
        return;
    }
    lv_obj_set_style_text_color(_value_label, Style::valueColor(), 0);
    lv_obj_set_style_text_font(_value_label, Style::valueFont(), 0);
    lv_obj_set_style_text_align(_value_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_value_label, "--");

    _unit_label = lv_label_create(_content_container);
    if (!_unit_label) {
        Serial.printf("[HomeAssistantCard-%s] CRITICAL: Failed to create _unit_label!\n", _entity_id.c_str());
        return;
    }
    lv_obj_set_style_text_color(_unit_label, Style::labelColor(), 0);
    lv_obj_set_style_text_font(_unit_label, Style::labelFont(), 0);
    lv_obj_set_style_text_align(_unit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(_unit_label, "");

    // Subscribe to Home Assistant entity state events
    _event_queue.subscribe([this](const Event& event) {
        if (event.type == EventType::HA_ENTITY_STATE_RECEIVED && event.insightId == _entity_id) {
            this->onEvent(event);
        }
    });
    
    Serial.printf("[HomeAssistantCard-%s] DEBUG: Card created successfully, subscribed to HA_ENTITY_STATE_RECEIVED events\n", 
                  _entity_id.c_str());
}

HomeAssistantCard::~HomeAssistantCard() {
    if (globalUIDispatch) {
        globalUIDispatch([card_obj = _card]() {
            if (card_obj && lv_obj_is_valid(card_obj)) {
                lv_obj_del_async(card_obj);
            }
        }, true);
    }
}

void HomeAssistantCard::onEvent(const Event& event) {
    Serial.printf("[HomeAssistantCard-%s] DEBUG: Event received - type: %d, entityId: %s, dataLength: %d\n", 
                  _entity_id.c_str(), (int)event.type, event.insightId.c_str(), event.jsonData.length());
    
    std::shared_ptr<HomeAssistantParser> parser = nullptr;
    if (event.jsonData.length() > 0) {
        Serial.printf("[HomeAssistantCard-%s] DEBUG: JSON data preview: %.100s%s\n", 
                      _entity_id.c_str(), event.jsonData.c_str(), 
                      event.jsonData.length() > 100 ? "..." : "");
        parser = std::make_shared<HomeAssistantParser>(event.jsonData.c_str());
    } else {
        Serial.printf("[HomeAssistantCard-%s] ERROR: Event received with no JSON data.\n", _entity_id.c_str());
        handleParsedData(nullptr);
        return;
    }
    handleParsedData(parser);
}

void HomeAssistantCard::handleParsedData(std::shared_ptr<HomeAssistantParser> parser) {
    Serial.printf("[HomeAssistantCard-%s] DEBUG: handleParsedData called\n", _entity_id.c_str());
    
    if (!parser || !parser->isValid()) {
        Serial.printf("[HomeAssistantCard-%s] ERROR: Invalid data or parse error. Parser: %s, Valid: %s\n", 
                      _entity_id.c_str(), 
                      parser ? "exists" : "null",
                      parser ? (parser->isValid() ? "true" : "false") : "N/A");
        if (globalUIDispatch) {
            globalUIDispatch([this]() {
                if(isValidObject(_title_label)) lv_label_set_text(_title_label, "Data Error");
                if(isValidObject(_value_label)) lv_label_set_text(_value_label, "--");
                if(isValidObject(_unit_label)) lv_label_set_text(_unit_label, "");
                _current_type = HomeAssistantParser::EntityType::ENTITY_NOT_SUPPORTED;
            }, true);
        }
        return;
    }

    if (!parser->isAvailable()) {
        Serial.printf("[HomeAssistantCard-%s] WARNING: Entity is unavailable.\n", _entity_id.c_str());
        if (globalUIDispatch) {
            globalUIDispatch([this]() {
                if(isValidObject(_title_label)) lv_label_set_text(_title_label, "Unavailable");
                if(isValidObject(_value_label)) lv_label_set_text(_value_label, "--");
                if(isValidObject(_unit_label)) lv_label_set_text(_unit_label, "");
            }, true);
        }
        return;
    }

    HomeAssistantParser::EntityType new_entity_type = parser->getEntityType();
    Serial.printf("[HomeAssistantCard-%s] DEBUG: Entity type detected: %d\n", _entity_id.c_str(), (int)new_entity_type);
    
    char friendly_name_buffer[64];
    if (!parser->getFriendlyName(friendly_name_buffer, sizeof(friendly_name_buffer))) {
        strcpy(friendly_name_buffer, _entity_id.c_str());
        Serial.printf("[HomeAssistantCard-%s] DEBUG: No friendly name found, using entity ID\n", _entity_id.c_str());
    } else {
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Friendly name: %s\n", _entity_id.c_str(), friendly_name_buffer);
    }
    String new_title(friendly_name_buffer);

    // Only dispatch title update event if the title has actually changed
    if (_current_title != new_title) {
        _current_title = new_title;
        _event_queue.publishEvent(Event::createTitleUpdateEvent(_entity_id, new_title));
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Title updated to: %s\n", _entity_id.c_str(), new_title.c_str());
    } else {
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Title unchanged: %s\n", _entity_id.c_str(), new_title.c_str());
    }

    Serial.printf("[HomeAssistantCard-%s] DEBUG: Dispatching UI update for entity type %d\n", 
                  _entity_id.c_str(), (int)new_entity_type);

    if (globalUIDispatch) {
        globalUIDispatch([this, new_entity_type, new_title, parser, id = _entity_id]() mutable {
            Serial.printf("[HomeAssistantCard-%s] DEBUG: UI update lambda executing\n", id.c_str());
            
            if (isValidObject(_title_label)) {
                lv_label_set_text(_title_label, new_title.c_str());
                Serial.printf("[HomeAssistantCard-%s] DEBUG: Title label updated\n", id.c_str());
            } else {
                Serial.printf("[HomeAssistantCard-%s] ERROR: Title label is invalid!\n", id.c_str());
            }

            _current_type = new_entity_type;

            switch (new_entity_type) {
                case HomeAssistantParser::EntityType::NUMERIC_SENSOR:
                    Serial.printf("[HomeAssistantCard-%s] DEBUG: Updating numeric display\n", id.c_str());
                    updateNumericDisplay(*parser, new_title);
                    break;
                case HomeAssistantParser::EntityType::BINARY_SENSOR:
                    Serial.printf("[HomeAssistantCard-%s] DEBUG: Updating binary display\n", id.c_str());
                    updateBinaryDisplay(*parser, new_title);
                    break;
                case HomeAssistantParser::EntityType::SWITCH:
                case HomeAssistantParser::EntityType::LIGHT:
                    Serial.printf("[HomeAssistantCard-%s] DEBUG: Updating switch/light display\n", id.c_str());
                    updateSwitchDisplay(*parser, new_title);
                    break;
                default:
                    Serial.printf("[HomeAssistantCard-%s] ERROR: Unsupported entity type %d.\n", 
                        id.c_str(), (int)new_entity_type);
                    if(isValidObject(_value_label)) lv_label_set_text(_value_label, "Unsupported");
                    if(isValidObject(_unit_label)) lv_label_set_text(_unit_label, "");
                    break;
            }

        }, true);
    } else {
        Serial.printf("[HomeAssistantCard-%s] ERROR: globalUIDispatch is null!\n", _entity_id.c_str());
    }
}

void HomeAssistantCard::updateNumericDisplay(const HomeAssistantParser& parser, const String& friendly_name) {
    Serial.printf("[HomeAssistantCard-%s] DEBUG: updateNumericDisplay called\n", _entity_id.c_str());
    
    if (parser.isNumericState()) {
        double value = parser.getNumericState();
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Numeric value: %f\n", _entity_id.c_str(), value);
        
        char value_buffer[32];
        formatNumericValue(value, value_buffer, sizeof(value_buffer));
        
        char unit_buffer[16];
        parser.getUnitOfMeasurement(unit_buffer, sizeof(unit_buffer));
        
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Formatted value: '%s', unit: '%s'\n", 
                      _entity_id.c_str(), value_buffer, unit_buffer);
        
        if (isValidObject(_value_label)) {
            lv_label_set_text(_value_label, value_buffer);
            Serial.printf("[HomeAssistantCard-%s] DEBUG: Value label updated\n", _entity_id.c_str());
        } else {
            Serial.printf("[HomeAssistantCard-%s] ERROR: Value label is invalid!\n", _entity_id.c_str());
        }
        if (isValidObject(_unit_label)) {
            lv_label_set_text(_unit_label, unit_buffer);
            Serial.printf("[HomeAssistantCard-%s] DEBUG: Unit label updated\n", _entity_id.c_str());
        } else {
            Serial.printf("[HomeAssistantCard-%s] ERROR: Unit label is invalid!\n", _entity_id.c_str());
        }
        
        Serial.printf("[HomeAssistantCard-%s] INFO: Updated numeric: %s %s\n", 
                     _entity_id.c_str(), value_buffer, unit_buffer);
    } else {
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Non-numeric state, getting state string\n", _entity_id.c_str());
        
        char state_buffer[32];
        parser.getStateString(state_buffer, sizeof(state_buffer));
        
        Serial.printf("[HomeAssistantCard-%s] DEBUG: State string: '%s'\n", _entity_id.c_str(), state_buffer);
        
        if (isValidObject(_value_label)) {
            lv_label_set_text(_value_label, state_buffer);
        }
        if (isValidObject(_unit_label)) {
            lv_label_set_text(_unit_label, "");
        }
    }
}

void HomeAssistantCard::updateBinaryDisplay(const HomeAssistantParser& parser, const String& friendly_name) {
    Serial.printf("[HomeAssistantCard-%s] DEBUG: updateBinaryDisplay called\n", _entity_id.c_str());
    
    char state_buffer[32];
    parser.getStateString(state_buffer, sizeof(state_buffer));
    
    Serial.printf("[HomeAssistantCard-%s] DEBUG: Raw binary state: '%s'\n", _entity_id.c_str(), state_buffer);
    
    // Convert common binary states to more user-friendly display
    String display_state = String(state_buffer);
    if (display_state == "on" || display_state == "true") {
        display_state = "ON";
    } else if (display_state == "off" || display_state == "false") {
        display_state = "OFF";
    } else if (display_state == "open") {
        display_state = "OPEN";
    } else if (display_state == "closed") {
        display_state = "CLOSED";
    }
    
    Serial.printf("[HomeAssistantCard-%s] DEBUG: Display state: '%s'\n", _entity_id.c_str(), display_state.c_str());
    
    if (isValidObject(_value_label)) {
        lv_label_set_text(_value_label, display_state.c_str());
        Serial.printf("[HomeAssistantCard-%s] DEBUG: Binary value label updated\n", _entity_id.c_str());
    } else {
        Serial.printf("[HomeAssistantCard-%s] ERROR: Binary value label is invalid!\n", _entity_id.c_str());
    }
    if (isValidObject(_unit_label)) {
        lv_label_set_text(_unit_label, "");
    }
    
    Serial.printf("[HomeAssistantCard-%s] INFO: Updated binary: %s\n", 
                 _entity_id.c_str(), display_state.c_str());
}

void HomeAssistantCard::updateSwitchDisplay(const HomeAssistantParser& parser, const String& friendly_name) {
    char state_buffer[32];
    parser.getStateString(state_buffer, sizeof(state_buffer));
    
    // Convert switch/light states to user-friendly display
    String display_state = String(state_buffer);
    if (display_state == "on") {
        display_state = "ON";
    } else if (display_state == "off") {
        display_state = "OFF";
    }
    
    if (isValidObject(_value_label)) {
        lv_label_set_text(_value_label, display_state.c_str());
    }
    if (isValidObject(_unit_label)) {
        lv_label_set_text(_unit_label, "");
    }
    
    Serial.printf("[HomeAssistantCard-%s] Updated switch/light: %s\n", 
                 _entity_id.c_str(), display_state.c_str());
}

void HomeAssistantCard::formatNumericValue(double value, char* buffer, size_t bufferSize) {
    // Use the existing NumberFormat utility if available, otherwise basic formatting
    if (value == (int)value && value < 1000) {
        // Integer values under 1000, no decimal places
        snprintf(buffer, bufferSize, "%.0f", value);
    } else if (abs(value) >= 1000) {
        // Large numbers, use K/M formatting if NumberFormat is available
        if (abs(value) >= 1000000) {
            snprintf(buffer, bufferSize, "%.1fM", value / 1000000.0);
        } else {
            snprintf(buffer, bufferSize, "%.1fK", value / 1000.0);
        }
    } else {
        // Decimal values, 1 decimal place
        snprintf(buffer, bufferSize, "%.1f", value);
    }
}

void HomeAssistantCard::clearContentContainer() {
    if (isValidObject(_content_container)) {
        lv_obj_clean(_content_container);
    }
}

bool HomeAssistantCard::isValidObject(lv_obj_t* obj) const {
    return obj && lv_obj_is_valid(obj);
}
