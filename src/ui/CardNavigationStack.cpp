#include "CardNavigationStack.h"
#include "hardware/Input.h"

// External button objects - defined in main.cpp
extern Bounce2::Button buttons[];

// Use the same NUM_BUTTONS definition as main.cpp
#define NUM_BUTTONS 3

CardNavigationStack::CardNavigationStack(lv_obj_t* parent, uint16_t width, uint16_t height)
    : _parent(parent), _width(width), _height(height), _current_card(0), _mutex_ptr(nullptr) {
    
    // Create main container
    _main_container = lv_obj_create(_parent);
    lv_obj_set_size(_main_container, _width - 7, _height);  // Reduced width to create 5px gap with indicator
    lv_obj_align(_main_container, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(_main_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_main_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_main_container, 0, 0);
    lv_obj_set_style_pad_left(_main_container, 0, 0);
    lv_obj_set_style_pad_right(_main_container, 0, 0);
    lv_obj_set_style_pad_top(_main_container, 0, 0);
    lv_obj_set_style_pad_bottom(_main_container, 0, 0);
    lv_obj_set_style_pad_row(_main_container, 0, 0);  // No gap between cards
    lv_obj_set_flex_flow(_main_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(_main_container, _scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_set_scroll_dir(_main_container, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(_main_container, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(_main_container, LV_SCROLLBAR_MODE_OFF);

    // Create scroll indicator container
    _scroll_indicator = lv_obj_create(_parent);
    lv_obj_set_size(_scroll_indicator, 2, _height);  // 2px wide container
    lv_obj_align(_scroll_indicator, LV_ALIGN_RIGHT_MID, 0, 0);  // Keep flush with right edge
    lv_obj_set_style_bg_color(_scroll_indicator, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_scroll_indicator, LV_OPA_TRANSP, 0);  // Make container transparent
    lv_obj_set_style_border_width(_scroll_indicator, 0, 0);
    lv_obj_set_style_pad_all(_scroll_indicator, 0, 0);
    lv_obj_set_style_pad_row(_scroll_indicator, 5, 0);  // 5px gap between pips
    lv_obj_set_flex_flow(_scroll_indicator, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_scroll_indicator, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(_scroll_indicator, LV_OBJ_FLAG_SCROLLABLE);     // Disable scrolling
}

void CardNavigationStack::_update_pip_count() {
    uint32_t card_count = lv_obj_get_child_cnt(_main_container);
    uint32_t pip_count = lv_obj_get_child_cnt(_scroll_indicator);
    
    // Ensure we have at least 1 card for calculations
    if (card_count == 0) {
        // Remove all pips if no cards
        while (pip_count > 0) {
            lv_obj_t* pip = lv_obj_get_child(_scroll_indicator, 0);
            if (pip) {
                lv_obj_del(pip);
            }
            pip_count--;
        }
        return;
    }
    
    // Calculate pip height based on card count
    // Height = (container height - (gaps between pips)) / number of pips
    int32_t total_gaps = (card_count > 1) ? ((card_count - 1) * 5) : 0;
    int32_t pip_height = (_height - total_gaps) / card_count;
    
    // Ensure minimum pip height
    if (pip_height < 2) pip_height = 2;
    
    // Remove all existing pips and recreate to ensure consistency
    while (pip_count > 0) {
        lv_obj_t* pip = lv_obj_get_child(_scroll_indicator, 0);
        if (pip) {
            lv_obj_del(pip);
        }
        pip_count--;
    }
    
    // Create new pips
    for (uint32_t i = 0; i < card_count; i++) {
        lv_obj_t* pip = lv_obj_create(_scroll_indicator);
        lv_obj_set_size(pip, 2, pip_height);  // 2px wide
        lv_obj_set_style_radius(pip, 0, 0);  // Rectangle shape
        lv_obj_set_style_bg_color(pip, lv_color_hex(0x808080), 0);  // Gray by default
        lv_obj_set_style_border_width(pip, 0, 0);
        lv_obj_clear_flag(pip, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // Make the first pip active if we just created pips
    if (_current_card < card_count) {
        lv_obj_t* active_pip = lv_obj_get_child(_scroll_indicator, _current_card);
        if (active_pip) {
            lv_obj_set_style_bg_color(active_pip, lv_color_white(), 0);
        }
    }
}

void CardNavigationStack::addCard(lv_obj_t* card) {
    // First, make the card a child of our container
    lv_obj_set_parent(card, _main_container);
    
    // Ensure the card fills the container properly with no borders/margins
    lv_obj_set_size(card, _width - 7, _height);
    // Don't override child padding/margin settings that might be needed for rounded corners
    lv_obj_set_style_border_width(card, 0, 0); // Remove borders
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    // Update the scroll indicator
    _update_pip_count();
    
    // Show the first card if this is the first addition
    if (lv_obj_get_child_cnt(_main_container) == 1) {
        _current_card = 0;
        _update_scroll_indicator(_current_card);
    }
}

void CardNavigationStack::nextCard() {
    uint32_t card_count = lv_obj_get_child_cnt(_main_container);
    if (card_count == 0) return;
    
    uint8_t next = (_current_card + 1) % card_count;
    goToCard(next);
}

void CardNavigationStack::prevCard() {
    uint32_t card_count = lv_obj_get_child_cnt(_main_container);
    if (card_count == 0) return;
    
    uint8_t prev = (_current_card > 0) ? _current_card - 1 : card_count - 1;
    goToCard(prev);
}

void CardNavigationStack::goToCard(uint8_t index) {
    uint32_t card_count = lv_obj_get_child_cnt(_main_container);
    if (index >= card_count) return;
    
    _current_card = index;
    lv_obj_t* target_card = lv_obj_get_child(_main_container, _current_card);
    if (!target_card) {
        Serial.printf("Error: Could not find card at index %d\n", _current_card);
        return;
    }
    
    // Calculate target scroll position based on child position
    lv_coord_t target_y = index * _height;
    
    // Create a custom animation
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, _main_container);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_scroll_to_y);
    lv_anim_set_values(&a, lv_obj_get_scroll_y(_main_container), target_y);
    lv_anim_set_time(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    
    // Short delay after animation
    vTaskDelay(pdMS_TO_TICKS(1));
    
    _update_scroll_indicator(_current_card);
}

uint8_t CardNavigationStack::getCurrentIndex() const {
    return _current_card;
}

uint32_t CardNavigationStack::getCardCount() const {
    return lv_obj_get_child_cnt(_main_container);
}

void CardNavigationStack::setMutex(SemaphoreHandle_t* mutex_ptr) {
    _mutex_ptr = mutex_ptr;
}

void CardNavigationStack::handleButtonPress(uint8_t button_index) {
    // Only handle button press if we have a mutex and can acquire it
    if (_mutex_ptr) {
        if (xSemaphoreTake(*_mutex_ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
            return; // Could not acquire mutex, skip this button press
        }
    }
    
    // Handle center button (Button 1) - delegate to active card if it has an input handler
    if (button_index == Input::BUTTON_CENTER) {
        // Find the input handler for the current card
        bool handled = false;
        for (const auto& handler_pair : _input_handlers) {
            if (handler_pair.first == lv_obj_get_child(_main_container, _current_card)) {
                // Found a handler for the current card
                if (handler_pair.second) {
                    handled = handler_pair.second->handleButtonPress(button_index);
                    if (handled) {
                        // Handler processed the button press, don't do default behavior
                        if (_mutex_ptr) {
                            xSemaphoreGive(*_mutex_ptr);
                        }
                        return;
                    }
                }
                break;
            }
        }
        // If not handled by card, fall through to default behavior (if any)
    }
    
    // Next card (Button 0)
    if (button_index == Input::BUTTON_DOWN) {
        nextCard();
    }
    // Previous card (Button 2)
    else if (button_index == Input::BUTTON_UP) {
        prevCard();
    }
    
    // Release mutex if we acquired it
    if (_mutex_ptr) {
        xSemaphoreGive(*_mutex_ptr);
    }
}

void CardNavigationStack::registerInputHandler(lv_obj_t* card, InputHandler* handler) {
    // Don't register null handlers
    if (!card || !handler) return;
    
    // Check if this card already has a handler
    for (auto& handler_pair : _input_handlers) {
        if (handler_pair.first == card) {
            // Update existing handler
            handler_pair.second = handler;
            return;
        }
    }
    
    // Add new handler
    _input_handlers.push_back(std::make_pair(card, handler));
}

void CardNavigationStack::forceUpdateIndicators() {
    // Force update pip count
    _update_pip_count();
    
    // Force update active indicator
    _update_scroll_indicator(_current_card);
    
    // Force LVGL to redraw the indicator container
    lv_obj_invalidate(_scroll_indicator);
}

void CardNavigationStack::_scroll_event_cb(lv_event_t* e) {
    lv_obj_t* cont = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_area_t cont_a;
    lv_obj_get_coords(cont, &cont_a);
    int32_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

    uint32_t child_cnt = lv_obj_get_child_cnt(cont);
    
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* child = lv_obj_get_child(cont, i);
        // Remove all opacity effects - everything is fully opaque
        lv_obj_set_style_opa(child, LV_OPA_COVER, 0);
    }
}

void CardNavigationStack::_update_scroll_indicator(int active_index) {
    uint32_t card_count = lv_obj_get_child_cnt(_main_container);
    uint32_t pip_count = lv_obj_get_child_cnt(_scroll_indicator);
    
    // Safety check - if we have no cards, don't update anything
    if (card_count == 0 || pip_count == 0) return;
    
    // Ensure active_index is valid
    if (active_index >= card_count) {
        active_index = card_count - 1;
        _current_card = active_index; // Update the current card too
    }
    
    // Safety check for pip count
    if (pip_count != card_count) {
        // Force re-sync pips with cards
        _update_pip_count();
        pip_count = lv_obj_get_child_cnt(_scroll_indicator);
    }
    
    // Keep track of previous active index with a static variable
    static int previous_index = 0;
    
    // Only update if we're changing to a different pip
    if (previous_index != active_index) {
        // Bounds checking for previous pip
        if (previous_index < pip_count) {
            // Get previous pip with bounds check
            lv_obj_t* prev_pip = lv_obj_get_child(_scroll_indicator, previous_index);
            if (prev_pip) {
                // Change color of inactive pip
                lv_obj_set_style_bg_color(prev_pip, lv_color_hex(0x808080), 0);
            }
        }

        // Bounds checking for new pip
        if (active_index < pip_count) {
            // Get new pip with bounds check
            lv_obj_t* new_pip = lv_obj_get_child(_scroll_indicator, active_index);
            if (new_pip) {
                // Change color of active pip
                lv_obj_set_style_bg_color(new_pip, lv_color_white(), 0);
            }
        }

        // Update previous index for next time
        previous_index = active_index;
    }
}

// Remove a card from the stack
bool CardNavigationStack::removeCard(lv_obj_t* card) {
    // Check if the card is a child of our container
    lv_obj_t* parent = lv_obj_get_parent(card);
    if (parent != _main_container) {
        return false; // Card not in our container
    }
    
    // Find the index of the card
    uint32_t card_index = 0;
    bool found = false;
    
    uint32_t child_count = lv_obj_get_child_cnt(_main_container);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t* child = lv_obj_get_child(_main_container, i);
        if (child == card) {
            card_index = i;
            found = true;
            break;
        }
    }
    
    if (!found) {
        return false; // Card not found
    }
    
    // Handle selection state before deleting the card
    uint32_t new_count = child_count - 1;
    uint8_t new_selection = _current_card;
    
    // Adjust current card index if necessary
    if (new_count == 0) {
        // If this was the last card, set index to 0
        new_selection = 0;
    } else if (card_index == _current_card) {
        // If we're deleting the current card, switch to the previous one
        // unless it's the first card, then switch to the next one
        if (_current_card > 0) {
            new_selection = _current_card - 1;
        } else {
            new_selection = 0; // Stay at the first card
        }
    } else if (card_index < _current_card) {
        // If we're deleting a card before the current one, 
        // we need to adjust the current index down by one
        new_selection = _current_card - 1;
    }
    
    // Remove input handler for this card if it exists
    for (auto it = _input_handlers.begin(); it != _input_handlers.end(); ++it) {
        if (it->first == card) {
            _input_handlers.erase(it);
            break;
        }
    }
    
    // Delete the card from LVGL
    lv_obj_del(card);
    
    // Force LVGL to process the deletion
    lv_refr_now(NULL);
    
    // Update the scroll indicator (this will recreate all pips)
    _update_pip_count();
    
    // Set the new selection - if there are any cards left
    if (new_count > 0) {
        // Ensure the new selection is within bounds 
        new_selection = (new_selection >= new_count) ? (new_count - 1) : new_selection;
        _current_card = new_selection;
        
        // Force scrolling to the selected card to ensure it's visible
        lv_obj_t* selected_card = lv_obj_get_child(_main_container, _current_card);
        if (selected_card) {
            lv_obj_scroll_to_view(selected_card, LV_ANIM_ON);
        }
        
        // Update scroll indicator after scrolling
        _update_scroll_indicator(_current_card);
    } else {
        // No cards left, reset current card index
        _current_card = 0;
    }
    
    return true;
}