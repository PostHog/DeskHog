#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <memory>
#include <string>
#include "ui/InputHandler.h"

// Enum for the card's display mode
enum class DisplayMode {
    CLOCK,
    TIMER_STOPPED,
    TIMER_RUNNING
};

/**
 * @class ClockCard
 * @brief UI component for displaying the current time
 */
class ClockCard : public InputHandler {
public:
    /**
     * @brief Constructor
     * @param parent LVGL parent object
     */
    ClockCard(lv_obj_t* parent);

    /**
     * @brief Destructor
     */
    ~ClockCard();

    /**
     * @brief Get the underlying LVGL card object
     * @return LVGL object pointer or nullptr if not created
     */
    lv_obj_t* getCard();

    /**
     * @brief Update the displayed time and background color
     * @param newTime String representing the current time (e.g., "HH:MM")
     */
    void updateTime(const String& newTime);

    /**
     * @brief Updates the timer display if the timer is currently running.
     *        Should be called frequently (e.g., from the main UI loop).
     */
    void updateIfTimerRunning();

    /**
     * @brief Handle button press events (does nothing for clock card)
     * @param button_index The index of the button pressed
     * @return false as clock card doesn't handle buttons
     */
    bool handleButtonPress(uint8_t button_index) override;

private:
    /**
     * @brief A safe wrapper to check if an LVGL object is valid
     * @param obj LVGL object to check
     * @return true if object exists and is valid
     */
    bool isValidObject(lv_obj_t* obj) const;

    // UI Elements
    lv_obj_t* _card;        ///< Main container card
    lv_obj_t* _timeLabel;   ///< Label to display the time/timer

    // State Management
    DisplayMode _currentMode; ///< Current display mode (clock or timer)
    unsigned long _timerStartTimeMillis; ///< Timestamp when the timer was started
    unsigned long _lastTimerUpdateMillis; ///< Timestamp of the last timer label update

    // Background Colors
    lv_color_t _dayBackgroundColor;   ///< Background color for daytime
    lv_color_t _nightBackgroundColor; ///< Background color for nighttime

    /**
     * @brief Updates the background color of the card based on the current hour.
     * @param currentHour The current hour (0-23).
     */
    void updateBackgroundColor(int currentHour);

    /**
     * @brief Resets the timer state and display to 00:00:00.
     */
    void resetTimer();

    /**
     * @brief Updates the time label with the current elapsed timer value.
     */
    void updateTimerDisplay();
}; 