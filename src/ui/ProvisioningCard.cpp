#include "ProvisioningCard.h"
#include "Style.h"
#include "SystemController.h"
#include "UICallback.h"
#include "hardware/DisplayInterface.h"

// Defined in main.cpp
extern DisplayInterface* displayInterface;

ProvisioningCard::ProvisioningCard(lv_obj_t* parent, WiFiInterface& wifiInterface, uint16_t width, uint16_t height)
    : _parent(parent), _wifiInterface(wifiInterface), _width(width), _height(height),
    _card(nullptr), _qrScreen(nullptr), _statusScreen(nullptr),
    _qrCode(nullptr), _ssidLabel(nullptr), _statusLabel(nullptr), _ipLabel(nullptr), _signalLabel(nullptr), _versionLabel(nullptr),
    _topLeftVersionLabel(nullptr) {
    
    createCard();
    createQRScreen();
    createStatusScreen();
    
    // Initially show QR screen and hide status screen
    lv_obj_clear_flag(_qrScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_statusScreen, LV_OBJ_FLAG_HIDDEN);

    // Register for system state changes and initialize API status display
    SystemController::onStateChange(std::bind(&ProvisioningCard::handleSystemStateChange, this, std::placeholders::_1));
    // Call handler immediately to set initial state for API status and potentially WiFi SSID
    handleSystemStateChange(SystemController::getFullState()); 
}

void ProvisioningCard::createCard() {
    // Create main card container - use full width of parent
    _card = lv_obj_create(_parent);
    lv_obj_set_size(_card, LV_PCT(100), _height);
    lv_obj_set_style_bg_color(_card, Style::backgroundColor(), 0);
    lv_obj_set_style_pad_all(_card, 0, 0);
    lv_obj_set_style_radius(_card, 8, 0);
    lv_obj_set_style_border_width(_card, 0, 0);
    
    // Create both screens as children of the card
    _qrScreen = lv_obj_create(_card);
    _statusScreen = lv_obj_create(_card);
    
    // Configure screens to take full card size
    lv_obj_set_size(_qrScreen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_size(_statusScreen, LV_PCT(100), LV_PCT(100));
    
    // Set screen backgrounds to match parent
    lv_obj_set_style_bg_color(_qrScreen, Style::backgroundColor(), 0);
    lv_obj_set_style_bg_color(_statusScreen, Style::backgroundColor(), 0);
    
    // Position screens at 0,0 relative to card
    lv_obj_set_pos(_qrScreen, 0, 0);
    lv_obj_set_pos(_statusScreen, 0, 0);

    // Create and configure the top-left version label
    // _topLeftVersionLabel = lv_label_create(_card);
    // lv_obj_set_style_text_font(_topLeftVersionLabel, Style::labelFont(), 0);
    // lv_obj_set_style_text_color(_topLeftVersionLabel, Style::labelColor(), 0);
    // lv_label_set_text(_topLeftVersionLabel, CURRENT_FIRMWARE_VERSION);
    // lv_obj_align(_topLeftVersionLabel, LV_ALIGN_TOP_LEFT, 5, 5); // 5px padding from top-left
    // lv_obj_move_foreground(_topLeftVersionLabel); // Ensure it's on top
}

void ProvisioningCard::updateConnectionStatus(const String& status) {
    if (SystemController::getWifiState() != WifiState::CONNECTED) {
        safeUpdateLabel(_statusLabel, status);
    } else {
        safeUpdateLabel(_statusLabel, _wifiInterface.getSSID());
    }
    showWiFiStatus();
}

void ProvisioningCard::updateIPAddress(const String& ip) {
    safeUpdateLabel(_ipLabel, ip);
}

void ProvisioningCard::updateSignalStrength(int strength) {
    const int boundedStrength = std::max(0, std::min(100, strength));
    safeUpdateLabel(_signalLabel, String(boundedStrength) + "%");
}

void ProvisioningCard::showQRCode() {
    const String ssid = _wifiInterface.getSSID();
    const String qrData = generateQRCodeData(ssid, "");
    const String ssidText = "SSID: " + ssid;
    lv_obj_t* showScreen = _qrScreen;
    lv_obj_t* hideScreen = _statusScreen;
    lv_obj_t* qrCode = _qrCode;
    lv_obj_t* ssidLabel = _ssidLabel;

    runOnUiTask([showScreen, hideScreen, qrCode, ssidLabel, qrData, ssidText]() {
        if (showScreen && lv_obj_is_valid(showScreen)) {
            lv_obj_clear_flag(showScreen, LV_OBJ_FLAG_HIDDEN);
        }
        if (hideScreen && lv_obj_is_valid(hideScreen)) {
            lv_obj_add_flag(hideScreen, LV_OBJ_FLAG_HIDDEN);
        }
        if (qrCode && lv_obj_is_valid(qrCode)) {
            lv_qrcode_update(qrCode, qrData.c_str(), qrData.length());
        }
        if (ssidLabel && lv_obj_is_valid(ssidLabel)) {
            lv_label_set_text(ssidLabel, ssidText.c_str());
        }
    });
}

void ProvisioningCard::showWiFiStatus() {
    toggleScreens(_statusScreen, _qrScreen);
}

void ProvisioningCard::createQRScreen() {
    lv_obj_set_style_bg_color(_qrScreen, Style::backgroundColor(), 0);
    lv_obj_set_style_pad_all(_qrScreen, 0, 0);
    lv_obj_set_style_border_width(_qrScreen, 0, 0);
    
    // Create and configure the top-left version label
    _topLeftVersionLabel = lv_label_create(_qrScreen); // Parent is now _qrScreen
    lv_obj_set_style_text_font(_topLeftVersionLabel, Style::labelFont(), 0);
    lv_obj_set_style_text_color(_topLeftVersionLabel, Style::labelColor(), 0);
    lv_label_set_text(_topLeftVersionLabel, CURRENT_FIRMWARE_VERSION);
    lv_obj_align(_topLeftVersionLabel, LV_ALIGN_TOP_LEFT, 5, 5); // 5px padding from top-left
    lv_obj_move_foreground(_topLeftVersionLabel); // Ensure it's on top within _qrScreen

    // Define layout parameters
    const int estimated_label_height = 16; // Estimated height for the SSID label
    const int padding_qr_to_label = 5;     // Padding between QR code and SSID label
    const int screen_top_padding = 5;      // Padding at the top of the QR screen area
    const int screen_bottom_padding = 5;   // Padding at the bottom of the QR screen area

    // Calculate QR code size
    // qr_size = _height (total card height) - top_pad - bottom_pad - space_between_qr_label - label_height
    const int qr_size = _height - screen_top_padding - screen_bottom_padding - padding_qr_to_label - estimated_label_height;
    
    // Create a placeholder QR code (will be updated later)
    _qrCode = lv_qrcode_create(_qrScreen);
    lv_qrcode_set_size(_qrCode, qr_size);
    lv_qrcode_set_dark_color(_qrCode, lv_color_black());
    lv_qrcode_set_light_color(_qrCode, lv_color_white()); 
    // Align QR code to the top-center with padding
    lv_obj_align(_qrCode, LV_ALIGN_TOP_MID, 0, screen_top_padding);
    
    // Initialize with placeholder text
    lv_qrcode_update(_qrCode, "WIFI:T:WPA;", 10);

    // Create SSID label beneath the QR code
    _ssidLabel = lv_label_create(_qrScreen);
    lv_obj_set_style_text_font(_ssidLabel, Style::valueFont(), 0);
    lv_obj_set_style_text_color(_ssidLabel, Style::valueColor(), 0);
    lv_obj_set_width(_ssidLabel, LV_PCT(100)); // Set label width to fill screen width
    lv_obj_set_style_text_align(_ssidLabel, LV_TEXT_ALIGN_CENTER, 0); // Center text within the label

    // Set initial SSID text - will be updated by showQRCode if/when called
    const String& currentSsid = _wifiInterface.getSSID();
    lv_label_set_text(_ssidLabel, ("SSID: " + currentSsid).c_str());
    lv_obj_align_to(_ssidLabel, _qrCode, LV_ALIGN_OUT_BOTTOM_MID, 0, padding_qr_to_label); // 5px padding below QR code
}

void ProvisioningCard::createStatusScreen() {
    lv_obj_set_style_bg_color(_statusScreen, Style::backgroundColor(), 0);
    lv_obj_set_style_pad_all(_statusScreen, 0, 0);
    lv_obj_set_style_border_width(_statusScreen, 0, 0);
    
    // Create container for status items
    lv_obj_t* table = lv_obj_create(_statusScreen);
    lv_obj_set_size(table, LV_PCT(100), LV_SIZE_CONTENT); // Width 100%, height adjusts to content
    lv_obj_set_style_pad_hor(table, 5, 0);  // 5px horizontal padding for the table itself
    lv_obj_set_style_pad_ver(table, 5, 0);  // Add some vertical padding for the table itself
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_bg_opa(table, 0, 0); // Make table background transparent

    // Configure Flexbox layout for the table (parent of rows)
    lv_obj_set_layout(table, LV_LAYOUT_FLEX); // Enable Flexbox
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN); // Arrange children (rows) in a column
    lv_obj_set_flex_align(table, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START); // Main-axis, Cross-axis, Track-cross-axis alignment
    lv_obj_set_style_pad_row(table, 3, 0); // Spacing between rows (flex items)
    
    // Create rows with title and value labels - order of calls now determines visual order
    createTableRow(table, "WiFi", &_statusLabel, Style::labelColor());
    createTableRow(table, "IP", &_ipLabel, Style::labelColor());
    createTableRow(table, "Signal", &_signalLabel, Style::labelColor());
    createTableRow(table, "Version", &_versionLabel, Style::labelColor());
    
    // Set initial values (some will be updated by handleSystemStateChange or other methods)
    lv_label_set_text(_statusLabel, "Initializing..."); 
    lv_label_set_text(_ipLabel, "");
    lv_label_set_text(_signalLabel, "0%");
    lv_label_set_text(_versionLabel, CURRENT_FIRMWARE_VERSION);
}

void ProvisioningCard::createTableRow(lv_obj_t* table, const char* title, 
                                     lv_obj_t** valueLabel, lv_color_t labelColor) {
    // Line height is no longer needed here for direct positioning, 
    // but can be useful if row containers need explicit height.
    // For now, LV_SIZE_CONTENT on the container is preferred with Flexbox.
    
    // Create container for the row content
    lv_obj_t* container = lv_obj_create(table); // Parent is the flex-container table
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT); // Width 100% of parent, height by content
    lv_obj_set_style_pad_all(container, 0, 0); // No padding within the row container itself
    lv_obj_set_style_bg_opa(container, 0, 0); // Make row container background transparent
    lv_obj_set_style_border_width(container, 0, 0);
    // lv_obj_set_pos is no longer needed; Flexbox handles positioning.
    
    // Create title label (left-aligned)
    lv_obj_t* titleLabel = lv_label_create(container);
    lv_obj_set_style_text_font(titleLabel, Style::labelFont(), 0);
    lv_obj_set_style_text_color(titleLabel, labelColor, 0);
    lv_label_set_text(titleLabel, title);
    lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 0, 0);
    
    // Create value label (right-aligned)
    *valueLabel = lv_label_create(container);
    lv_obj_set_style_text_font(*valueLabel, Style::valueFont(), 0);
    lv_obj_set_style_text_color(*valueLabel, Style::valueColor(), 0);
    lv_obj_align(*valueLabel, LV_ALIGN_RIGHT_MID, 0, 0);
}

String ProvisioningCard::generateQRCodeData(const String& ssid, const String& password) {
    // Escape special characters in SSID and password
    String escapedSsid = escapeString(ssid);
    String escapedPassword = escapeString(password);
    
    // For open networks
    if (password.length() == 0) {
        return "WIFI:S:" + escapedSsid + ";T:nopass;;";
    }
    
    // For password-protected networks
    return "WIFI:S:" + escapedSsid + ";T:WPA;P:" + escapedPassword + ";;";
}

String ProvisioningCard::escapeString(const String& input) {
    String result = input;
    result.replace("\\", "\\\\");
    result.replace(";", "\\;");
    result.replace(",", "\\,");
    result.replace("\"", "\\\"");
    result.replace("'", "\\'");
    return result;
}

// This card is updated from the WiFi task and the event task, but LVGL may only
// be touched from the UI task or under its mutex. So every update is handed to
// the UI task, which runs it holding the mutex.
//
// (These used to create an LVGL timer from whatever task they were on. That
// allocates from LVGL's heap with no lock, and it crashed the device when it
// raced with the UI task drawing.)
void ProvisioningCard::runOnUiTask(std::function<void()> work) {
    if (!globalUIDispatch) return;
    globalUIDispatch([work]() {
        if (!displayInterface || !displayInterface->takeMutex(pdMS_TO_TICKS(500))) return;
        work();
        displayInterface->giveMutex();
    }, false);
}

void ProvisioningCard::safeUpdateLabel(lv_obj_t* label, const String& text) {
    runOnUiTask([label, text]() {
        if (label && lv_obj_is_valid(label)) {
            lv_label_set_text(label, text.c_str());
        }
    });
}

void ProvisioningCard::toggleScreens(lv_obj_t* showScreen, lv_obj_t* hideScreen) {
    runOnUiTask([showScreen, hideScreen]() {
        if (showScreen && lv_obj_is_valid(showScreen)) {
            lv_obj_clear_flag(showScreen, LV_OBJ_FLAG_HIDDEN);
        }
        if (hideScreen && lv_obj_is_valid(hideScreen)) {
            lv_obj_add_flag(hideScreen, LV_OBJ_FLAG_HIDDEN);
        }
    });
}

lv_obj_t* ProvisioningCard::getCard() const {
    return _card;
}

// Implementation for SystemController integration
void ProvisioningCard::handleSystemStateChange(const ControllerState& newState) {
    // Update WiFi Status label (SSID if connected, otherwise relies on updateConnectionStatus)
    if (_statusLabel && lv_obj_is_valid(_statusLabel)) {
        if (newState.wifi_state == WifiState::CONNECTED) {
            safeUpdateLabel(_statusLabel, _wifiInterface.getSSID());
        }
        // If not connected, updateConnectionStatus() is expected to be called externally 
        // with a more specific status (e.g., "Connecting...", "Failed", "Disconnected").
        // So, we don't set a generic "Disconnected" here to avoid overwriting specific statuses.
    }
}
