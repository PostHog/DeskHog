#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include "ui/InputHandler.h"

/**
 * @class AnimationCard
 * @brief UI component for displaying animated sprites with encouraging messages
 * 
 * Features:
 * - Get reassurance from Max
 * - Cycling messages
 * - Center button interaction for cycling messages
 */
class AnimationCard : public InputHandler {
public:
    /**
     * @brief Constructor
     * 
     * @param parent LVGL parent object
     * 
     * Creates a full-width/height card
     */
    AnimationCard(lv_obj_t* parent);
    
    /**
     * @brief Destructor - safely cleans up UI resources
     * 
     * Hides the card and schedules async deletion of all UI elements
     */
    ~AnimationCard();
    
    /**
     * @brief Get the underlying LVGL card object
     * @return LVGL object pointer or nullptr if not created
     */
    lv_obj_t* getCardObject() const override;
    
    /**
     * @brief Start the sprite animation
     * 
     * Starts infinite looping animation if not already running.
     * Animation duration is ANIMATION_DURATION_MS.
     */
    void startAnimation();
    
    /**
     * @brief Stop the sprite animation
     * 
     * Currently only marks animation as stopped (no pause functionality).
     */
    void stopAnimation();
    
    /**
     * @brief Set text for both the main label and shadow label
     * 
     * @param text The text to display
     * 
     * Updates both the white main text and black shadow text (1px offset)
     */
    void setText(const char* text);
    
    /**
     * @brief Add a message to the cycling messages list
     * 
     * @param message The message to add
     * 
     * Messages can be cycled through using the center button
     */
    void addMessage(const char* message);
    
    /**
     * @brief Cycle to the next message in the list
     * 
     * Advances to next message and wraps around to start.
     * No effect if message list is empty.
     */
    void cycleNextMessage();
    
    /**
     * @brief Handle button press events
     * 
     * @param button_index The index of the button that was pressed
     * @return true if center button (cycles messages), false otherwise
     */
    bool handleButtonPress(uint8_t button_index) override;
    
    // Add override for update() from InputHandler
    void update() override;
    
private:
    // Animation timing
    static constexpr int ANIMATION_DURATION_MS = 1000; ///< Duration of one animation cycle
    
    /**
     * @brief A safe wrapper to check if an LVGL object is valid
     * 
     * @param obj LVGL object to check
     * @return true if object exists and is valid
     */
    bool isValidObject(lv_obj_t* obj) const;
    
    /**
     * @brief Create a text label with consistent styling
     * 
     * @param parent Parent LVGL object
     * @param color Text color
     * @param x_offset X offset for alignment
     * @param y_offset Y offset for alignment
     * @return lv_obj_t* The created label or nullptr if creation fails
     * 
     * Creates a label with Loud Noises font
     */
    lv_obj_t* createStyledLabel(lv_obj_t* parent, lv_color_t color, int16_t x_offset, int16_t y_offset);
    
    // UI Elements
    lv_obj_t* _card;           ///< Main black container
    lv_obj_t* _background;     ///< Green rounded container
    lv_obj_t* _anim_img;       ///< Walking sprite animation
    lv_obj_t* _label;          ///< White text label
    lv_obj_t* _label_shadow;   ///< Black shadow text label
    
    // State tracking
    bool _animation_running;    ///< Current animation state
    
    // Message management
    std::vector<std::string> _messages;        ///< List of cycling messages
    size_t _current_message_index;             ///< Index of current message
}; 