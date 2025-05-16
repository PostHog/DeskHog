#include "CardNavigationStack.h"
#include "hardware/Input.h"
#include <algorithm> // Required for std::find and std::distance

// External button objects - defined in main.cpp
extern Bounce2::Button buttons[];

// Use the same NUM_BUTTONS definition as main.cpp
#define NUM_BUTTONS 3

CardNavigationStack::CardNavigationStack(lv_obj_t* parent, uint16_t width, uint16_t height)
    : _parent(parent), _width(width), _height(height), _current_card(0), _mutex_ptr(nullptr) {
    
    // Create main container
    _main_container = lv_obj_create(_parent);
    lv_obj_set_size(_main_container, _width - 7, _height);
    lv_obj_align(_main_container, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(_main_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_main_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_main_container, 0, 0);
    lv_obj_set_style_pad_all(_main_container, 0, 0);
    lv_obj_set_style_pad_row(_main_container, 0, 0);
    lv_obj_set_flex_flow(_main_container, LV_FLEX_FLOW_COLUMN);
    // lv_obj_add_event_cb(_main_container, _scroll_event_cb, LV_EVENT_SCROLL, this); // Pass this for context
    lv_obj_set_scroll_dir(_main_container, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(_main_container, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(_main_container, LV_SCROLLBAR_MODE_OFF);

    // Create scroll indicator container
    _scroll_indicator = lv_obj_create(_parent);
    lv_obj_set_size(_scroll_indicator, 2, _height);
    lv_obj_align(_scroll_indicator, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(_scroll_indicator, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_scroll_indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_scroll_indicator, 0, 0);
    lv_obj_set_style_pad_all(_scroll_indicator, 0, 0);
    lv_obj_set_style_pad_row(_scroll_indicator, 5, 0);
    lv_obj_set_flex_flow(_scroll_indicator, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_scroll_indicator, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(_scroll_indicator, LV_OBJ_FLAG_SCROLLABLE);
}

void CardNavigationStack::_update_pip_count() {
    uint32_t card_count = _handler_stack.size(); // MODIFIED
    uint32_t pip_count = lv_obj_get_child_cnt(_scroll_indicator);
    
    int32_t pip_height = (_height - ((card_count > 1 ? (card_count - 1) : 0) * 5)) / (card_count > 0 ? card_count : 1);
    
    while (pip_count < card_count) {
        lv_obj_t* pip = lv_obj_create(_scroll_indicator);
        lv_obj_set_size(pip, 2, pip_height);
        lv_obj_set_style_radius(pip, 0, 0);
        lv_obj_set_style_bg_color(pip, lv_color_hex(0x808080), 0);
        lv_obj_set_style_border_width(pip, 0, 0);
        pip_count++;
    }
    
    while (pip_count > card_count) {
        lv_obj_t* last_pip = lv_obj_get_child(_scroll_indicator, pip_count - 1);
        if (last_pip) {
            lv_obj_del_async(last_pip); // Use async delete
        }
        pip_count--;
    }
    
    for (uint32_t i = 0; i < pip_count; i++) {
        lv_obj_t* pip = lv_obj_get_child(_scroll_indicator, i);
        if (pip) {
            lv_obj_set_height(pip, pip_height);
        }
    }
    
    if (card_count >= 1) { // Ensure indicator is updated even for one card
      _update_scroll_indicator(_current_card);
    } else if (pip_count == 0 && card_count == 0) {
      // if no cards and no pips, nothing to do.
      // if pips exist but no cards (after removal), they are cleared above.
    }
}

void CardNavigationStack::addCard(InputHandler* handler, const char* name) { // MODIFIED
    if (!handler) return;
    lv_obj_t* card_obj = handler->getCardObject();
    if (!card_obj) return;

    lv_obj_set_parent(card_obj, _main_container);
    lv_obj_set_size(card_obj, _width - 7, _height);
    lv_obj_set_style_border_width(card_obj, 0, 0);
    lv_obj_set_style_pad_all(card_obj,0,0); // Ensure card itself has no padding to interfere with its content
    lv_obj_clear_flag(card_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card_obj, LV_OBJ_FLAG_SNAPPABLE);


    _handler_stack.push_back(handler);
    // _input_handlers.push_back(std::make_pair(card_obj, handler)); // Keep _input_handlers in sync if still used by old logic

    _update_pip_count();
    
    if (_handler_stack.size() == 1) {
        _current_card = 0;
        _update_scroll_indicator(_current_card);
        // lv_obj_scroll_to_view(card_obj, LV_ANIM_OFF); // Ensure first card is in view
    }
    if (name && strlen(name) > 0) {
         Serial.printf("[CNS] Added card: %s, Total cards: %d\n", name, _handler_stack.size());
    }
     goToCard(_current_card); // Ensure view is correct
}

bool CardNavigationStack::removeCard(InputHandler* handler) { // MODIFIED
    if (!handler) return false;

    auto it = std::find(_handler_stack.begin(), _handler_stack.end(), handler);
    if (it == _handler_stack.end()) {
        return false; 
    }

    int removed_idx = std::distance(_handler_stack.begin(), it);
    _handler_stack.erase(it);

    lv_obj_t* card_obj_to_remove = handler->getCardObject();
    if (card_obj_to_remove && lv_obj_is_valid(card_obj_to_remove) && lv_obj_get_parent(card_obj_to_remove) == _main_container) {
        lv_obj_del_async(card_obj_to_remove);
    }
    
    // _input_handlers: remove entry if it exists
    // auto& vec = _input_handlers; // alias for brevity
    // vec.erase(std::remove_if(vec.begin(), vec.end(),
    //                          [card_obj_to_remove](const std::pair<lv_obj_t*, InputHandler*>& pair){
    //                              return pair.first == card_obj_to_remove;
    //                          }), vec.end());


    if (_handler_stack.empty()) {
        _current_card = 0;
    } else {
        if (_current_card >= (uint8_t)_handler_stack.size()) {
             _current_card = (uint8_t)_handler_stack.size() - 1;
        }
        // If the removed card was before or was the current_card, and current_card was not 0,
        // it might need adjustment, but clamping above should suffice.
        // Or, if it was the last card, _current_card might now be out of bounds.
    }
    
    _update_pip_count(); // This will also call _update_scroll_indicator if cards remain
    
    if (!_handler_stack.empty()) {
        goToCard(_current_card); // Adjust view to the (potentially new) current card
    } else {
        // Clear any remaining pips if _update_pip_count didn't (e.g. if pips remained but cards are 0)
        while(lv_obj_get_child_cnt(_scroll_indicator) > 0) {
            lv_obj_del_async(lv_obj_get_child(_scroll_indicator, 0));
        }
    }
    Serial.printf("[CNS] Removed card. Remaining cards: %d, Current index: %d\n", _handler_stack.size(), _current_card);
    return true;
}


void CardNavigationStack::nextCard() {
    uint32_t card_count = _handler_stack.size(); // MODIFIED
    if (card_count == 0) return;
    
    uint8_t next_idx = (_current_card + 1) % card_count;
    goToCard(next_idx);
}

void CardNavigationStack::prevCard() {
    uint32_t card_count = _handler_stack.size(); // MODIFIED
    if (card_count == 0) return;
    
    uint8_t prev_idx = (_current_card > 0) ? _current_card - 1 : card_count - 1;
    goToCard(prev_idx);
}

void CardNavigationStack::goToCard(uint8_t index) {
    uint32_t card_count = _handler_stack.size(); // MODIFIED
    if (card_count == 0) { // No cards, nothing to do
        _current_card = 0; // Reset index
         _update_scroll_indicator(0); // Update indicator (will show no active pip)
        return;
    }
    if (index >= card_count) index = card_count -1; // Clamp index


    _current_card = index;
    
    InputHandler* target_handler = _handler_stack[_current_card];
    if (!target_handler) return;
    lv_obj_t* target_card_obj = target_handler->getCardObject();
    if (!target_card_obj || !lv_obj_is_valid(target_card_obj)) {
        Serial.printf("[CNS-ERROR] goToCard: Target card object for index %d is invalid.\n", _current_card);
        return;
    }
    
    // Scroll the main container to bring the target card into view
    // The y-coordinate calculation assumes all cards have the same height (_height)
    // and are direct children of _main_container arranged in a column.
    // lv_coord_t target_y = lv_obj_get_y(target_card_obj) - lv_obj_get_y(_main_container) + lv_obj_get_scroll_y(_main_container);
    // A simpler way if children are full height and snappable:
    lv_obj_scroll_to_view_recursive(target_card_obj, LV_ANIM_ON);


    // The scroll animation is handled by lv_obj_scroll_to_view_recursive or manual animation.
    // If using manual animation as before:
    /*
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _main_container);
    // Calculate target scroll position based on child position or index * height
    // This needs careful checking if cards have margins/padding or if _main_container has padding.
    // Assuming cards are _height and packed tightly:
    lv_coord_t target_y_scroll = _current_card * _height; 
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_scroll_to_y);
    lv_anim_set_values(&a, lv_obj_get_scroll_y(_main_container), target_y_scroll);
    lv_anim_set_time(&a, 200); // Animation time
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    */
    
    _update_scroll_indicator(_current_card); // Update pips after scroll
}

uint8_t CardNavigationStack::getCurrentIndex() const {
    return _current_card;
}

InputHandler* CardNavigationStack::getActiveCard() { // ADDED
    if (_handler_stack.empty() || _current_card >= _handler_stack.size()) {
        return nullptr;
    }
    return _handler_stack[_current_card];
}

void CardNavigationStack::setMutex(SemaphoreHandle_t* mutex_ptr) {
    _mutex_ptr = mutex_ptr;
}

void CardNavigationStack::handleButtonPress(uint8_t button_index) { // MODIFIED
    if (_mutex_ptr) {
        if (xSemaphoreTake(*_mutex_ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
            return; 
        }
    }
    
    bool handled_by_card = false;
    InputHandler* activeHandler = getActiveCard();
    if (activeHandler) {
        // Delegate any button press to the active card first
        if (activeHandler->handleButtonPress(button_index)) {
            handled_by_card = true;
        }
    }

    if (!handled_by_card) { // If not handled by card's specific logic, do default navigation
        if (button_index == Input::BUTTON_DOWN) {
            nextCard();
        } else if (button_index == Input::BUTTON_UP) {
            prevCard();
        }
        // If center button was not handled by the card, and it's not up/down, it does nothing by default here.
        // (Or, if it was up/down and not handled, it triggers navigation above)
    }
    
    if (_mutex_ptr) {
        xSemaphoreGive(*_mutex_ptr);
    }
}

// registerInputHandler is removed.

void CardNavigationStack::_scroll_event_cb(lv_event_t* e) {
    // This callback might not be needed if scroll snap is sufficient,
    // or if _update_scroll_indicator is called after goToCard.
    // CardNavigationStack* self = static_cast<CardNavigationStack*>(lv_event_get_user_data(e));
    // if (!self) return;

    // lv_obj_t* cont = lv_event_get_target(e);
    // lv_coord_t y = lv_obj_get_scroll_y(cont);
    // lv_coord_t card_height = self->_height; 
    // int current_idx = round((float)y / card_height);

    // if (current_idx >= 0 && current_idx < self->_handler_stack.size() && current_idx != self->_current_card) {
    //    self->_current_card = current_idx;
    //    self->_update_scroll_indicator(self->_current_card);
    // }
    // The original _scroll_event_cb set opacity, which we removed.
    // If scroll snapping is working, direct calls to _update_scroll_indicator from goToCard should be enough.
}

void CardNavigationStack::_update_scroll_indicator(int active_index) { // active_index is uint8_t _current_card
    uint32_t card_count = _handler_stack.size(); // MODIFIED
    uint32_t pip_count = lv_obj_get_child_cnt(_scroll_indicator);
    
    // if (card_count == 0 && pip_count > 0) { // Should be handled by _update_pip_count
    //     while(lv_obj_get_child_cnt(_scroll_indicator) > 0) lv_obj_del_async(lv_obj_get_child(_scroll_indicator,0));
    //     return;
    // }
    if (pip_count == 0 && card_count > 0) { // Pips might not be created yet
        _update_pip_count(); // Create them
        pip_count = lv_obj_get_child_cnt(_scroll_indicator); // Re-fetch
    }
     if (pip_count != card_count && card_count > 0) { // Mismatch, force pip update
        _update_pip_count();
        pip_count = lv_obj_get_child_cnt(_scroll_indicator);
    }


    for (uint32_t i = 0; i < pip_count; i++) {
        lv_obj_t* pip = lv_obj_get_child(_scroll_indicator, i);
        if (pip) {
            if (i == (uint32_t)active_index && card_count > 0) { // Only highlight if cards exist
                lv_obj_set_style_bg_color(pip, lv_color_white(), 0);
            } else {
                lv_obj_set_style_bg_color(pip, lv_color_hex(0x808080), 0);
            }
        }
    }
    if (card_count == 0 && pip_count > 0) { // No cards, but pips somehow exist ensure all grey
         for (uint32_t i = 0; i < pip_count; i++) {
            lv_obj_t* pip = lv_obj_get_child(_scroll_indicator, i);
            if(pip) lv_obj_set_style_bg_color(pip, lv_color_hex(0x808080), 0);
         }
    }
}

// Destructor might be needed if _handler_stack stores raw pointers that CardNavigationStack owns
// However, CardController creates and owns the card objects (InputHandlers)
// CardNavigationStack just references them.
// So, CardNavigationStack's destructor doesn't need to delete them.
// CardController's destructor handles deleting provisioningCard, animationCard, pongCard, and insightCards.
CardNavigationStack::~CardNavigationStack() {
    // Children of _main_container and _scroll_indicator are LVGL objects.
    // LVGL can handle their deletion if _parent is deleted, or they can be deleted manually.
    // Since _main_container and _scroll_indicator are children of _parent passed in constructor,
    // their lifecycle is tied to _parent.
    // No explicit deletion of _handler_stack elements as they are owned by CardController.
    _handler_stack.clear(); 
    // _input_handlers.clear(); // If it were still used

    // Explicitly delete main containers if they are not auto-deleted by LVGL with parent.
    // Usually, lv_obj_del_async(_main_container) etc. would be here if CNS owned them directly
    // but they are children of `_parent`.
}

