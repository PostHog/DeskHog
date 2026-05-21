#include "OtaManager.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h> // For ESP32 Update functions
#include "esp_task_wdt.h"
#include <WiFi.h> // For WiFi.status() and WL_CONNECTED
#include "esp_ota_ops.h" // Needed for esp_ota_get_running_partition()

// For heap_caps_malloc and esp_ptr_external_ram, ensure correct include if not already covered by Arduino.h/ESP-IDF basics
// #include "esp_heap_caps.h" // Already in OtaManager.h but good to be mindful

#if ARDUINOJSON_VERSION_MAJOR >= 6 && ARDUINOJSON_VERSION_MINOR >= 18
    #define OTAMANAGER_ARDUINOJSON_ENABLE_PSRAM ARDUINOJSON_ENABLE_PSRAM
#else
    #define OTAMANAGER_ARDUINOJSON_ENABLE_PSRAM 0 // Older versions might not have this macro defined
#endif

// Define BOARD_HAS_PSRAM in your platformio.ini or build flags if not auto-defined
// For example: build_flags = -DBOARD_HAS_PSRAM
// Or check for a specific board macro if your framework provides one.
#ifndef BOARD_HAS_PSRAM
    // Attempt to infer from common ESP32 SDK configs if not explicitly defined by build system
    #if CONFIG_ESP32_SPIRAM_SUPPORT || CONFIG_SPIRAM_SUPPORT // Common SDK config names
        #define BOARD_HAS_PSRAM 1
    #else
        #define BOARD_HAS_PSRAM 0
    #endif
#endif

#if OTAMANAGER_ARDUINOJSON_ENABLE_PSRAM && BOARD_HAS_PSRAM
struct PsramAllocator {
    // Check if PSRAM is available and initialized.
    // This function can be called once at setup or less frequently.
    static bool psramAvailable() {
        return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
    }

    void* allocate(size_t size) {
        if (psramAvailable()) {
            // Serial.printf("PsramAllocator: Attempting to allocate %u bytes from PSRAM\n", size);
            void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
            // if (!p) Serial.println("PsramAllocator: heap_caps_malloc failed!");
            return p;
        }
        // Serial.printf("PsramAllocator: PSRAM not available, falling back to internal RAM for %u bytes\n", size);
        return malloc(size); // Fallback to internal RAM
    }

    void deallocate(void* pointer) {
        if (!pointer) return;
        if (esp_ptr_external_ram(pointer)) {
            // Serial.println("PsramAllocator: Deallocating from PSRAM");
            heap_caps_free(pointer);
        } else {
            // Serial.println("PsramAllocator: Deallocating from internal RAM");
            free(pointer);
        }
    }

    void* reallocate(void* ptr, size_t new_size) {
        if (esp_ptr_external_ram(ptr)) {
            // Serial.printf("PsramAllocator: Reallocating from PSRAM, new size %u\n", new_size);
            return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
        } else {
            // Serial.printf("PsramAllocator: Reallocating from internal RAM, new size %u\n", new_size);
            return realloc(ptr, new_size);
        }
    }
};
#endif

// Structure to pass parameters to the update task
struct UpdateTaskParams {
    OtaManager* otaManagerInstance;
    char* downloadUrl;
};

// Constructor
OtaManager::OtaManager(const String& currentVersion, const String& repoOwner, const String& repoName)
    : _currentVersion(currentVersion),
      _repoOwner(repoOwner),
      _repoName(repoName),
      _checkTaskHandle(NULL),
      _updateTaskHandle(NULL),
      _timeSynced(false) { // Initialize _timeSynced
    _currentStatus.status = UpdateStatus::State::IDLE;
    _currentStatus.message = "Idle";
    _currentStatus.progress = 0;
    _lastCheckResult.updateAvailable = false;
    _lastCheckResult.currentVersion = _currentVersion;

    // only one watchdog timer should be initialized for the whole project
    // if this is needed elsewhere, it should be hoisted to a shared file/location
    esp_task_wdt_config_t twdt_config_update = {
        .timeout_ms = 120000, // 120 seconds
        .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,
        .trigger_panic = true
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config_update));

    _dataMutex = xSemaphoreCreateMutex();
    if (_dataMutex == NULL) {
        Serial.println("Error: Failed to create OtaManager data mutex!");
        // Handle error appropriately - perhaps a critical system halt or error state
    }
}

// Public methods
bool OtaManager::checkForUpdate() {
    if (_dataMutex) {
        if (xSemaphoreTake(_dataMutex, (TickType_t)10) == pdTRUE) { // Try to take mutex with short timeout
            if (_currentStatus.status == UpdateStatus::State::CHECKING_VERSION ||
                _currentStatus.status == UpdateStatus::State::DOWNLOADING ||
                _currentStatus.status == UpdateStatus::State::WRITING) {
                Serial.println("OtaManager: Check or update already in progress.");
                xSemaphoreGive(_dataMutex);
                return false; // Already checking or updating
            }
            // Minimal state to set before potentially long NTP sync
            _currentStatus.status = UpdateStatus::State::CHECKING_VERSION;
            _currentStatus.message = "Initializing update check...";
            _currentStatus.progress = 0;
            _lastCheckResult.error = ""; // Clear previous error before new check cycle
            _lastCheckResult.updateAvailable = false;
            _lastCheckResult.availableVersion = "";
            _lastCheckResult.downloadUrl = "";
            _lastCheckResult.releaseNotes = "";
            // Log this initial state change (moved from _setUpdateStatus for this specific case)
            Serial.printf("OtaManager Status: [%d] %s (%d%%)\n", static_cast<int>(_currentStatus.status), _currentStatus.message.c_str(), _currentStatus.progress);
            xSemaphoreGive(_dataMutex);
        } else {
            Serial.println("OtaManager: checkForUpdate could not acquire mutex to start check.");
            return false; // Could not get mutex, effectively busy
        }
    } else {
        Serial.println("OtaManager: _dataMutex is NULL in checkForUpdate.");
        return false; // Critical error
    }

    // Create a task to handle the update check.
    // The task will be responsible for NTP sync and then the actual check.
    if (xTaskCreatePinnedToCore(
            _checkUpdateTaskRunner,   /* Task function */
            "otaCheckTask",         /* Name of task */
            8192,                   /* Stack size of task */
            this,                   /* Parameter of the task */
            1,                      /* Priority of the task */
            &_checkTaskHandle,      /* Task handle to keep track of the task */
            0                       /* Core pin, 0 for PRO_CPU */
        ) != pdPASS) {
        Serial.println("OtaManager: Failed to create check update task.");
        if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
        _setUpdateStatus(UpdateStatus::State::IDLE, "Failed to start check task.");
        _lastCheckResult.error = "Failed to start check task.";
        if (_dataMutex) xSemaphoreGive(_dataMutex);
        return false;
    }
    return true;
}

bool OtaManager::beginUpdate(const String& downloadUrl) {
    Serial.printf("OtaManager: [beginUpdate] Entered. Download URL: %s\n", downloadUrl.c_str());

    if (downloadUrl.isEmpty()) {
        Serial.println("OtaManager: [beginUpdate] Error: Download URL is empty.");
        _setUpdateStatus(UpdateStatus::State::ERROR_UPDATE_BEGIN, "Download URL is empty.");
        return false;
    }

    if (_dataMutex) {
        Serial.println("OtaManager: [beginUpdate] Attempting to take dataMutex.");
        if (xSemaphoreTake(_dataMutex, portMAX_DELAY) == pdTRUE) {
            Serial.println("OtaManager: [beginUpdate] dataMutex taken.");
        } else {
            Serial.println("OtaManager: [beginUpdate] CRITICAL: Failed to take dataMutex.");
            // Potentially return or handle error, though portMAX_DELAY should wait indefinitely
        }
    } else {
        Serial.println("OtaManager: [beginUpdate] CRITICAL: _dataMutex is NULL.");
        _setUpdateStatus(UpdateStatus::State::ERROR_INTERNAL, "Mutex not initialized in beginUpdate");
        return false; // Cannot proceed without mutex
    }

    Serial.printf("OtaManager: [beginUpdate] Before status check. Raw _currentStatus.status: %d\n", static_cast<int>(_currentStatus.status)); // Log the raw status value
    if (_currentStatus.status == UpdateStatus::State::DOWNLOADING || _currentStatus.status == UpdateStatus::State::WRITING) {
        Serial.println("OtaManager: [beginUpdate] Error: Update already in progress.");
        if (_dataMutex) xSemaphoreGive(_dataMutex); // Release mutex before returning
        Serial.println("OtaManager: [beginUpdate] dataMutex given (update already in progress).");
        return false;
    }
    // If we are here, status is NOT DOWNLOADING or WRITING. Mutex is still held.
    // Crucial change: Release mutex BEFORE calling _setUpdateStatus, as _setUpdateStatus will take it.
    if (_dataMutex) {
        xSemaphoreGive(_dataMutex);
        Serial.println("OtaManager: [beginUpdate] dataMutex given (before calling _setUpdateStatus).");
    } else {
        Serial.println("OtaManager: [beginUpdate] CRITICAL: _dataMutex was NULL before trying to give it prior to _setUpdateStatus call. This shouldn't happen if taken above.");
        // This is a sanity check; if mutex was taken successfully, it shouldn't be NULL here.
    }

    _setUpdateStatus(UpdateStatus::State::DOWNLOADING, "Starting update...", 0);
    // _setUpdateStatus will handle its own mutex locking for setting the status.

    // The rest of the function does not need to re-take the mutex for its operations (strdup, malloc, xTaskCreatePinnedToCore)
    // unless _updateTaskHandle itself needs protection, but xTaskCreatePinnedToCore is task-safe for the handle.

    Serial.println("OtaManager: [beginUpdate] About to allocate memory for URL copy.");
    char* urlCopy = strdup(downloadUrl.c_str());
    if (!urlCopy) {
        Serial.println("OtaManager: [beginUpdate] Error: Failed to allocate memory for URL copy.");
        _setUpdateStatus(UpdateStatus::State::ERROR_UPDATE_BEGIN, "Memory allocation failed for URL.");
        return false;
    }
    Serial.printf("OtaManager: [beginUpdate] URL copied to: %p, Content: %s\n", (void*)urlCopy, urlCopy);

    Serial.println("OtaManager: [beginUpdate] About to allocate memory for UpdateTaskParams struct.");
    UpdateTaskParams* taskParams = (UpdateTaskParams*)malloc(sizeof(UpdateTaskParams));
    if (!taskParams) {
        Serial.println("OtaManager: [beginUpdate] Error: Failed to allocate memory for task params.");
        free(urlCopy); // Clean up urlCopy if params allocation fails
        Serial.println("OtaManager: [beginUpdate] Freed urlCopy due to taskParams allocation failure.");
        _setUpdateStatus(UpdateStatus::State::ERROR_UPDATE_BEGIN, "Memory allocation failed for task params.");
        return false;
    }
    taskParams->otaManagerInstance = this;
    taskParams->downloadUrl = urlCopy;
    Serial.printf("OtaManager: [beginUpdate] taskParams allocated at %p. otaManagerInstance: %p, downloadUrl (copied): %p -> %s\n", (void*)taskParams, (void*)taskParams->otaManagerInstance, (void*)taskParams->downloadUrl, taskParams->downloadUrl);

    Serial.println("OtaManager: [beginUpdate] About to call xTaskCreatePinnedToCore for _updateTaskRunner.");
    BaseType_t taskCreateResult = xTaskCreatePinnedToCore(
            _updateTaskRunner,      /* Task function */
            "otaUpdateTask",        /* Name of task */
            12288,                  /* Stack size of task (increased for HTTPS and flashing) */
            (void*)taskParams,      /* Parameter of the task (pass the new struct) */
            2,                      /* Priority of the task (higher than check) */
            &_updateTaskHandle,     /* Task handle */
            0                       /* Core pin */
        );

    Serial.printf("OtaManager: [beginUpdate] xTaskCreatePinnedToCore result: %s\n", (taskCreateResult == pdPASS) ? "pdPASS" : "pdFAIL");

    if (taskCreateResult != pdPASS) {
        Serial.println("OtaManager: [beginUpdate] Error: Failed to create update task.");
        free(taskParams->downloadUrl); // Free the copied URL
        Serial.println("OtaManager: [beginUpdate] Freed taskParams->downloadUrl due to task creation failure.");
        free(taskParams);              // Free the params struct
        Serial.println("OtaManager: [beginUpdate] Freed taskParams struct due to task creation failure.");
        // Mutex should be taken before setting status if beginUpdate can be called from multiple contexts
        // However, at this point, it was released earlier. For safety, re-take if setting status here.
        if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
        _setUpdateStatus(UpdateStatus::State::IDLE, "Failed to start update task");
        if (_dataMutex) xSemaphoreGive(_dataMutex);
        return false;
    }
    // taskParams and taskParams->downloadUrl will be freed by _updateTaskRunner
    Serial.println("OtaManager: [beginUpdate] Update task created successfully. Exiting beginUpdate.");
    return true;
}

UpdateStatus OtaManager::getStatus() {
    UpdateStatus currentStatusCopy;
    if (_dataMutex) {
        if (xSemaphoreTake(_dataMutex, (TickType_t)0) == pdTRUE) {
            currentStatusCopy = _currentStatus;
            xSemaphoreGive(_dataMutex);
        } else {
            // Mutex is busy, return a status indicating this clearly
            currentStatusCopy.status = UpdateStatus::State::MUTEX_BUSY;
            currentStatusCopy.message = "OTA manager busy, status temporarily unavailable.";
            currentStatusCopy.progress = 0; // Or -1, indicate progress unknown
        }
    } else {
        // Mutex not initialized, critical error
        currentStatusCopy.status = UpdateStatus::State::ERROR_INTERNAL;
        currentStatusCopy.message = "OTA mutex error (not initialized).";
        currentStatusCopy.progress = 0;
    }
    return currentStatusCopy;
}

UpdateInfo OtaManager::getLastCheckResult() {
    UpdateInfo lastCheckResultCopy;
    if (_dataMutex) {
        if (xSemaphoreTake(_dataMutex, (TickType_t)0) == pdTRUE) {
            lastCheckResultCopy = _lastCheckResult;
            xSemaphoreGive(_dataMutex);
        } else {
            // Mutex is busy, return a status indicating this clearly
            // Initialize to safe defaults, indicating data is unavailable
            lastCheckResultCopy.updateAvailable = false;
            lastCheckResultCopy.currentVersion = _currentVersion; // Current version is known, not from shared state
            lastCheckResultCopy.availableVersion = "";
            lastCheckResultCopy.downloadUrl = "";
            lastCheckResultCopy.releaseNotes = "";
            lastCheckResultCopy.error = "OTA manager busy, check result temporarily unavailable.";
        }
    } else {
        // Mutex not initialized, critical error
        lastCheckResultCopy.currentVersion = _currentVersion;
        lastCheckResultCopy.error = "OTA mutex error (not initialized).";
    }
    return lastCheckResultCopy;
}

void OtaManager::process() {
    // This method could be used if not using tasks, or for tasks to yield/delay.
    // For now, tasks handle themselves.
}

// Private helper methods
void OtaManager::_setUpdateStatus(UpdateStatus::State state, const String& message, int progress) {
    Serial.printf("OtaManager: [_setUpdateStatus] Entered. Requested State: %d, Msg: %s, Prog: %d\n", static_cast<int>(state), message.c_str(), progress);

    // Local copies for logging after mutex release
    UpdateStatus::State localState = state;
    String localMessage = message;
    int localProgress = progress;
    int actualProgressForLogging = 0; // Will be updated if progress is valid

    if (_dataMutex) {
        Serial.println("OtaManager: [_setUpdateStatus] Attempting to take dataMutex.");
        if (xSemaphoreTake(_dataMutex, portMAX_DELAY) == pdTRUE) {
            Serial.println("OtaManager: [_setUpdateStatus] dataMutex taken.");
            _currentStatus.status = localState;
            Serial.printf("OtaManager: [_setUpdateStatus] _currentStatus.status set to %d\n", static_cast<int>(_currentStatus.status));
            _currentStatus.message = localMessage;
            Serial.printf("OtaManager: [_setUpdateStatus] _currentStatus.message set to %s\n", _currentStatus.message.c_str());
            if (localProgress != -1) { // Only update progress if a valid value is provided
                _currentStatus.progress = localProgress;
                actualProgressForLogging = localProgress;
                Serial.printf("OtaManager: [_setUpdateStatus] _currentStatus.progress set to %d\n", _currentStatus.progress);
            } else {
                actualProgressForLogging = _currentStatus.progress; // Log existing progress
                Serial.printf("OtaManager: [_setUpdateStatus] _currentStatus.progress unchanged, remains %d\n", _currentStatus.progress);
            }
            xSemaphoreGive(_dataMutex);
            Serial.println("OtaManager: [_setUpdateStatus] dataMutex given.");
        } else {
            Serial.println("OtaManager: [_setUpdateStatus] CRITICAL_ERROR: Failed to take dataMutex even with portMAX_DELAY.");
            // This situation should ideally not happen with portMAX_DELAY.
            // Log with passed-in values, as status wasn't actually set.
            Serial.printf("CRITICAL_ERROR_DETAIL: OtaManager::_setUpdateStatus mutex fail. Requested state: [%d] %s (%d%%)\n",
                          static_cast<int>(localState), localMessage.c_str(), localProgress == -1 ? -1 : localProgress);
            return; // Skip logging below as status wasn't actually set
        }
    } else {
        Serial.println("OtaManager: [_setUpdateStatus] CRITICAL_ERROR: _dataMutex is NULL.");
        Serial.printf("CRITICAL_ERROR_DETAIL: OtaManager::_setUpdateStatus _dataMutex NULL. Requested state: [%d] %s (%d%%)\n",
                      static_cast<int>(localState), localMessage.c_str(), localProgress == -1 ? -1 : localProgress);
        return; // Skip logging below
    }

    // Perform logging after mutex has been released
    Serial.printf("OtaManager Status: [%d] %s (%d%%) (Logged from _setUpdateStatus after mutex release)\n",
                  static_cast<int>(localState), localMessage.c_str(), actualProgressForLogging);
    Serial.println("OtaManager: [_setUpdateStatus] Exiting.");
}

String OtaManager::_performHttpsRequest(const char* url, const char* rootCa) {
    String payload = "";
    HTTPClient http;

    Serial.printf("OtaManager: Performing HTTPS request to: %s\n", url);

    // Ensure WiFi is connected (basic check, real check might be more robust or external)
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("OtaManager: WiFi not connected for HTTPS request.");
        if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
        _lastCheckResult.error = "WiFi not connected.";
        if (_dataMutex) xSemaphoreGive(_dataMutex);
        _setUpdateStatus(UpdateStatus::State::ERROR_WIFI, "WiFi not connected");
        return "";
    }

    http.begin(url, rootCa);
    http.setConnectTimeout(10000); // 10 seconds
    http.setTimeout(20000); // 20 seconds for the whole request
    int httpCode = http.GET();

    if (httpCode > 0) {
        Serial.printf("OtaManager: HTTPS GET successful, code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            payload = http.getString();
        } else {
            Serial.printf("OtaManager: HTTPS GET failed, error: %s\n", http.errorToString(httpCode).c_str());
            if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
            _lastCheckResult.error = "HTTP error: " + String(httpCode);
            if (_dataMutex) xSemaphoreGive(_dataMutex);
        }
    } else {
        Serial.printf("OtaManager: HTTPS GET failed, error: %s\n", http.errorToString(httpCode).c_str());
         if (_dataMutex) xSemaphoreTake(_dataMutex, portMAX_DELAY);
        _lastCheckResult.error = "HTTP connection failed: " + http.errorToString(httpCode);
        if (_dataMutex) xSemaphoreGive(_dataMutex);
    }

    http.end();
    return payload;
}

UpdateInfo OtaManager::_parseGithubApiResponse(const String& jsonPayload) {
    UpdateInfo info;
    info.currentVersion = _currentVersion;

#if OTAMANAGER_ARDUINOJSON_ENABLE_PSRAM && BOARD_HAS_PSRAM
    Serial.println("OtaManager: Using PsramAllocator for JSON parsing.");
    // Capacity can be larger when using PSRAM, e.g., 10KB or 20KB depending on expected payload size
    // Let's try 10KB first. Max GitHub API response for releases is 30 items, but we only parse the first.
    // However, release notes can be long.
    BasicJsonDocument<PsramAllocator> doc(10240);
#else
    Serial.println("OtaManager: Using default allocator (internal RAM) for JSON parsing.");
    DynamicJsonDocument doc(1536); // Previous reduced size for internal RAM fallback
#endif

    DeserializationError error = deserializeJson(doc, jsonPayload);
    if (error) {
        Serial.print(F("OtaManager: deserializeJson() failed: "));
        Serial.println(error.c_str());
        info.error = "JSON parsing failed: " + String(error.c_str());
        return info;
    }

    if (!doc.is<JsonObject>()) {
        Serial.println(F("OtaManager: JSON is not an object. No release found?"));
        info.error = "No release information found in API response.";
        return info;
    }

    JsonObject latestRelease = doc.as<JsonObject>();
    const char* tagName = latestRelease["tag_name"];

    if (tagName) {
        info.availableVersion = String(tagName);
        // Simple version comparison (e.g., "v0.1.1" > "v0.1.0")
        // Assumes semantic versioning like vX.Y.Z or X.Y.Z
        String current = _currentVersion;
        String available = info.availableVersion;
        // Remove 'v' prefix if present for comparison
        if (current.startsWith("v")) current.remove(0, 1);
        if (available.startsWith("v")) available.remove(0, 1);

        if (available.compareTo(current) > 0) { // lexicographical comparison, good for SemVer if numbers are same width or major/minor/patch logic is used
            info.updateAvailable = true;
            Serial.printf("OtaManager: Update available. Current: %s, Available: %s\n", _currentVersion.c_str(), info.availableVersion.c_str());

            JsonArray assets = latestRelease["assets"].as<JsonArray>();
            for (JsonObject asset : assets) {
                String assetName = asset["name"].as<String>();
                String downloadUrl = asset["browser_download_url"].as<String>();

                if (assetName == _firmwareAssetName) {
                    info.downloadUrl = downloadUrl;
                    Serial.printf("OtaManager: MATCHED firmware asset '%s' with URL: %s\n", _firmwareAssetName.c_str(), downloadUrl.c_str());
                    break;
                }
            }
            if (info.downloadUrl.isEmpty()) {
                 Serial.printf("OtaManager: Firmware asset '%s' not found in latest release.\n", _firmwareAssetName.c_str());
                 info.error = "Firmware asset not found.";
                 info.updateAvailable = false; // Cannot update if no download URL
            }
            const char* body = latestRelease["body"];
            if (body) {
                info.releaseNotes = String(body);
            }
        } else {
            Serial.printf("OtaManager: Firmware is up to date. Current: %s, Latest: %s\n", _currentVersion.c_str(), info.availableVersion.c_str());
        }
    } else {
        Serial.println(F("OtaManager: 'tag_name' not found in release JSON."));
        info.error = "Tag name not found in release info.";
    }
    return info;
}

bool OtaManager::_ensureTimeSynced() {
    if (_timeSynced) return true;

    Serial.println("OtaManager: Attempting to sync NTP time...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov"); // UTC, no DST offset, NTP servers

    time_t now = time(nullptr);
    int retries = 0;
    while (now < 8 * 3600 * 2) { // Check if time is reasonably after epoch (1 Jan 1970)
        delay(500);
        now = time(nullptr);
        retries++;
        if (retries > 20) { // Approx 10 seconds timeout
            Serial.println("OtaManager: NTP time sync failed after retries.");
            return false;
        }
    }
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.printf("OtaManager: NTP time synced: %s", asctime(&timeinfo));
    _timeSynced = true;
    return true;
}

// Static task runners
void OtaManager::_checkUpdateTaskRunner(void* pvParameters) {
    Serial.println("DEBUG: _checkUpdateTaskRunner started.");
        Serial.println("!!!!!!!!!! UPDATE TASK RUNNER - VERSION XYZ - CHECKING IN !!!!!!!!!!");
    OtaManager* self = static_cast<OtaManager*>(pvParameters);
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL)); // Add current task to WDT

    // Perform NTP Sync first
    if (!self->_ensureTimeSynced()) {
        // NTP failed. Update status and terminate task.
        UpdateStatus::State ntpErrorState = UpdateStatus::State::ERROR_HTTP_CHECK; // Or a more specific NTP error state if you add one
        String ntpErrorMessage = "NTP time sync failed. Cannot check for updates.";

        if (self->_dataMutex) {
            if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
                self->_currentStatus.status = ntpErrorState;
                self->_currentStatus.message = ntpErrorMessage;
                self->_lastCheckResult.error = ntpErrorMessage;
                self->_checkTaskHandle = NULL; // Mark task as complete (failed)
                xSemaphoreGive(self->_dataMutex);
                Serial.printf("OtaManager Status Update (NTP Fail in _checkUpdateTaskRunner): [%d] %s\n",
                              static_cast<int>(ntpErrorState), ntpErrorMessage.c_str());
            } else {
                Serial.println("ERROR: _checkUpdateTaskRunner (NTP Fail) failed to take mutex!");
                self->_checkTaskHandle = NULL;
            }
        }
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return; // Essential to exit the task here
    }

    // NTP Sync successful, now update status to indicate actual checking is starting
    // This uses the standard _setUpdateStatus which handles its own mutex and logging.
    self->_setUpdateStatus(UpdateStatus::State::CHECKING_VERSION, "Checking for updates (post-NTP)...", 0);

    String apiUrl = "https://api.github.com/repos/" + self->_repoOwner + "/" + self->_repoName + "/releases/latest";
    String jsonPayload = self->_performHttpsRequest(apiUrl.c_str(), self->_githubApiRootCa);

    UpdateInfo localCheckResult;
    UpdateStatus::State newOtaState = UpdateStatus::State::IDLE;
    String newOtaMessage = "";
    int newOtaProgress = 0;

    if (!jsonPayload.isEmpty()) {
        localCheckResult = self->_parseGithubApiResponse(jsonPayload);
    } else {
        // _performHttpsRequest might have set an error in self->_lastCheckResult via _setUpdateStatus or direct write.
        // We should prioritize that error if available, or set a generic one.
        bool errorAlreadySet = false;
        if (self->_dataMutex) { // Check if mutex is valid before trying to take
            if (xSemaphoreTake(self->_dataMutex, (TickType_t)10) == pdTRUE) { // Brief wait
                if (!self->_lastCheckResult.error.isEmpty()) {
                    localCheckResult.error = self->_lastCheckResult.error;
                    errorAlreadySet = true;
                }
                // Also check current status if it reflects an HTTP error from _performHttpsRequest
                if (self->_currentStatus.status == UpdateStatus::State::ERROR_HTTP_CHECK || self->_currentStatus.status == UpdateStatus::State::ERROR_WIFI) {
                    newOtaState = self->_currentStatus.status;
                    newOtaMessage = self->_currentStatus.message;
                    errorAlreadySet = true; // Indicates we should use this state
                }
                xSemaphoreGive(self->_dataMutex);
            }
        }
        if (!errorAlreadySet) {
            localCheckResult.error = "Failed to fetch release info (empty payload).";
            newOtaState = UpdateStatus::State::ERROR_HTTP_CHECK;
            newOtaMessage = "Failed to fetch release info";
        }
    }

    // Logic to determine newOtaState and newOtaMessage based on localCheckResult
    // This logic was previously inside the mutex lock and calling _setUpdateStatus
    if (newOtaState != UpdateStatus::State::ERROR_HTTP_CHECK && newOtaState != UpdateStatus::State::ERROR_WIFI) { // Only proceed if no HTTP error already determined
        if (!localCheckResult.error.isEmpty() && !localCheckResult.updateAvailable) {
            newOtaState = UpdateStatus::State::ERROR_JSON;
            newOtaMessage = "Error processing release: " + localCheckResult.error;
        } else if (!localCheckResult.updateAvailable) {
            newOtaState = UpdateStatus::State::IDLE;
            newOtaMessage = "Firmware is up to date.";
        } else { // Update is available
            newOtaState = UpdateStatus::State::IDLE; // Or CHECKING_VERSION if you want to signify data is ready before user acts
                                                      // Setting to IDLE implies check is done, UI can show "Update Available"
            newOtaMessage = "Update available: " + localCheckResult.availableVersion;
            newOtaProgress = 100; // Check complete
        }
    }

    // Now, update shared state under a single mutex lock
    if (self->_dataMutex) {
        if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
            self->_lastCheckResult = localCheckResult; // Store the latest check result
            self->_lastCheckResult.currentVersion = self->_currentVersion; // Ensure current version is part of the result

            self->_currentStatus.status = newOtaState;
            self->_currentStatus.message = newOtaMessage;
            if (newOtaProgress != -1) { // Keep this progress update logic consistent
                self->_currentStatus.progress = newOtaProgress;
            }

            self->_checkTaskHandle = NULL; // Mark task as complete
            xSemaphoreGive(self->_dataMutex);

            // Log *after* releasing the mutex
            Serial.printf("OtaManager Status Update (from _checkUpdateTaskRunner): [%d] %s (%d%%)\n",
                          static_cast<int>(newOtaState), newOtaMessage.c_str(), newOtaProgress);
        } else {
            Serial.println("ERROR: _checkUpdateTaskRunner failed to take mutex to update status!");
            // Handle critical error, perhaps try to clean up task handle if possible, though state is inconsistent
            self->_checkTaskHandle = NULL; // Still try to mark as done to prevent re-triggering issues
        }
    }

    esp_task_wdt_delete(NULL); // Remove current task from WDT
    vTaskDelete(NULL);
}

void OtaManager::_updateTaskRunner(void* pvParameters) {
    Serial.println("OtaManager: [_updateTaskRunner] *** TASK STARTED SUCCESSFULLY ***"); // New very first line

    UpdateTaskParams* params = static_cast<UpdateTaskParams*>(pvParameters);
    if (!params) {
        Serial.println("OtaManager: [_updateTaskRunner] CRITICAL ERROR: pvParameters is NULL. Task cannot proceed.");
        // Cannot call self->_setUpdateStatus as self is unknown. Log and delete task.
        vTaskDelete(NULL);
        return;
    }
    Serial.printf("OtaManager: [_updateTaskRunner] pvParameters (params struct) address: %p\n", (void*)params);

    OtaManager* self = params->otaManagerInstance;
    if (!self) {
        Serial.println("OtaManager: [_updateTaskRunner] CRITICAL ERROR: params->otaManagerInstance is NULL. Task cannot proceed.");
        // Cannot call self->_setUpdateStatus. Log, free what we can, and delete task.
        if(params->downloadUrl) free(params->downloadUrl);
        free(params);
        vTaskDelete(NULL);
        return;
    }
    Serial.printf("OtaManager: [_updateTaskRunner] self (OtaManager instance) address: %p\n", (void*)self);

    char* downloadUrlCStr_task = params->downloadUrl; // Keep a C-string pointer before creating String
    if (!downloadUrlCStr_task) {
        Serial.println("OtaManager: [_updateTaskRunner] CRITICAL ERROR: params->downloadUrl is NULL. Task cannot proceed.");
        self->_setUpdateStatus(UpdateStatus::State::ERROR_INTERNAL, "Task started with NULL download URL");
        // self is valid here, so we can use _setUpdateStatus if needed, though probably implies bigger issues.
        free(params); // Free the params struct itself
        vTaskDelete(NULL);
        return;
    }
    Serial.printf("OtaManager: [_updateTaskRunner] downloadUrlCStr_task (from params) address: %p, Content: %s\n", (void*)downloadUrlCStr_task, downloadUrlCStr_task);

    String downloadUrl = String(downloadUrlCStr_task);
    Serial.printf("OtaManager: [_updateTaskRunner] downloadUrl (String object created): %s\n", downloadUrl.c_str());

    // Free the passed parameters now that we have copied/used them
    // Important: downloadUrlCStr_task (which is params->downloadUrl) must be freed.
    // The 'params' struct itself also needs to be freed.
    Serial.printf("OtaManager: [_updateTaskRunner] Freeing params->downloadUrl (%p)\n", (void*)params->downloadUrl);
    free(params->downloadUrl);
    params->downloadUrl = nullptr; // Good practice after free
    Serial.printf("OtaManager: [_updateTaskRunner] Freeing params struct (%p)\n", (void*)params);
    free(params);
    params = nullptr; // Good practice

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));    // Add current task to WDT

    HTTPClient http;
    Serial.printf("OtaManager: [_updateTaskRunner] Starting firmware update from URL: %s\n", downloadUrl.c_str());
    self->_setUpdateStatus(UpdateStatus::State::DOWNLOADING, "Downloading firmware...", 0);

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("OtaManager: [_updateTaskRunner] WiFi not connected for firmware download.");
        self->_setUpdateStatus(UpdateStatus::State::ERROR_WIFI, "WiFi not connected for download");
        if (self->_dataMutex) { // Check if mutex is valid before taking
             if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
                self->_updateTaskHandle = NULL;
                xSemaphoreGive(self->_dataMutex);
            } else {
                Serial.println("ERROR: _updateTaskRunner (WiFi Fail) failed to take mutex!");
                self->_updateTaskHandle = NULL; // Attempt to clear handle anyway
            }
        } else {
             Serial.println("ERROR: _updateTaskRunner (WiFi Fail) _dataMutex is NULL!");
             self->_updateTaskHandle = NULL; // Attempt to clear handle anyway
        }
        esp_task_wdt_delete(NULL);
        vTaskDelete(NULL);
        return;
    }

    // Use the CA certificate for the HTTPS connection
    Serial.println("OtaManager: [_updateTaskRunner] Calling http.begin() with URL and Root CA.");
    http.begin(downloadUrl, self->_githubApiRootCa);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Enable following redirects
    http.setConnectTimeout(10000); // 10s
    http.setTimeout(180000);      // 3 minutes for download

    Serial.println("OtaManager: [_updateTaskRunner] About to call http.GET().");
    int httpCode = http.GET();
    Serial.printf("OtaManager: [_updateTaskRunner] http.GET() returned code: %d\n", httpCode);

    if (httpCode > 0) { // Moved the check for httpCode > 0 to encompass redirect handling
        Serial.printf("OtaManager: [_updateTaskRunner] HTTP GET successful, code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK) {
            int totalSize = http.getSize();
            Serial.printf("OtaManager: [_updateTaskRunner] HTTP_CODE_OK. Total size: %d\n", totalSize);
            if (totalSize <= 0) {
                Serial.println("OtaManager: [_updateTaskRunner] Content length header missing or zero.");
                self->_setUpdateStatus(UpdateStatus::State::ERROR_HTTP_DOWNLOAD, "Invalid content length from server.");
                if (self->_dataMutex) { // Check if mutex is valid before taking
                    if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
                        self->_updateTaskHandle = NULL;
                        xSemaphoreGive(self->_dataMutex);
                    } else {
                        Serial.println("ERROR: _updateTaskRunner (Content Length) failed to take mutex!");
                        self->_updateTaskHandle = NULL;
                    }
                } else {
                    Serial.println("ERROR: _updateTaskRunner (Content Length) _dataMutex is NULL!");
                    self->_updateTaskHandle = NULL;
                }
            }

            // Initialize OTA Update process
            Serial.printf("OtaManager: [_updateTaskRunner] Pre-Update.begin(): Update.isRunning(): %s, Update.canRollBack(): %s\\n",
                          Update.isRunning() ? "true" : "false",
                          Update.canRollBack() ? "true" : "false");

            // Log the currently running partition
            const esp_partition_t *running_partition = esp_ota_get_running_partition();
            if (running_partition != NULL) {
                Serial.printf("OtaManager: [_updateTaskRunner] Currently running from partition: %s (Type: %d, SubType: %d)\\n",
                              running_partition->label, running_partition->type, running_partition->subtype);
            } else {
                Serial.println("OtaManager: [_updateTaskRunner] Could not determine running partition (esp_ota_get_running_partition() returned NULL).");
            }

            Serial.printf("OtaManager: [_updateTaskRunner] Attempting Update.begin(totalSize: %d, command: U_FLASH, label: \\\"ota_1\\\")\\n", totalSize);
            // Explicitly target the "ota_1" partition label. U_FLASH (0) is for application partitions.
            bool update_begin_success = Update.begin(totalSize, U_FLASH);
            Serial.printf("OtaManager: [_updateTaskRunner] Update.begin(\\\"ota_1\\\") result: %s\\n", update_begin_success ? "SUCCESS" : "FAILED");

            if (!update_begin_success) {
                uint8_t update_error_code = Update.getError();
                const char* update_error_cstr = Update.errorString();
                Serial.printf("OtaManager: [_updateTaskRunner] Update.begin(\\\"ota_1\\\") FAILED. Error: %s (%d)\n", update_error_cstr, update_error_code);
                self->_setUpdateStatus(UpdateStatus::State::ERROR_NO_SPACE, "Update.begin(\\\"ota_1\\\") failed: " + String(update_error_cstr));

                // Standard error handling: clear task handle, release mutex, end HTTP, delete WDT & task.
                if (self->_dataMutex) {
                    if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
                        self->_updateTaskHandle = NULL;
                        xSemaphoreGive(self->_dataMutex);
                    } else {
                         Serial.println("ERROR: _updateTaskRunner (Update.begin fail) failed to take mutex!");
                         self->_updateTaskHandle = NULL;
                    }
                } else {
                    Serial.println("ERROR: _updateTaskRunner (Update.begin fail) _dataMutex is NULL!");
                    self->_updateTaskHandle = NULL;
                }
                http.end();
                esp_task_wdt_delete(NULL);
                vTaskDelete(NULL);
                return; // Essential to exit the task here
            }

            // If Update.begin() was successful:
            Serial.printf("OtaManager: [_updateTaskRunner] Post-Update.begin(\\\"ota_1\\\") SUCCESS: Update.isRunning(): %s, Update.canRollBack(): %s\\n",
                          Update.isRunning() ? "true" : "false",
                          Update.canRollBack() ? "true" : "false");

            self->_setUpdateStatus(UpdateStatus::State::WRITING, "Writing firmware...", 0);

            WiFiClient* stream = http.getStreamPtr();
            Serial.println("OtaManager: [_updateTaskRunner] Got stream pointer.");
            size_t written = 0;
            uint8_t buff[1460] = {0}; // TCP MSS typical size
            unsigned long lastProgressUpdate = millis();

            Serial.println("OtaManager: [_updateTaskRunner] Entering firmware write loop.");
            while (http.connected() && (written < totalSize)) {
                esp_task_wdt_reset(); // Reset WDT periodically during download/write
                Serial.printf("OtaManager: [_updateTaskRunner] Loop Start: http.connected(): %d, stream->available(): %d, written: %d / %d\n", http.connected(), stream->available(), written, totalSize);
                size_t len = stream->available();
                if (len > 0) {
                    int c = stream->readBytes(buff, ((len > sizeof(buff)) ? sizeof(buff) : len));
                    Serial.printf("OtaManager: [_updateTaskRunner] stream->readBytes read %d bytes.\n", c);
                    if (c > 0) { // Only call Update.write if bytes were actually read
                        size_t bytes_written_by_update = Update.write(buff, c);
                        if (bytes_written_by_update != c) {
                            Serial.printf("OtaManager: [_updateTaskRunner] Update.write() failed. Expected to write %d, actually wrote %d.\n", c, bytes_written_by_update);
                            // Capture error immediately after the failed write
                            uint8_t update_error_code = Update.getError(); // Get the error code
                            const char* update_error_cstr = Update.errorString(); // Get the error string (no argument)
                            Serial.printf("OtaManager: [_updateTaskRunner] Update Error (captured immediately): %s (%d)\n", update_error_cstr, update_error_code);

                            self->_setUpdateStatus(UpdateStatus::State::ERROR_UPDATE_WRITE, "Firmware write error: " + String(update_error_cstr));
                            if (self->_dataMutex) {
                                if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
                                    self->_updateTaskHandle = NULL;
                                    xSemaphoreGive(self->_dataMutex);
                                } else {
                                    Serial.println("ERROR: _updateTaskRunner (Write Error) failed to take mutex!");
                                    self->_updateTaskHandle = NULL;
                                }
                            } else {
                                Serial.println("ERROR: _updateTaskRunner (Write Error) _dataMutex is NULL!");
                                self->_updateTaskHandle = NULL;
                            }
                            Update.abort();
                            Serial.println("OtaManager: [_updateTaskRunner] Called Update.abort() after write error.");
                            http.end();
                            Serial.println("OtaManager: [_updateTaskRunner] Called http.end() after write error.");
                            esp_task_wdt_delete(NULL);
                            vTaskDelete(NULL);
                            Serial.println("OtaManager: [_updateTaskRunner] Task deleted after write error.");
                            return;
                        }
                        written += bytes_written_by_update; // Use actual bytes written
                    } else if (c < 0) { // readBytes can return -1 on error/timeout for some stream types
                        Serial.println("OtaManager: [_updateTaskRunner] stream->readBytes returned a negative value, indicating an error.");
                        // Treat as a download error
                        self->_setUpdateStatus(UpdateStatus::State::ERROR_HTTP_DOWNLOAD, "Stream read error during download");
                         if (self->_dataMutex) {
                            if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
                                self->_updateTaskHandle = NULL;
                                xSemaphoreGive(self->_dataMutex);
                            } else {
                                Serial.println("ERROR: _updateTaskRunner (Stream Read Error) failed to take mutex!");
                                self->_updateTaskHandle = NULL;
                            }
                        } else {
                            Serial.println("ERROR: _updateTaskRunner (Stream Read Error) _dataMutex is NULL!");
                            self->_updateTaskHandle = NULL;
                        }
                        Update.abort();
                        http.end();
                        esp_task_wdt_delete(NULL);
                        vTaskDelete(NULL);
                        return;
                    } // If c == 0, it means no data was available right now, loop will continue based on http.connected()

                    int progress = (int)(((float)written / totalSize) * 100);
                    // Update progress not too frequently to avoid flooding serial/logs
                    if (progress > self->_currentStatus.progress && (millis() - lastProgressUpdate > 1000 || progress == 100)) {
                        self->_setUpdateStatus(UpdateStatus::State::WRITING, "Writing firmware...", progress);
                        lastProgressUpdate = millis();
                    }
                }
                delay(1); // Small delay to allow other tasks and prevent tight loop if stream is slow
            }

            if (written != totalSize) {
                Serial.printf("OtaManager: [_updateTaskRunner] Update failed. Bytes written: %d / %d\n", written, totalSize);
                self->_setUpdateStatus(UpdateStatus::State::ERROR_HTTP_DOWNLOAD, "Download incomplete.");
                Update.abort();
            } else if (!Update.end(true)) { // true to set the boot partition
                Serial.printf("OtaManager: [_updateTaskRunner] Error occurred during Update.end(): %u\n", Update.getError());
                self->_setUpdateStatus(UpdateStatus::State::ERROR_UPDATE_END, "Finalizing update error: " + String(Update.errorString()));
                Update.abort(); // ensure abort is called
            } else {
                self->_setUpdateStatus(UpdateStatus::State::SUCCESS, "Update successful! Rebooting...", 100);
                Serial.println("OtaManager: [_updateTaskRunner] Update successful. Rebooting...");
                delay(1000); // Give a moment for serial message to get out
                ESP.restart();
            }
        } else {
            Serial.printf("OtaManager: [_updateTaskRunner] HTTP GET completed with code %d, but not HTTP_CODE_OK. Error: %s\n", httpCode, http.errorToString(httpCode).c_str());
            self->_setUpdateStatus(UpdateStatus::State::ERROR_HTTP_DOWNLOAD, "Firmware download HTTP error: " + String(httpCode) + " " + http.errorToString(httpCode));
        }
    } else {
        Serial.printf("OtaManager: [_updateTaskRunner] HTTP GET failed, error: %s (httpCode: %d)\n", http.errorToString(httpCode).c_str(), httpCode);
        self->_setUpdateStatus(UpdateStatus::State::ERROR_HTTP_DOWNLOAD, "HTTP connection failed: " + http.errorToString(httpCode));
    }
    Serial.println("OtaManager: [_updateTaskRunner] Calling http.end().");
    http.end();

    // If we reach here and haven't rebooted, it means an error occurred after starting or during HTTP connection.
    // Ensure task handle is cleared if not already done by an error path.
    // Also set status to IDLE if it was an in-progress state that failed silently.
    if (self->_dataMutex) { // Check if mutex is valid before taking
        if (xSemaphoreTake(self->_dataMutex, portMAX_DELAY) == pdTRUE) {
            if (self->_currentStatus.status != UpdateStatus::State::SUCCESS) {
                // If it was in progress but failed and didn't set a specific error state above
                if(self->_currentStatus.status == UpdateStatus::State::DOWNLOADING || self->_currentStatus.status == UpdateStatus::State::WRITING ||
                   self->_currentStatus.status == UpdateStatus::State::ERROR_HTTP_DOWNLOAD || // if previously set to this by this task
                   self->_currentStatus.status == UpdateStatus::State::ERROR_UPDATE_WRITE || // if previously set to this by this task
                   self->_currentStatus.status == UpdateStatus::State::ERROR_NO_SPACE) { // if previously set to this
                     self->_setUpdateStatus(UpdateStatus::State::IDLE, "Update failed or ended prematurely");
                }
            }
            self->_updateTaskHandle = NULL;
            xSemaphoreGive(self->_dataMutex);
        } else {
             Serial.println("ERROR: _updateTaskRunner (End Task) failed to take mutex!");
             self->_updateTaskHandle = NULL; // Attempt to clear handle anyway
        }
    } else {
        Serial.println("ERROR: _updateTaskRunner (End Task) _dataMutex is NULL!");
        self->_updateTaskHandle = NULL; // Attempt to clear handle anyway
    }

    esp_task_wdt_delete(NULL); // Remove current task from WDT
    vTaskDelete(NULL);
}
