#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include "hardware/WifiInterface.h"

// Use the WiFiInterface's state enum
using WifiState = WiFiState;


/**
 * @enum SystemState
 * @brief Represents the overall system state
 */
enum class SystemState {
    SYS_BOOTING,         ///< System is initializing
    SYS_READY            ///< System is ready for operation
};

/**
 * @struct ControllerState
 * @brief Composite state containing all system state components
 */
struct ControllerState {
    WifiState wifi_state;    ///< Current WiFi connection state
    SystemState sys_state;   ///< Current system state
};

/**
 * @typedef StateChangeCallback
 * @brief Function type for state change notifications
 * 
 * Callbacks are invoked immediately upon registration with current state,
 * and subsequently whenever any state component changes.
 */
typedef std::function<void(const ControllerState&)> StateChangeCallback;

/**
 * @class SystemController
 * @brief Singleton controller managing overall system state
 * 
 * Keeps track of the WiFi state and whether the device has finished starting. Provides a 
 * callback mechanism for state change notifications.
 * 
 * State changes are logged to Serial for debugging purposes.
 */
class SystemController {
private:
    ControllerState state;
    static SystemController* instance;
    std::vector<StateChangeCallback> state_change_callbacks;
    
    /**
     * @brief Private constructor for singleton pattern
     * 
     * Initializes state to:
     * - WiFi: DISCONNECTED
     * - Auth: AUTH_NONE
     * - System: SYS_BOOTING
     */
    SystemController();

    // Delete copy constructor and assignment operator
    SystemController(const SystemController&) = delete;
    void operator=(const SystemController&) = delete;
    
    /**
     * @brief Notify all registered callbacks of state change
     * 
     * Logs current state to Serial and calls all registered callbacks
     * with the current state. Only called when a state component actually changes.
     */
    void notify_state_change();
    
    /**
     * @brief Get singleton instance
     * @return Pointer to singleton instance, creating it if necessary
     */
    static SystemController* getInstance();
    
    /**
     * @brief Handle WiFi state changes from WiFiInterface
     * @param new_state New WiFi state
     * 
     * Registered with WiFiInterface during begin() to receive WiFi state updates
     */
    static void onWiFiStateChange(WiFiState new_state);
    
public:
    /**
     * @brief Initialize the system controller
     * 
     * Registers for WiFi state changes and sets initial system state to BOOTING
     */
    static void begin();
    
    /**
     * @brief Get current WiFi state
     * @return Current WiFi state
     */
    static WifiState getWifiState();

    /**
     * @brief Get current system state
     * @return Current system state
     */
    static SystemState getSystemState();

    /**
     * @brief Get complete system state
     * @return Structure containing all state components
     */
    static ControllerState getFullState();

    /**
     * @brief Set system state
     * @param new_state New system state
     * 
     * Only triggers notifications if state actually changes
     */
    static void setSystemState(SystemState new_state);
    
    /**
     * @brief Register callback for state changes
     * @param callback Function to call on state change
     * 
     * Callback is immediately invoked with current state upon registration,
     * and subsequently whenever any state component changes
     */
    static void onStateChange(StateChangeCallback callback);

};  