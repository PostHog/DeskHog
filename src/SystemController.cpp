#include "SystemController.h"

// Initialize static member
SystemController* SystemController::instance = nullptr;

// Private constructor
SystemController::SystemController() : state{
    WifiState::DISCONNECTED,  // Initial WiFi state
    SystemState::SYS_BOOTING
} {}

void SystemController::begin() {
    // Register for WiFi state changes
    WiFiInterface::onStateChange(onWiFiStateChange);
    SystemController::setSystemState(SystemState::SYS_BOOTING);
}

// WiFi event handler
void SystemController::onWiFiStateChange(WiFiState new_state) {
    if (getInstance()->state.wifi_state != new_state) {
        getInstance()->state.wifi_state = new_state;
        getInstance()->notify_state_change();
    }
}

// Singleton access method
SystemController* SystemController::getInstance() {
    if (instance == nullptr) {
        instance = new SystemController();
    }
    return instance;
}

// State accessors
WifiState SystemController::getWifiState() {
    return getInstance()->state.wifi_state;
}

SystemState SystemController::getSystemState() {
    return getInstance()->state.sys_state;
}

ControllerState SystemController::getFullState() {
    return getInstance()->state;
}

// State setters
void SystemController::setSystemState(SystemState new_state) {
    if (getInstance()->state.sys_state != new_state) {
        getInstance()->state.sys_state = new_state;
        getInstance()->notify_state_change();
    }
}

// Notify state changes and log
void SystemController::notify_state_change() {
    auto* controller = getInstance();
    
    // Log state changes
    Serial.println("State change:");
    Serial.print("  WiFi: ");
    switch (controller->state.wifi_state) {
        case WifiState::DISCONNECTED: Serial.println("DISCONNECTED"); break;
        case WifiState::CONNECTING: Serial.println("CONNECTING"); break;
        case WifiState::CONNECTED: Serial.println("CONNECTED"); break;
        case WifiState::AP_MODE: Serial.println("AP_MODE"); break;
    }
    
    Serial.print("  System: ");
    switch (controller->state.sys_state) {
        case SystemState::SYS_BOOTING: Serial.println("BOOTING"); break;
        case SystemState::SYS_READY: Serial.println("READY"); break;
    }
    
    // Notify all registered callbacks
    for (const auto& callback : controller->state_change_callbacks) {
        callback(controller->state);
    }
}

// Callback management
void SystemController::onStateChange(StateChangeCallback callback) {
    getInstance()->state_change_callbacks.push_back(callback);
    // Immediately call the callback with the current state
    callback(getInstance()->state);
}
