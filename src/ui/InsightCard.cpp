#include "InsightCard.h"
#include "Style.h"
#include "NumberFormat.h"
#include <algorithm>
#include "renderers/NumericCardRenderer.h"
#include "renderers/LineGraphRenderer.h"
#include "renderers/FunnelRenderer.h"
#include "hardware/Input.h"
#include "../AsyncNetworkManager.h"


InsightCard::InsightCard(lv_obj_t* parent, ConfigManager& config, EventQueue& eventQueue,
                        const String& insightId, uint16_t width, uint16_t height)
    : _config(config)
    , _event_queue(eventQueue)
    , _insight_id(insightId)
    , _current_title("")
    , _card(nullptr)
    , _title_label(nullptr)
    , _content_container(nullptr)
    , _loading_spinner(nullptr)
    , _error_label(nullptr)
    , _is_loading(false)
    , _has_error(false)
    , _is_showing_cached_data(false)
    , _active_renderer(nullptr)
    , _current_type(InsightParser::InsightType::INSIGHT_NOT_SUPPORTED) {
    
    // NOTE: UI queue is now initialized by CardController

    _card = lv_obj_create(parent);
    if (!_card) {
        Serial.printf("[InsightCard-%s] CRITICAL: Failed to create card base object!\n", _insight_id.c_str());
        return;
    }
    lv_obj_set_size(_card, width, height);
    lv_obj_set_style_bg_color(_card, Style::backgroundColor(), 0);
    lv_obj_set_style_pad_all(_card, 0, 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    lv_obj_set_style_radius(_card, 0, 0);

    lv_obj_t* flex_col = lv_obj_create(_card);
    if (!flex_col) { 
        Serial.printf("[InsightCard-%s] CRITICAL: Failed to create flex_col!\n", _insight_id.c_str());
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
        Serial.printf("[InsightCard-%s] CRITICAL: Failed to create _title_label!\n", _insight_id.c_str());
        return; 
    }
    lv_obj_set_width(_title_label, lv_pct(100)); 
    lv_obj_set_style_text_color(_title_label, Style::labelColor(), 0);
    lv_obj_set_style_text_font(_title_label, Style::labelFont(), 0);
    lv_label_set_long_mode(_title_label, LV_LABEL_LONG_DOT);
    lv_label_set_text(_title_label, "Loading...");

    _content_container = lv_obj_create(flex_col);
    if (!_content_container) { 
        Serial.printf("[InsightCard-%s] CRITICAL: Failed to create _content_container!\n", _insight_id.c_str());
        return; 
    }
    lv_obj_set_width(_content_container, lv_pct(100));
    lv_obj_set_flex_grow(_content_container, 1);
    lv_obj_set_style_bg_opa(_content_container, LV_OPA_0, 0);
    lv_obj_set_style_border_width(_content_container, 0, 0);
    lv_obj_set_style_pad_all(_content_container, 0, 0);

    _event_queue.subscribe([this](const Event& event) {
        if (event.insightId == _insight_id) {
            if (event.type == EventType::INSIGHT_DATA_RECEIVED) {
                this->onEvent(event);
            } else if (event.type == EventType::INSIGHT_DATA_ERROR) {
                this->onErrorEvent(event);
            } else if (event.type == EventType::INSIGHT_NETWORK_STATE_CHANGED) {
                this->onNetworkStateChanged(event);
            }
        }
    });
}

InsightCard::~InsightCard() {
    Serial.printf("[InsightCard-%s] DESTRUCTOR called\n", _insight_id.c_str());
    std::shared_ptr<InsightRendererBase> renderer_for_lambda = std::move(_active_renderer);
    if (globalUIDispatch) {
        globalUIDispatch([card_obj = _card, renderer = renderer_for_lambda]() mutable {
            if (renderer) {
                renderer->clearElements();
            }
            if (card_obj && lv_obj_is_valid(card_obj)) {
                lv_obj_del_async(card_obj);
            }
        }, true);
    }
}

void InsightCard::onEvent(const Event& event) {
    std::shared_ptr<InsightParser> parser = nullptr;
    if (event.jsonData.length() > 0) {
        parser = std::make_shared<InsightParser>(event.jsonData.c_str());
    } else if (event.parser) {
        parser = event.parser;
    } else {
        Serial.printf("[InsightCard-%s] Event received with no JSON data or pre-parsed object.\n", _insight_id.c_str());
        handleParsedData(nullptr);
        return;
    }
    handleParsedData(parser);
}

void InsightCard::handleParsedData(std::shared_ptr<InsightParser> parser) {
    if (!parser || !parser->isValid()) {
        Serial.printf("[InsightCard-%s] Invalid data or parse error.\n", _insight_id.c_str());
        if (globalUIDispatch) {
            globalUIDispatch([this]() {
                if(isValidObject(_title_label)) lv_label_set_text(_title_label, "Data Error");
                if (_active_renderer) {
                    _active_renderer->clearElements();
                    _active_renderer.reset();
                }
                _current_type = InsightParser::InsightType::INSIGHT_NOT_SUPPORTED;
            }, true);
        }
        return;
    }

    InsightParser::InsightType new_insight_type = parser->getInsightType();
    char title_buffer[64];
    if (!parser->getName(title_buffer, sizeof(title_buffer))) {
        strcpy(title_buffer, "Insight");
    }
    String new_title(title_buffer);

    // Only dispatch title update event if the title has actually changed
    if (_current_title != new_title) {
        _current_title = new_title;
        _event_queue.publishEvent(Event::createTitleUpdateEvent(_insight_id, new_title));
        Serial.printf("[InsightCard-%s] Title updated to: %s\n", _insight_id.c_str(), new_title.c_str());
    }

    if (globalUIDispatch) {
        globalUIDispatch([this, new_insight_type, new_title, parser, id = _insight_id]() mutable {
        if (isValidObject(_title_label)) {
            lv_label_set_text(_title_label, new_title.c_str());
        }

        bool needs_rebuild = false;
        if (new_insight_type != _current_type || !_active_renderer) {
            needs_rebuild = true;
        } else if (_active_renderer && !_active_renderer->areElementsValid()) {
            Serial.printf("[InsightCard-%s] Active renderer elements are invalid. Rebuilding.\n", id.c_str());
            needs_rebuild = true;
        }

        if (needs_rebuild) {
            Serial.printf("[InsightCard-%s] Rebuilding renderer START. Old type: %d, New type: %d. Core: %d, Card: %p, Container: %p\n", 
                id.c_str(), (int)_current_type, (int)new_insight_type, xPortGetCoreID(), _card, _content_container);

            if (_active_renderer) {
                _active_renderer->clearElements();
                _active_renderer.reset();
            }
            clearContentContainer();
            _current_type = new_insight_type;

            switch (new_insight_type) {
                case InsightParser::InsightType::NUMERIC_CARD:
                    _active_renderer = std::make_unique<NumericCardRenderer>();
                    break;
                case InsightParser::InsightType::LINE_GRAPH:
                    _active_renderer = std::make_unique<LineGraphRenderer>();
                    break;
                case InsightParser::InsightType::FUNNEL:
                    _active_renderer = std::make_unique<FunnelRenderer>();
                    break;
                default:
                    Serial.printf("[InsightCard-%s] Unsupported insight type %d. Using Numeric as fallback.\n", 
                        id.c_str(), (int)new_insight_type);
                    _active_renderer = std::make_unique<NumericCardRenderer>(); 
                    break;
            }

            if (_active_renderer) {
                _active_renderer->createElements(_content_container);
                if (isValidObject(_content_container)) {
                    lv_obj_invalidate(_content_container);
                }
                lv_display_t* disp = lv_display_get_default();
                if (disp) {
                    lv_refr_now(disp);
                }
            } else {
                Serial.printf("[InsightCard-%s] CRITICAL: Failed to create a renderer!\n", id.c_str());
            }
        }

        if (_active_renderer) {
            char prefix_buffer[16] = "";
            char suffix_buffer[16] = "";

            if (new_insight_type == InsightParser::InsightType::NUMERIC_CARD && parser) {
                parser->getNumericFormattingPrefix(prefix_buffer, sizeof(prefix_buffer));
                parser->getNumericFormattingSuffix(suffix_buffer, sizeof(suffix_buffer));
            }
            _active_renderer->updateDisplay(*parser, new_title, prefix_buffer, suffix_buffer);
        } else if (!needs_rebuild) {
            Serial.printf("[InsightCard-%s] No active renderer to update and no rebuild was triggered. Type: %d\n",
                id.c_str(), (int)_current_type);
        }

        }, true);
    }
}

void InsightCard::clearContentContainer() {
    if (isValidObject(_content_container)) {
        lv_obj_clean(_content_container);
    }
}

bool InsightCard::isValidObject(lv_obj_t* obj) const {
    return obj && lv_obj_is_valid(obj);
}

bool InsightCard::handleButtonPress(uint8_t button_index) {
    // Check if it's the center button
    if (button_index == Input::BUTTON_CENTER) {
        // Publish event to request a forced refresh
        Event refreshEvent;
        refreshEvent.type = EventType::INSIGHT_FORCE_REFRESH;
        refreshEvent.insightId = _insight_id;
        _event_queue.publishEvent(refreshEvent);
        
        // With async networking, no need to show "Refreshing" state
        // Fresh data will appear seamlessly when available
        
        Serial.printf("[InsightCard-%s] Force refresh requested\n", _insight_id.c_str());
        return true; // Event handled
    }
    
    return false; // Not handled, pass to default handler
}

void InsightCard::onErrorEvent(const Event& event) {
    Serial.printf("[InsightCard-%s] Error event received: %s\n", _insight_id.c_str(), event.jsonData.c_str());
    
    if (globalUIDispatch) {
        // Capture necessary objects to ensure they exist when lambda executes
        lv_obj_t* card_obj = _card;
        lv_obj_t* error_label = _error_label;
        lv_obj_t* loading_spinner = _loading_spinner;
        lv_obj_t* content_container = _content_container;
        
        globalUIDispatch([card_obj, error_label, loading_spinner, content_container, error_msg = event.jsonData]() mutable {
            // Validate that the card still exists before accessing child objects
            if (!card_obj || !lv_obj_is_valid(card_obj)) {
                return;
            }
            
            // Create error label if it doesn't exist and content container is valid
            if (!error_label && content_container && lv_obj_is_valid(content_container)) {
                error_label = lv_label_create(content_container);
                if (error_label) {
                    lv_obj_set_width(error_label, lv_pct(100));
                    lv_obj_center(error_label);
                    lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF3B30), 0); // Apple red
                    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_align(error_label, LV_TEXT_ALIGN_CENTER, 0);
                    lv_label_set_long_mode(error_label, LV_LABEL_LONG_WRAP);
                    lv_label_set_text(error_label, "Error loading data");
                    lv_obj_add_flag(error_label, LV_OBJ_FLAG_HIDDEN);
                }
            }
            
            // Show error state if error label exists and is valid
            if (error_label && lv_obj_is_valid(error_label)) {
                lv_label_set_text(error_label, error_msg.c_str());
                lv_obj_clear_flag(error_label, LV_OBJ_FLAG_HIDDEN);
                
                // Animate opacity transition
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, error_label);
                lv_anim_set_values(&a, 0, 255);
                lv_anim_set_time(&a, 200);
                lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
                    if (obj && lv_obj_is_valid((lv_obj_t*)obj)) {
                        lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
                    }
                });
                lv_anim_start(&a);
            }
            
            // Hide loading spinner if it exists and is valid
            if (loading_spinner && lv_obj_is_valid(loading_spinner)) {
                lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Set content container to full opacity if it exists and is valid
            if (content_container && lv_obj_is_valid(content_container)) {
                lv_obj_set_style_opa(content_container, LV_OPA_COVER, 0);
            }
        }, true);
    }
}

void InsightCard::onNetworkStateChanged(const Event& event) {
    Serial.printf("[InsightCard-%s] Network state changed: %s\n", _insight_id.c_str(), event.jsonData.c_str());
    
    // With async networking, we don't need to show loading states anymore
    // Data will be updated directly via onEvent when received
    if (globalUIDispatch) {
        // Capture necessary objects to ensure they exist when lambda executes
        lv_obj_t* card_obj = _card;
        lv_obj_t* loading_spinner = _loading_spinner;
        lv_obj_t* error_label = _error_label;
        lv_obj_t* content_container = _content_container;
        
        globalUIDispatch([card_obj, loading_spinner, error_label, content_container, state_str = event.jsonData]() {
            // Validate that the card still exists before accessing child objects
            if (!card_obj || !lv_obj_is_valid(card_obj)) {
                return;
            }
            
            if (state_str == "success") {
                // Hide loading spinner if it exists and is valid
                if (loading_spinner && lv_obj_is_valid(loading_spinner)) {
                    lv_obj_add_flag(loading_spinner, LV_OBJ_FLAG_HIDDEN);
                }
                
                // Hide error label if it exists and is valid
                if (error_label && lv_obj_is_valid(error_label)) {
                    lv_obj_add_flag(error_label, LV_OBJ_FLAG_HIDDEN);
                }
                
                // Set content container to full opacity if it exists and is valid
                if (content_container && lv_obj_is_valid(content_container)) {
                    lv_obj_set_style_opa(content_container, LV_OPA_COVER, 0);
                }
            } else if (state_str == "error") {
                // Error state will be handled by onErrorEvent
            }
            // Remove loading state handling since async networking provides immediate data
        }, true);
    }
}

void InsightCard::showLoadingState(bool show_spinner) {
    // Loading state no longer needed with async networking
    // Data updates happen immediately with cached data, then fresh data
    _is_loading = false;
    _has_error = false;
    
    // Hide any existing loading UI with proper validation
    if (_loading_spinner && lv_obj_is_valid(_loading_spinner)) {
        lv_obj_add_flag(_loading_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (_error_label && lv_obj_is_valid(_error_label)) {
        lv_obj_add_flag(_error_label, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Keep content at full opacity with proper validation
    if (_content_container && lv_obj_is_valid(_content_container)) {
        lv_obj_set_style_opa(_content_container, LV_OPA_COVER, 0);
    }
}

void InsightCard::showErrorState(const String& error_message) {
    _is_loading = false;
    _has_error = true;
    
    if (!_error_label) {
        createErrorDisplay();
    }
    
    if (_error_label && lv_obj_is_valid(_error_label)) {
        lv_label_set_text(_error_label, error_message.c_str());
        lv_obj_clear_flag(_error_label, LV_OBJ_FLAG_HIDDEN);
        animateOpacityTransition(_error_label, 0, 255, 200);
    }
    
    if (_loading_spinner && lv_obj_is_valid(_loading_spinner)) {
        animateOpacityTransition(_loading_spinner, 255, 0, 200);
        lv_obj_add_flag(_loading_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Restore content opacity with proper validation
    if (_content_container && lv_obj_is_valid(_content_container)) {
        lv_obj_set_style_opa(_content_container, LV_OPA_COVER, 0);
    }
}

void InsightCard::showSuccessState() {
    _is_loading = false;
    _has_error = false;
    _is_showing_cached_data = false;
    
    if (_loading_spinner && lv_obj_is_valid(_loading_spinner)) {
        animateOpacityTransition(_loading_spinner, 255, 0, 200);
        lv_obj_add_flag(_loading_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    
    if (_error_label && lv_obj_is_valid(_error_label)) {
        lv_obj_add_flag(_error_label, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Restore full content opacity with smooth transition
    if (_content_container && lv_obj_is_valid(_content_container)) {
        animateOpacityTransition(_content_container, lv_obj_get_style_opa(_content_container, 0), 255, 300);
    }
}

void InsightCard::createLoadingSpinner() {
    // Loading spinner no longer needed with async networking
    // Progressive loading provides immediate data display
    return;
}

void InsightCard::createErrorDisplay() {
    if (!_content_container || !lv_obj_is_valid(_content_container) || _error_label) {
        return;
    }
    
    _error_label = lv_label_create(_content_container);
    if (_error_label) {
        lv_obj_set_width(_error_label, lv_pct(100));
        lv_obj_center(_error_label);
        lv_obj_set_style_text_color(_error_label, lv_color_hex(0xFF3B30), 0); // Apple red
        lv_obj_set_style_text_font(_error_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(_error_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(_error_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(_error_label, "Error loading data");
        lv_obj_add_flag(_error_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void InsightCard::animateOpacityTransition(lv_obj_t* obj, uint8_t from_opacity, uint8_t to_opacity, uint32_t duration) {
    if (!obj || !lv_obj_is_valid(obj)) {
        return;
    }
    
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, from_opacity, to_opacity);
    lv_anim_set_time(&a, duration);
    lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
    });
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}