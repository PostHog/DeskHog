#ifndef UI_CALLBACK_H
#define UI_CALLBACK_H

#include <functional>

// Helper class for memory-safe UI callbacks, typically for LVGL operations
class UICallback {
public:
    UICallback(std::function<void()> func) : _func(std::move(func)) {}
    
    void execute() {
        if (_func) {
            _func();
        }
    }
    
private:
    std::function<void()> _func;
};

/**
 * @brief Global UI dispatch function
 * 
 * This function allows any component to dispatch UI updates to the LVGL thread safely.
 * It should be set by CardController during initialization.
 * 
 * @param func The function to execute on the UI thread
 * @param to_front Whether to add to front of queue (higher priority)
 */
extern std::function<void(std::function<void()>, bool)> globalUIDispatch;

#endif // UI_CALLBACK_H 