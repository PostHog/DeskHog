#ifndef INSIGHT_RENDERER_BASE_H
#define INSIGHT_RENDERER_BASE_H

#include "lvgl.h"
#include "../../posthog/parsers/InsightParser.h" // Adjusted path
#include <Arduino.h> // For String, if used in titles or other data
#include <functional> // For std::function

#include "../UICallback.h" // For global dispatch function

/**
 * @class InsightRendererBase
 * @brief Abstract base class for rendering different types of insights.
 *
 * Defines the interface for creating, updating, and clearing UI elements 
 * for a specific insight visualization.
 *
 * **Important LVGL Rendering Lifecycle Note:**
 * When implementing a new renderer, be aware of LVGL's rendering and layout lifecycle.
 * LVGL may not immediately calculate the final dimensions and positions of newly created 
 * objects (especially when using flexbox or percentage-based sizing in parent containers)
 * within the same execution cycle as their creation.
 *
 * If a renderer's `updateDisplay` method relies on the final, calculated dimensions of 
 * elements just created in its `createElements` method (e.g., to size or position 
 * children accurately), it's crucial to ensure LVGL has processed these layouts.
 * In the `InsightCard` (which manages these renderers), a forced refresh 
 * (e.g., `lv_obj_invalidate()` on the container followed by `lv_refr_now()`) 
 * is performed *between* calling `createElements()` and `updateDisplay()`.
 * This allows `updateDisplay()` to work with more reliable, up-to-date element dimensions.
 * Failure to account for this can lead to elements appearing incorrectly sized, 
 * misplaced, or not appearing until a later, unrelated refresh cycle.
 */
class InsightRendererBase {
public:
    virtual ~InsightRendererBase() = default;

    /**
     * @brief Creates the specific UI elements for this insight type.
     * This method will be called on the LVGL UI thread.
     * 
     * @param parent_container The LVGL object within which to create the elements.
     * @param initial_title The initial title to set for the insight.
     */
    virtual void createElements(lv_obj_t* parent_container) = 0;

    /**
     * @brief Updates the display with new data from the parser.
     * This method will be called when new data for the insight is received.
     * The renderer is responsible for dispatching its internal LVGL calls to the UI thread.
     * 
     * @param parser The InsightParser instance containing the new data.
     * @param title The title of the insight.
     * @param prefix The prefix to prepend to the title.
     * @param suffix The suffix to append to the title.
     */
    virtual void updateDisplay(InsightParser& parser, const String& title, const char* prefix = nullptr, const char* suffix = nullptr) = 0;

    /**
     * @brief Clears/deletes all UI elements created by this renderer.
     * This method will be called on the LVGL UI thread before this renderer is destroyed
     * or when the insight type changes.
     */
    virtual void clearElements() = 0;

    /**
     * @brief Checks if the core UI elements managed by this renderer are valid.
     * Useful for pre-update validation.
     * @return true if elements are valid, false otherwise.
     */
    virtual bool areElementsValid() const = 0;

protected:
    // Helper to dispatch UI updates to the LVGL task using global dispatch function
    static void dispatchToUI(std::function<void()> func, bool to_front = false) {
        if (globalUIDispatch) {
            globalUIDispatch(std::move(func), to_front);
        } else {
            Serial.println("[UI-ERROR] Global UI dispatch not set, cannot dispatch UI update.");
        }
    }

    // Helper to check LVGL object validity (can be used by derived classes)
    static bool isValidLVGLObject(lv_obj_t* obj) {
        return obj && lv_obj_is_valid(obj);
    }
};

#endif // INSIGHT_RENDERER_BASE_H 