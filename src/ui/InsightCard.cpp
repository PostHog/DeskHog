#include "InsightCard.h"
#include "Style.h"
#include "NumberFormat.h"
#include <algorithm>
#include "renderers/NumericCardRenderer.h"
#include "renderers/LineGraphRenderer.h"
#include "renderers/FunnelRenderer.h"


InsightCard::InsightCard(lv_obj_t* parent, ConfigManager& config, EventQueue& eventQueue,
                        const String& insightId, uint16_t width, uint16_t height)
    : _config(config)
    , _event_queue(eventQueue)
    , _insight_id(insightId)
    , _current_title("")
    , _card(nullptr)
    , _title_label(nullptr)
    , _content_container(nullptr)
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
        if (event.type == EventType::INSIGHT_DATA_RECEIVED && event.insightId == _insight_id) {
            this->onEvent(event);
        }
    });
}

InsightCard::~InsightCard() {
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
            Serial.printf("[InsightCard-%s] Rebuilding renderer. Old type: %d, New type: %d. Core: %d\n", 
                id.c_str(), (int)_current_type, (int)new_insight_type, xPortGetCoreID());

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