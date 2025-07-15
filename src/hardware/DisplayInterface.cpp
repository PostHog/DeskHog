#include "DisplayInterface.h"

// A pointer to the instance for use in static callbacks
static DisplayInterface* instance = nullptr;

DisplayInterface::DisplayInterface(
    uint16_t screen_width,
    uint16_t screen_height,
    uint16_t buffer_rows,
    int8_t cs_pin,
    int8_t dc_pin, 
    int8_t rst_pin,
    int8_t backlight_pin
) : _screen_width(screen_width),
    _screen_height(screen_height),
    _buffer_rows(buffer_rows),
    _cs_pin(cs_pin),
    _dc_pin(dc_pin),
    _rst_pin(rst_pin),
    _backlight_pin(backlight_pin),
    _tft(nullptr),
    _display(nullptr),
    _buf1(nullptr),
    _buf2(nullptr),
    _lvgl_mutex(nullptr) {
    
    // Store instance for static callbacks
    instance = this;
    
    // Create TFT object
    _tft = new Adafruit_ST7789(&SPI, _cs_pin, _dc_pin, _rst_pin);
    if (!_tft) {
        Serial.println("Failed to create TFT object");
        return;
    }
    
    // Allocate display buffers using C++ new operator instead of malloc
    try {
        _buf1 = new lv_color_t[_screen_width * _buffer_rows];
        _buf2 = new lv_color_t[_screen_width * _buffer_rows];
    } catch (std::bad_alloc&) {
        Serial.println("Failed to allocate display buffers");
        // Clean up already allocated resources
        delete _tft;
        _tft = nullptr;
        if (_buf1) {
            delete[] _buf1;
            _buf1 = nullptr;
        }
        return;
    }
    
    // Create LVGL mutex
    _lvgl_mutex = xSemaphoreCreateMutex();
    if (_lvgl_mutex == nullptr) {
        Serial.println("Could not create mutex");
        // Clean up already allocated resources
        delete _tft;
        _tft = nullptr;
        delete[] _buf1;
        _buf1 = nullptr;
        delete[] _buf2;
        _buf2 = nullptr;
        return;
    }
}

void DisplayInterface::begin() {
    // Check if initialization failed
    if (!_tft || !_buf1 || !_buf2 || !_lvgl_mutex) {
        Serial.println("Cannot initialize display: resources not allocated");
        return;
    }
    
    // Initialize SPI
    SPI.begin();
    
    // Initialize display
    _tft->init(_screen_height, _screen_width);
    _tft->setRotation(1);
    
    // Configure backlight with PWM for brightness control
    pinMode(_backlight_pin, OUTPUT);
    
    // Set up PWM on the backlight pin using new ESP32 Arduino Core 3.x API
    // Attach pin with 5000 Hz frequency and 8-bit resolution
    ledcAttach(_backlight_pin, 5000, 8);
    
    // Set brightness to 80% (204 out of 255)
    ledcWrite(_backlight_pin, 204);
    Serial.println("Display backlight set to 80% brightness");
    
    _tft->fillScreen(ST77XX_BLACK);
    
    // Initialize LVGL
    lv_init();
    
    // Initialize and register display for LVGL v9
    _display = lv_display_create(_screen_width, _screen_height);
    if (!_display) {
        Serial.println("Failed to create LVGL display");
        return;
    }
    
    lv_display_set_flush_cb(_display, _disp_flush);
    
    // Set the buffer correctly
    lv_display_set_buffers(
        _display, 
        _buf1, 
        _buf2, 
        _screen_width * _buffer_rows * sizeof(lv_color_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );
    
    // Set screen background to black
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(lv_scr_act(), 0, 0);
}

Adafruit_ST7789* DisplayInterface::getDisplay() {
    return _tft;
}

void DisplayInterface::handleLVGLTasks() {
    if (takeMutex()) {
        lv_timer_handler();
        giveMutex();
    }
}

void DisplayInterface::tickTask(void* arg) {
    (void)arg;  // Silence unused parameter warning
    while (1) {
        lv_tick_inc(10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool DisplayInterface::takeMutex(TickType_t timeout) {
    if (!_lvgl_mutex) {
        return false;
    }
    return xSemaphoreTake(_lvgl_mutex, timeout) == pdTRUE;
}

void DisplayInterface::giveMutex() {
    if (_lvgl_mutex) {
        xSemaphoreGive(_lvgl_mutex);
    }
}

SemaphoreHandle_t* DisplayInterface::getMutexPtr() {
    return &_lvgl_mutex;
}

void DisplayInterface::_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (instance && instance->_tft) {
        uint32_t w = (area->x2 - area->x1 + 1);
        uint32_t h = (area->y2 - area->y1 + 1);
        
        instance->_tft->startWrite();
        instance->_tft->setAddrWindow(area->x1, area->y1, w, h);
        instance->_tft->writePixels((uint16_t*)px_map, w * h);
        instance->_tft->endWrite();
    }
    
    lv_display_flush_ready(disp);
}

DisplayInterface::~DisplayInterface() {
    // Free resources in reverse order of allocation
    if (_lvgl_mutex) {
        vSemaphoreDelete(_lvgl_mutex);
        _lvgl_mutex = nullptr;
    }
    
    if (_buf2) {
        delete[] _buf2;  // Using delete[] for array allocations
        _buf2 = nullptr;
    }
    
    if (_buf1) {
        delete[] _buf1;  // Using delete[] for array allocations
        _buf1 = nullptr;
    }
    
    if (_tft) {
        delete _tft;
        _tft = nullptr;
    }
    
    // Reset the static instance pointer if it points to this object
    if (instance == this) {
        instance = nullptr;
    }
}

void DisplayInterface::setBrightness(uint8_t brightness) {
    // Update the PWM duty cycle on the backlight pin
    ledcWrite(_backlight_pin, brightness);
}