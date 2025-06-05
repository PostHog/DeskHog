#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <Bounce2.h>
#include <vector>
#include "ui/InputHandler.h"

// Forward declaration
class DisplayInterface;

/**
 * @class CardNavigationStack
 * @brief Manages a vertically scrolling stack of cards with visual indicators
 * 
 * Provides a container for card-based UI elements with:
 * - Smooth animated transitions between cards
 * - Visual scroll indicators (pips) showing current position
 * - Button-based navigation
 * - Support for card-specific input handling
 * - Thread-safe button handling via mutex
 */
class CardNavigationStack {
public:
    /**
     * @brief Constructor
     * @param parent LVGL parent object
     * @param width Width of the card stack
     * @param height Height of the card stack
     * 
     * Creates a main container for cards and a separate container
     * for scroll indicators
     */
    CardNavigationStack(lv_obj_t* parent, uint16_t width, uint16_t height);
    
    /**
     * @brief Add an existing LVGL object as a card
     * @param card LVGL object to add
     * 
     * Configures the card to fill the container and updates scroll indicators.
     * First card added becomes the active card.
     */
    void addCard(lv_obj_t* card);
    
    /**
     * @brief Remove a card from the stack
     * @param card LVGL object to remove
     * @return true if card was found and removed
     */
    bool removeCard(lv_obj_t* card);
    
    /**
     * @brief Navigate to next card with animation
     * 
     * Wraps around to first card if at end of stack.
     */
    void nextCard();

    /**
     * @brief Navigate to previous card with animation
     * 
     * Wraps around to last card if at start of stack.
     */
    void prevCard();

    /**
     * @brief Navigate to specific card with animation
     * @param index Zero-based index of target card
     * 
     * No action if index is out of range.
     */
    void goToCard(uint8_t index);
    
    /**
     * @brief Get index of currently visible card
     * @return Zero-based index of current card
     */
    uint8_t getCurrentIndex() const;
    
    /**
     * @brief Get LVGL object of a specific card
     * @param index Zero-based index of target card
     * @return LVGL object of the specified card
     */
    lv_obj_t* getCardObjectByIndex(uint8_t index) const;
    
    /**
     * @brief Get number of cards in the stack
     * @return Number of cards in the stack
     */
    uint8_t getCardCount() const;
    
    /**
     * @brief Set mutex for thread-safe button handling
     * @param mutex_ptr Pointer to FreeRTOS semaphore
     */
    void setMutex(SemaphoreHandle_t* mutex_ptr);
    
    /**
     * @brief Process button press events
     * @param button_index Index of button that was pressed
     * 
     * Handles navigation and delegates center button presses
     * to card-specific input handlers if registered.
     */
    void handleButtonPress(uint8_t button_index);
    
    /**
     * @brief Register an input handler for a specific card
     * @param card LVGL object to handle input for
     * @param handler InputHandler implementation
     */
    void registerInputHandler(lv_obj_t* card, InputHandler* handler);
    
private:
    /**
     * @brief LVGL scroll event callback
     * Updates scroll indicators when scrolling occurs
     */
    static void _scroll_event_cb(lv_event_t* e);
    
    /**
     * @brief Update number of scroll indicator pips
     * 
     * Adds or removes pips to match card count.
     * Adjusts pip height to fill container evenly.
     */
    void _update_pip_count();

    /**
     * @brief Update active scroll indicator
     * @param active_index Index of currently active card
     * 
     * Highlights the pip corresponding to active card.
     */
    void _update_scroll_indicator(int active_index);
    
    // UI elements
    lv_obj_t* _parent;              ///< Parent LVGL object
    lv_obj_t* _main_container;      ///< Container for cards
    lv_obj_t* _scroll_indicator;    ///< Container for indicator pips
    
    // Dimensions
    uint16_t _width;                ///< Width of card stack
    uint16_t _height;               ///< Height of card stack
    
    // Navigation state
    uint8_t _current_card;          ///< Index of currently visible card
    
    // Thread safety
    SemaphoreHandle_t* _mutex_ptr;  ///< Optional mutex for thread-safe updates
    
    // Input handling
    std::vector<std::pair<lv_obj_t*, InputHandler*>> _input_handlers;  ///< Card-specific input handlers
}; 