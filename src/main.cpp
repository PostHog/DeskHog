/**
 * DeskHog - PostHog Analytics Display
 * ===================================
 * 
 * An ESP32-S3-based project that displays PostHog analytics insights on a 240x135 TFT screen.
 * Built for the Adafruit ESP32-S3 Reverse TFT Feather, this project provides a compact, desk-friendly way to monitor your PostHog analytics in real-time.
 * 
 * Components:
 * - Hardware: ESP32-S3 with integrated 240x135 TFT display
 * - UI: LVGL-based card interface for displaying insights
 * - Network: WiFi connectivity with captive portal for configuration
 * 
 * Development note:
 * Keep this main.cpp file lean, tidy and simple. Do not leak implementation details into the main file.
 * Keep main.cpp focused on setup and task creation. Encapsulate and isolate components into their own files.
 */

#include <Arduino.h>
#include <lvgl.h>
#include <Adafruit_ST7789.h>
#include "ConfigManager.h"
#include "hardware/WifiInterface.h"
#include "hardware/NeoPixelController.h"
#include "ui/CaptivePortal.h"
#include "ui/ProvisioningCard.h"
#include "hardware/DisplayInterface.h"
#include "ui/CardNavigationStack.h"
#include "ui/InsightCard.h"
#include "hardware/Input.h"
#include "posthog/PostHogClient.h"
#include "Style.h"
#include "esp_heap_caps.h" // For PSRAM management
#include "ui/CardController.h"
#include "EventQueue.h"
#include "esp_partition.h" // Include for partition functions
#include "OtaManager.h"
#include <esp_sleep.h> // Added for deep sleep functionality

// Display dimensions
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 135

// LVGL display buffer size
#define LVGL_BUFFER_ROWS 135  // Full screen height

// Button configuration
#define NUM_BUTTONS 3
const uint8_t BUTTON_PINS[NUM_BUTTONS] = {
    Input::BUTTON_DOWN,    // BOOT button
    Input::BUTTON_CENTER,  // Center button
    Input::BUTTON_UP       // Up button
};

// Define the global buttons array
Bounce2::Button buttons[NUM_BUTTONS];

// Global objects
DisplayInterface* displayInterface;
ConfigManager* configManager;
WiFiInterface* wifiInterface;
CaptivePortal* captivePortal;
CardController* cardController; // Replace individual card objects with controller
PostHogClient* posthogClient;
EventQueue* eventQueue; // Add global EventQueue
NeoPixelController* neoPixelController;  // Renamed from neoPixelManager
OtaManager* otaManager;

// Task handles
TaskHandle_t wifiTask;
TaskHandle_t portalTask;
TaskHandle_t insightTask;
TaskHandle_t neoPixelTask;

// WiFi connection timeout in milliseconds
#define WIFI_TIMEOUT 30000

// WiFi task that handles WiFi operations
void wifiTaskFunction(void* parameter) {
    while (1) {
        // Process WiFi events
        wifiInterface->process();
        
        // Delay to prevent hogging CPU
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Captive portal task that handles web server requests
void portalTaskFunction(void* parameter) {
    while (1) {
        captivePortal->processAsyncOperations(); // Process pending portal actions
        // Delay to prevent hogging CPU
        vTaskDelay(pdMS_TO_TICKS(100)); // Check for operations every 100ms
    }
}

// Insight processing task
void insightTaskFunction(void* parameter) {
    while (1) {
        posthogClient->process();  // Use the global instance
        vTaskDelay(pdMS_TO_TICKS(100));  // Check every 100ms
    }
}

// LVGL handler task that includes button polling - added here to consolidate UI operations
void lvglHandlerTask(void* parameter) {
    TickType_t lastButtonCheck = xTaskGetTickCount();
    const TickType_t buttonCheckInterval = pdMS_TO_TICKS(50); // Check buttons every 50ms
    
    static unsigned long powerOffPressStartTime = 0;
    // static bool upPressedState = false; // Unused
    // static bool downPressedState = false; // Unused

    while (1) {
        // Handle LVGL tasks
        displayInterface->handleLVGLTasks();

        cardController->processUIQueue();
        
        // Poll buttons at regular intervals
        TickType_t currentTime = xTaskGetTickCount();
        if ((currentTime - lastButtonCheck) >= buttonCheckInterval) {
            lastButtonCheck = currentTime;
            
            // Update all buttons first
            for (int i = 0; i < NUM_BUTTONS; i++) {
                buttons[i].update();
            }

            // Get current state of UP and DOWN buttons
            // BUTTON_UP is pressed when HIGH (INPUT_PULLDOWN)
            // BUTTON_DOWN is pressed when LOW (INPUT_PULLUP)
            bool centerButtonHeld = (buttons[Input::BUTTON_CENTER].read() == HIGH);
            bool downButtonHeld = (buttons[Input::BUTTON_DOWN].read() == LOW);

            if (centerButtonHeld && downButtonHeld) {
                if (powerOffPressStartTime == 0) { // Both pressed, start timer
                    powerOffPressStartTime = millis();
                } else {
                    if (millis() - powerOffPressStartTime >= 2000) { // Held for 2 seconds
                        Serial.println("Simultaneous CENTER and DOWN hold for 2s detected. Entering deep sleep.");
                        // Optional: Turn off display backlight or other peripherals before sleep
                        // displayInterface->setBacklight(0); // Example if such a function exists
                        esp_deep_sleep_start();
                    }
                }
            } else {
                // If either button is released or not simultaneously pressed, reset the timer
                powerOffPressStartTime = 0;

                // Process individual button presses if power-off sequence is not active or completed.
                // Check .pressed() for single press actions (triggers on state change).
                // This ensures that navigation still works if the power-off combo isn't fully executed.
                if (buttons[Input::BUTTON_UP].pressed()) {
                    // Ensure this doesn't trigger if it was part of the combo that just got released
                    // However, .pressed() fires on the transition, so if it was held and released, 
                    // it won't fire again here unless pressed again separately.
                    cardController->getCardStack()->handleButtonPress(Input::BUTTON_UP);
                }
                if (buttons[Input::BUTTON_DOWN].pressed()) {
                    cardController->getCardStack()->handleButtonPress(Input::BUTTON_DOWN);
                }
                if (buttons[Input::BUTTON_CENTER].pressed()) {
                    cardController->getCardStack()->handleButtonPress(Input::BUTTON_CENTER);
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// NeoPixel update task
void neoPixelTaskFunction(void* parameter) {
    while (1) {
        neoPixelController->update();
        vTaskDelay(pdMS_TO_TICKS(5));  // Small delay to prevent task starvation
    }
}

void setup() {
    Serial.begin(115200); // Keep Serial.begin() as it initializes the port
    delay(100);  // Give serial port time to initialize
    Serial.println("Starting up...");

    if (psramInit()) {
        Serial.println("PSRAM initialized successfully");
        Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
        Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
        
        // Set memory allocation preference to PSRAM
        heap_caps_malloc_extmem_enable(4096);
    } else {
        Serial.println("PSRAM initialization failed!");
        while(1); // Stop here if PSRAM init fails
    }

    // Add Partition Logging Here
    Serial.println("--- Partition Table Info ---");
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (it == NULL) {
        Serial.println("Could not find partitions!");
    } else {
        while (it != NULL) {
            const esp_partition_t *p = esp_partition_get(it);
            if (p == NULL) {
                // Added null check for safety, might occur if iterator is exhausted
                it = esp_partition_next(it); // Make sure to advance iterator even if partition is null
                continue;
            }
            Serial.printf("  Label: %-10s Type: 0x%02x Subtype: 0x%02x Offset: 0x%08x Size: 0x%08x (%d KB)\n",
                          p->label, p->type, p->subtype, p->address, p->size, p->size / 1024);
            it = esp_partition_next(it);
        }
        esp_partition_iterator_release(it); // Release the iterator!
    }
    Serial.println("--------------------------");

    // UI queue will be initialized by CardController


    SystemController::begin();
    
    // Initialize styles and fonts
    Style::init();
    
    // Initialize event queue first
    eventQueue = new EventQueue(20); // Create queue with capacity for 20 events
    eventQueue->begin(); // Start event processing
    
    // Initialize NeoPixel controller
    neoPixelController = new NeoPixelController();
    neoPixelController->begin();
    
    // Initialize config manager with event queue
    configManager = new ConfigManager(*eventQueue);
    configManager->begin();
    
    // Initialize PostHog client with event queue
    posthogClient = new PostHogClient(*configManager, *eventQueue);
    
    // Initialize display manager
    displayInterface = new DisplayInterface(
        SCREEN_WIDTH, SCREEN_HEIGHT, LVGL_BUFFER_ROWS, 
        TFT_CS, TFT_DC, TFT_RST, TFT_BACKLITE
    );
    displayInterface->begin();
    
    // Initialize WiFi manager with event queue
    wifiInterface = new WiFiInterface(*configManager, *eventQueue);
    wifiInterface->begin();
    
    // Initialize buttons
    Input::configureButtons();
    
    // Create and initialize card controller
    cardController = new CardController(
        lv_scr_act(),
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        *configManager,
        *wifiInterface,
        *posthogClient,
        *eventQueue
    );
    
    // Initialize with display interface directly
    cardController->initialize(displayInterface);
    
    // Initialize OtaManager
    otaManager = new OtaManager(CURRENT_FIRMWARE_VERSION, "PostHog", "DeskHog");
    
    // Initialize captive portal
    captivePortal = new CaptivePortal(*configManager, *wifiInterface, *eventQueue, *otaManager, *cardController);
    captivePortal->begin();
    
    // Create task for WiFi operations
    xTaskCreatePinnedToCore(
        wifiTaskFunction,
        "wifiTask",
        4096,
        NULL,
        1,
        &wifiTask,
        0
    );
    
    // Create task for captive portal
    xTaskCreatePinnedToCore(
        portalTaskFunction,
        "portalTask",
        8192,
        NULL,
        1,
        &portalTask,
        1
    );
    
    // Create task for insight processing
    xTaskCreatePinnedToCore(
        insightTaskFunction,
        "insightTask",
        8192,
        NULL,
        1,
        &insightTask,
        0
    );
    
    // Create LVGL tick task
    xTaskCreatePinnedToCore(
        DisplayInterface::tickTask,
        "lv_tick_task",
        2048,
        NULL,
        1,
        NULL,
        1
    );
    
    // Create LVGL handler task (now includes button polling)
    xTaskCreatePinnedToCore(
        lvglHandlerTask,
        "lvglTask",
        8192,
        NULL,
        2,
        NULL,
        1
    );
    
    // Create NeoPixel task
    xTaskCreatePinnedToCore(
        neoPixelTaskFunction,
        "neoPixelTask",
        2048,
        NULL,
        1,
        &neoPixelTask,
        0
    );
    
    // Check if we have WiFi credentials and publish the appropriate event
    configManager->checkWiFiCredentialsAndPublish();

    SystemController::setSystemState(SystemState::SYS_READY);
}

void loop() {
    // Empty - tasks handle everything
    vTaskDelete(NULL);
    
}