#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * @brief Interface class for TFT display with LVGL integration
 * 
 * This class manages the initialization and operation of an ST7789 TFT display
 * through SPI and integrates it with the LVGL graphics library.
 */
class DisplayInterface {
public:
    /**
     * @brief Construct a new Display Interface object
     * 
     * @param screen_width Width of the display in pixels
     * @param screen_height Height of the display in pixels
     * @param buffer_rows Number of rows to buffer for LVGL rendering
     * @param cs_pin Chip select pin
     * @param dc_pin Data/command pin
     * @param rst_pin Reset pin
     * @param backlight_pin Backlight control pin
     */
    DisplayInterface(
        uint16_t screen_width,
        uint16_t screen_height,
        uint16_t buffer_rows,
        int8_t cs_pin,
        int8_t dc_pin, 
        int8_t rst_pin,
        int8_t backlight_pin
    );
    
    /**
     * @brief Destroy the Display Interface object and free resources
     */
    ~DisplayInterface();
    
    /**
     * @brief Initialize the display and LVGL
     */
    void begin();
    
    /**
     * @brief Get the underlying TFT display object
     * 
     * @return Adafruit_ST7789* Pointer to the display
     */
    Adafruit_ST7789* getDisplay();
    
    /**
     * @brief Process LVGL tasks (should be called regularly)
     */
    void handleLVGLTasks();
    
    /**
     * @brief FreeRTOS task for LVGL tick handling
     * 
     * @param arg Task argument (not used)
     */
    static void tickTask(void* arg);
    
    /**
     * @brief Acquire the LVGL mutex
     * 
     * @param timeout Timeout in ticks (default: portMAX_DELAY)
     * @return true if mutex was acquired
     * @return false if timeout occurred
     */
    bool takeMutex(TickType_t timeout = portMAX_DELAY);
    
    /**
     * @brief Release the LVGL mutex
     */
    void giveMutex();
    
    /**
     * @brief Get pointer to the mutex
     * 
     * @return SemaphoreHandle_t* Pointer to the mutex
     */
    SemaphoreHandle_t* getMutexPtr();
    
    /**
     * @brief Set display backlight brightness
     * 
     * @param brightness Brightness level (0-255)
     */
    void setBrightness(uint8_t brightness);

private:
    uint16_t _screen_width;
    uint16_t _screen_height;
    uint16_t _buffer_rows;
    int8_t _cs_pin;
    int8_t _dc_pin;
    int8_t _rst_pin;
    int8_t _backlight_pin;
    
    Adafruit_ST7789* _tft;
    lv_display_t* _display;
    lv_color_t* _buf1;
    lv_color_t* _buf2;
    SemaphoreHandle_t _lvgl_mutex;
    
    /**
     * @brief LVGL display flush callback
     * 
     * @param disp LVGL display
     * @param area Area to flush
     * @param px_map Pixel data
     */
    static void _disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    
    // Prevent copying
    DisplayInterface(const DisplayInterface&) = delete;
    DisplayInterface& operator=(const DisplayInterface&) = delete;
};