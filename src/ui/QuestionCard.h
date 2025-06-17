#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include "ui/InputHandler.h"

/**
 * @class QuestionCard
 * @brief UI component for displaying rotating questions or quotes
 * 
 * Features:
 * - Rotating questions/quotes
 * - Center button interaction for cycling questions
 * - Custom styling and animations
 */
class QuestionCard : public InputHandler {
public:
    /**
     * @brief Constructor
     * @param parent LVGL parent object
     */
    QuestionCard(lv_obj_t* parent);
    
    /**
     * @brief Destructor - safely cleans up UI resources
     */
    ~QuestionCard();
    
    /**
     * @brief Get the underlying LVGL card object
     * @return LVGL object pointer or nullptr if not created
     */
    lv_obj_t* getCard();
    
    /**
     * @brief Add a question to the rotation
     * @param question The question to add
     */
    void addQuestion(const char* question);
    
    /**
     * @brief Cycle to the next question
     */
    void cycleNextQuestion();
    
    /**
     * @brief Handle button press events
     * @param button_index The index of the button that was pressed
     * @return true if center button (cycles questions), false otherwise
     */
    bool handleButtonPress(uint8_t button_index) override;
    
    /**
     * @brief Set text for both the main label and shadow label
     * @param text The text to display
     */
    void setText(const char* text);
    
    /**
     * @brief Start vertical scrolling animation
     */
    void startScrolling();
    
private:
    /**
     * @brief Create a text label with consistent styling
     */
    lv_obj_t* createStyledLabel(lv_obj_t* parent, lv_color_t color, int16_t x_offset, int16_t y_offset);
    
    /**
     * @brief Check if an LVGL object is valid
     */
    bool isValidObject(lv_obj_t* obj) const;
    
    // UI Elements
    lv_obj_t* _card;           ///< Main container
    lv_obj_t* _background;     ///< Background container
    lv_obj_t* _label;          ///< Main text label
    lv_obj_t* _label_shadow;   ///< Shadow text label
    lv_obj_t* _cont;          ///< Main container for scrolling
    lv_obj_t* _shadow_cont;   ///< Shadow container for scrolling
    
    // State tracking
    std::vector<std::string> _questions;        ///< List of questions
    size_t _current_question_index;             ///< Index of current question
};