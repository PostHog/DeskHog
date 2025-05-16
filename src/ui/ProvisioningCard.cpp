#include "ProvisioningCard.h"
#include "Style.h"

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
    safeUpdateLabel(_statusLabel, status);
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
    const String& ssid = _wifiInterface.getSSID();
    const String qrData = generateQRCodeData(ssid, "");
    
    // Create struct for callback data
    struct QRUpdateData {
        lv_obj_t* show_screen;
        lv_obj_t* hide_screen;
        lv_obj_t* qr_code;
        String qr_data;
        lv_obj_t* ssid_label;
        String ssid_text;
        
        QRUpdateData(lv_obj_t* s, lv_obj_t* h, lv_obj_t* q, const String& d, lv_obj_t* sl, const String& st) 
            : show_screen(s), hide_screen(h), qr_code(q), qr_data(d), ssid_label(sl), ssid_text(st) {}
    };
    
    auto* data = new QRUpdateData(_qrScreen, _statusScreen, _qrCode, qrData, _ssidLabel, "SSID: " + ssid);
    
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* timer) {
        auto* params = static_cast<QRUpdateData*>(lv_timer_get_user_data(timer));
        if (params) {
            // Update screen visibility
            if (params->show_screen && lv_obj_is_valid(params->show_screen)) {
                lv_obj_clear_flag(params->show_screen, LV_OBJ_FLAG_HIDDEN);
            }
            if (params->hide_screen && lv_obj_is_valid(params->hide_screen)) {
                lv_obj_add_flag(params->hide_screen, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Update QR code
            if (params->qr_code && lv_obj_is_valid(params->qr_code)) {
                lv_qrcode_update(params->qr_code, params->qr_data.c_str(), params->qr_data.length());
            }

            // Update SSID label
            if (params->ssid_label && lv_obj_is_valid(params->ssid_label)) {
                lv_label_set_text(params->ssid_label, params->ssid_text.c_str());
            }
        }
        delete params;
        lv_timer_del(timer);
    }, 0, data);
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
    lv_obj_set_size(table, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(table, 5, 0);  // 5px horizontal padding
    lv_obj_set_style_pad_ver(table, 0, 0);  // No vertical padding
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_bg_opa(table, 0, 0);
    
    // Create rows with title and value labels
    createTableRow(table, 0, "WiFi", &_statusLabel, Style::labelColor());
    createTableRow(table, 1, "IP", &_ipLabel, Style::labelColor());
    createTableRow(table, 2, "Signal", &_signalLabel, Style::labelColor());
    createTableRow(table, 3, "Version", &_versionLabel, Style::labelColor());
    
    // Set initial values
    lv_label_set_text(_statusLabel, "Disconnected");
    lv_label_set_text(_ipLabel, "");
    lv_label_set_text(_signalLabel, "0%");
    lv_label_set_text(_versionLabel, CURRENT_FIRMWARE_VERSION);
}

void ProvisioningCard::createTableRow(lv_obj_t* table, uint16_t row, const char* title, 
                                     lv_obj_t** valueLabel, lv_color_t labelColor) {
    // Calculate line height based on font
    int lineHeight = lv_font_get_line_height(Style::valueFont()) + 5;
    
    // Create container for the row content
    lv_obj_t* container = lv_obj_create(table);
    lv_obj_set_size(container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_bg_opa(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_pos(container, 0, row * lineHeight);
    
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

void ProvisioningCard::safeUpdateLabel(lv_obj_t* label, const String& text) {
    // Make a copy of the string for the callback
    auto* callback = new std::pair<lv_obj_t*, String>(label, text);
    
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* timer) {
        auto* params = static_cast<std::pair<lv_obj_t*, String>*>(lv_timer_get_user_data(timer));
        if (params && params->first && lv_obj_is_valid(params->first)) {
            lv_label_set_text(params->first, params->second.c_str());
        }
        delete params;
        lv_timer_del(timer);
    }, 0, callback);
}

void ProvisioningCard::toggleScreens(lv_obj_t* showScreen, lv_obj_t* hideScreen) {
    auto* screens = new std::pair<lv_obj_t*, lv_obj_t*>(showScreen, hideScreen);
    
    lv_timer_t* timer = lv_timer_create([](lv_timer_t* timer) {
        auto* params = static_cast<std::pair<lv_obj_t*, lv_obj_t*>*>(lv_timer_get_user_data(timer));
        if (params) {
            if (params->first && lv_obj_is_valid(params->first)) {
                lv_obj_clear_flag(params->first, LV_OBJ_FLAG_HIDDEN);
            }
            if (params->second && lv_obj_is_valid(params->second)) {
                lv_obj_add_flag(params->second, LV_OBJ_FLAG_HIDDEN);
            }
        }
        delete params;
        lv_timer_del(timer);
    }, 0, screens);
}

lv_obj_t* ProvisioningCard::getCardObject() const {
    return _card;
}