#pragma once

#include <lvgl.h>
#include <memory>
#include <functional>
#include <vector>
#include "ConfigManager.h"
#include "EventQueue.h"
#include "posthog/parsers/InsightParser.h"
#include "UICallback.h"
#include "ui/InputHandler.h"

// Forward declaration for the renderer base class
class InsightRendererBase;

/**
 * @class InsightCard
 * @brief UI component for displaying different data visualizations
 * 
 * Provides a flexible card-based UI component that can display various PostHog insights:
 * - Numeric displays (single value with formatted numbers)
 * - Line graphs (time series with auto-scaling)
 * - Funnel visualizations (with multi-breakdown support)
 * 
 * Features:
 * - Thread-safe UI updates via queue system
 * - Automatic insight type detection and UI adaptation
 * - Memory-safe LVGL object management
 * - Smart number formatting with unit scaling (K, M)
 */
class InsightCard : public InputHandler {
public:
    /**
     * @brief Constructor
     * 
     * @param parent LVGL parent object to attach this card to
     * @param config Configuration manager for persistent storage
     * @param eventQueue Event queue for receiving data updates
     * @param insightId Unique identifier for this insight
     * @param width Card width in pixels
     * @param height Card height in pixels
     * 
     * Creates a card with a vertical flex layout containing:
     * - Title label with ellipsis for overflow
     * - Content container for visualization
     * Subscribes to INSIGHT_DATA_RECEIVED events for the specified insightId.
     */
    InsightCard(lv_obj_t* parent, ConfigManager& config, EventQueue& eventQueue,
                const String& insightId, uint16_t width, uint16_t height);
    
    /**
     * @brief Destructor - safely cleans up UI resources
     * 
     * Ensures all LVGL objects are deleted safely on the UI thread
     * using async deletion to prevent threading issues.
     */
    ~InsightCard();
    
    /**
     * @brief Get the underlying LVGL card object
     * 
     * @return LVGL object pointer for the main card container
     */
    lv_obj_t* getCard() const { return _card; }

    /**
     * @brief Get the insight ID
     * 
     * @return String containing the unique insight identifier
     */
    String getInsightId() const { return _insight_id; }

    /**
     * @brief Handle button press events
     * 
     * @param button_index The index of the button that was pressed
     * @return true if the event was handled, false otherwise
     * 
     * Handles center button (button 1) to force refresh insight data
     */
    bool handleButtonPress(uint8_t button_index) override;
    void prepareForRemoval() override { _card = nullptr; }

private:
    // Constants for UI layout and limits
    static constexpr int MAX_FUNNEL_STEPS = 5;     ///< Maximum number of steps in a funnel
    static constexpr int MAX_BREAKDOWNS = 5;       ///< Maximum number of breakdowns per step
    static constexpr int GRAPH_WIDTH = 240;        ///< Width of line graphs
    static constexpr int GRAPH_HEIGHT = 90;        ///< Height of line graphs
    static constexpr int FUNNEL_BAR_HEIGHT = 5;    ///< Height of each funnel bar
    static constexpr int FUNNEL_BAR_GAP = 20;      ///< Vertical gap between funnel bars
    static constexpr int FUNNEL_LEFT_MARGIN = 0;   ///< Left margin for funnel bars
    static constexpr int FUNNEL_LABEL_HEIGHT = 20; ///< Height of funnel step labels

    
    /**
     * @brief Handle events from the event queue
     * 
     * @param event Event containing insight data or JSON
     * 
     * Processes INSIGHT_DATA_RECEIVED events, parsing JSON if needed
     * and updating the visualization accordingly.
     */
    void onEvent(const Event& event);
    
    /**
     * @brief Handle error events from network requests
     * 
     * @param event Event containing error information
     * 
     * Updates UI to show error state with smooth transitions.
     */
    void onErrorEvent(const Event& event);
    
    /**
     * @brief Handle network state changes for Apple-like loading states
     * 
     * @param event Event containing network state information
     * 
     * Updates loading indicators and animations based on network state.
     */
    void onNetworkStateChanged(const Event& event);
    
    /**
     * @brief Process parsed insight data
     * 
     * @param parser Shared pointer to parsed insight data
     * 
     * Updates the card's visualization based on the insight type.
     * Handles type changes by recreating UI elements as needed.
     */
    void handleParsedData(std::shared_ptr<InsightParser> parser);
    
    /**
     * @brief Clear the content container
     * 
     * Safely removes all UI elements from the content container
     * and resets internal element pointers to nullptr.
     */
    void clearContentContainer();
    
    /**
     * @brief Check if an LVGL object is valid
     * 
     * @param obj LVGL object to check
     * @return true if object exists and is valid
     * 
     * Thread-safe method to verify LVGL object validity
     * before performing operations.
     */
    bool isValidObject(lv_obj_t* obj) const;
    
    // Configuration and state
    ConfigManager& _config;              ///< Configuration manager reference
    EventQueue& _event_queue;            ///< Event queue reference
    String _insight_id;                  ///< Unique insight identifier
    String _current_title;               ///< Current card title
    InsightParser::InsightType _current_type; ///< Current visualization type
    
    // UI Elements
    lv_obj_t* _card;                    ///< Main card container
    lv_obj_t* _title_label;             ///< Title text label
    lv_obj_t* _content_container;       ///< Container for visualization
    lv_obj_t* _loading_spinner;         ///< Loading spinner for Apple-like loading states
    lv_obj_t* _error_label;             ///< Error message label
    
    // Loading state management
    bool _is_loading;                   ///< Current loading state
    bool _has_error;                    ///< Current error state
    bool _is_showing_cached_data;       ///< Whether currently showing cached data
    
    // Renderer related members
    std::unique_ptr<InsightRendererBase> _active_renderer; ///< Smart pointer to the current renderer
    
    /**
     * @brief Show loading state with spinner
     * @param show_spinner Whether to show animated spinner
     */
    void showLoadingState(bool show_spinner = true);
    
    /**
     * @brief Show error state with message
     * @param error_message Error message to display
     */
    void showErrorState(const String& error_message);
    
    /**
     * @brief Show success state (hide loading/error indicators)
     */
    void showSuccessState();
    
    /**
     * @brief Create loading spinner with Apple-like animation
     */
    void createLoadingSpinner();
    
    /**
     * @brief Create error display elements
     */
    void createErrorDisplay();
    
    /**
     * @brief Animate transition between states
     * @param from_opacity Starting opacity (0-255)
     * @param to_opacity Ending opacity (0-255)
     * @param duration Animation duration in ms
     */
    void animateOpacityTransition(lv_obj_t* obj, uint8_t from_opacity, uint8_t to_opacity, uint32_t duration = 300);
};