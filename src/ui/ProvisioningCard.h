#pragma once

#include <lvgl.h>  // LVGL core library
#include <string>  // For String class (or could be Arduino's String)
#include "hardware/WiFiInterface.h"  // Custom WiFi interface class
#include "ui/InputHandler.h"

/**
 * @class ProvisioningCard
 * @brief UI component for WiFi provisioning with QR code and status display
 * 
 * Features:
 * - Two switchable screens: QR code and status display
 * - Thread-safe label updates using LVGL timers
 * - WiFi status table with custom fonts and styling
 * - QR code generation for WiFi network connection
 */
class ProvisioningCard : public InputHandler {
public:
    /**
     * @brief Construct a new WiFi provisioning card UI component
     * 
     * @param parent Parent LVGL container
     * @param wifiInterface Reference to WiFi interface
     * @param width Card width (uses 100% of parent width)
     * @param height Card height
     * 
     * Creates a card with two screens (QR and status).
     * QR screen is shown initially.
     */
    ProvisioningCard(lv_obj_t* parent, WiFiInterface& wifiInterface, 
                     uint16_t width, uint16_t height);
    
    /**
     * @brief Get the underlying LVGL card object
     * 
     * @return lv_obj_t* Pointer to the LVGL card container
     */
    lv_obj_t* getCardObject() const override;
    
    /**
     * @brief Updates connection status text and shows status screen
     * 
     * @param status WiFi connection status text
     * 
     * Thread-safe update that switches to status screen view.
     */
    void updateConnectionStatus(const String& status);
    
    /**
     * @brief Updates IP address display
     * 
     * @param ip IP address string
     * 
     * Thread-safe update of IP address label.
     */
    void updateIPAddress(const String& ip);
    
    /**
     * @brief Updates signal strength percentage
     * 
     * @param strength Signal strength (clamped to 0-100)
     * 
     * Thread-safe update of signal strength label.
     */
    void updateSignalStrength(int strength);
    
    /**
     * @brief Generates and displays QR code for WiFi network
     * 
     * Creates QR code for current network SSID (no password).
     * Thread-safe update that switches to QR code view.
     */
    void showQRCode();
    
    /**
     * @brief Shows WiFi status screen and hides QR code screen
     * 
     * Thread-safe screen transition.
     */
    void showWiFiStatus();

    // InputHandler interface
    bool handleButtonPress(uint8_t button_index) override {
        // Provisioning card doesn't handle direct button presses for now
        return false; 
    }
    void update() override {
        // No continuous update needed for ProvisioningCard
    }
    
private:
    /**
     * @brief Creates main card container
     * 
     * Sets up full-width card with rounded corners and
     * creates containers for both QR and status screens.
     */
    void createCard();
    
    /**
     * @brief Creates the QR code screen
     * 
     * Creates QR code display sized to fit card height with
     * 20px padding. Initializes with placeholder "WIFI:T:WPA;".
     */
    void createQRScreen();
    
    /**
     * @brief Creates the status display screen
     * 
     * Creates table with three rows:
     * - WiFi status
     * - IP address
     * - Signal strength
     * Each row has custom fonts and colors.
     */
    void createStatusScreen();
    
    /**
     * @brief Creates a table row with label and value
     * 
     * @param table Parent table object
     * @param row Row index (0-based)
     * @param title Label text
     * @param valueLabel Pointer to store created value label
     * @param labelColor Color for the title label
     * 
     * Creates a row with:
     * - Left-aligned title using label font
     * - Right-aligned value using value font
     * - 5px line spacing
     */
    void createTableRow(lv_obj_t* table, uint16_t row, const char* title, 
                      lv_obj_t** valueLabel, lv_color_t labelColor);
    
    /**
     * @brief Generates QR code data string for WiFi network
     * 
     * @param ssid Network SSID
     * @param password Network password
     * @return WiFi network QR code format string
     * 
     * Formats string as "WIFI:S:<ssid>;T:<type>;P:<pass>;;"
     * where type is "nopass" for open networks or "WPA" for secured.
     */
    String generateQRCodeData(const String& ssid, const String& password);
    
    /**
     * @brief Escapes special characters in strings for QR code
     * 
     * @param input Input string
     * @return String with escaped: \, ;, ,, ", '
     */
    String escapeString(const String& input);
    
    /**
     * @brief Safely updates a label's text from any thread
     * 
     * @param label Label to update
     * @param text New text content
     * 
     * Uses LVGL timer to ensure thread-safe update.
     */
    void safeUpdateLabel(lv_obj_t* label, const String& text);
    
    /**
     * @brief Safely toggles visibility between two screens
     * 
     * @param showScreen Screen to show
     * @param hideScreen Screen to hide
     * 
     * Uses LVGL timer to ensure thread-safe transition.
     */
    void toggleScreens(lv_obj_t* showScreen, lv_obj_t* hideScreen);
    
    // Member variables
    lv_obj_t* _parent;              ///< Parent LVGL container
    WiFiInterface& _wifiInterface;   ///< WiFi interface reference
    uint16_t _width;                ///< Card width
    uint16_t _height;               ///< Card height
    
    // UI Elements
    lv_obj_t* _card;               ///< Main card container
    lv_obj_t* _qrScreen;           ///< QR code screen container
    lv_obj_t* _statusScreen;       ///< Status screen container
    lv_obj_t* _qrCode;            ///< QR code widget
    lv_obj_t* _ssidLabel;         ///< SSID text label below QR code
    lv_obj_t* _statusLabel;       ///< WiFi status text
    lv_obj_t* _ipLabel;           ///< IP address text
    lv_obj_t* _signalLabel;       ///< Signal strength text
    lv_obj_t* _versionLabel;      ///< Firmware version text
    lv_obj_t* _topLeftVersionLabel; ///< Firmware version text (on card top-left)
};