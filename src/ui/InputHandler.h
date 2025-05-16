#pragma once

/**
 * @class InputHandler
 * @brief Interface for components that can handle button input
 * 
 * Allows cards and other UI components to receive button press events
 */
class InputHandler {
public:
    virtual ~InputHandler() = default;
    
    /**
     * @brief Handle a button press event
     * 
     * @param button_index The index of the button that was pressed
     * @return true if the event was handled, false to pass to default handler
     */
    virtual bool handleButtonPress(uint8_t button_index) = 0;

    /**
     * @brief Optional method for components that need continuous updates.
     * 
     * Called regularly by the CardController if the handler is the active card.
     */
    virtual void update() {}

    /**
     * @brief Get the main LVGL object associated with this handler.
     * 
     * @return lv_obj_t* Pointer to the LVGL object.
     */
    virtual lv_obj_t* getCardObject() const = 0;
}; 