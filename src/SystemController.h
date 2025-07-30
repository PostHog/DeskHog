#pragma once

#include <Arduino.h>
#include <functional>
#include <vector>
#include "hardware/WifiInterface.h"

// Use the WiFiInterface's state enum
using WifiState = WiFiState;

/**
 * @enum AuthState
 * @brief Represents the current authentication state
 */
enum class AuthState {
    AUTH_NONE,           ///< No authentication attempted
    AUTH_AWAITING_LOGIN, ///< Waiting for user login
    AUTH_CONFIRMED      ///< Authentication confirmed
};

/**
 * @enum SystemState
 * @brief Represents the overall system state
 */
enum class SystemState {
    SYS_BOOTING,         ///< System is initializing
    SYS_READY,           ///< System is ready for operation
    SYS_IDLE,            ///< System is in idle state
    SYS_INSIGHTS_CHANGED ///< Insight configuration has changed
};

/**
 * @struct ControllerState
 * @brief Composite state containing all system state components
 */
struct ControllerState {
    WifiState wifi_state;    ///< Current WiFi connection state
    AuthState auth_state;    ///< Current authentication state
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
 * Coordinates system components and maintains state information about WiFi,
 * API configuration, authentication, and system readiness. Provides a 
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
     * - API: API_NONE
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
     * @brief Get current authentication state
     * @return Current authentication state
     */
    static AuthState getAuthState();

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
     * @brief Set authentication state
     * @param new_state New authentication state
     * 
     * Only triggers notifications if state actually changes
     */
    static void setAuthState(AuthState new_state);

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

    /**
     * @brief Remove all registered callbacks
     */
    static void removeAllCallbacks();
    
    /**
     * @brief Check if system is fully initialized and ready
     * @return true if system is ready, false otherwise
     * 
     * System is considered ready when:
     * - WiFi is CONNECTED
     * - API is CONFIGURED
     * - System state is READY, IDLE, or INSIGHTS_CHANGED
     */
    static bool isSystemFullyReady();
};  