#pragma once

#include <Arduino.h>
#include <ArduinoJson.h> // For potential JSON parsing within the manager
#include <HTTPClient.h>     // Required for making HTTP requests
#include "esp_heap_caps.h"   // For heap_caps_malloc_extmem_enable

// FreeRTOS for task management
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "time.h" // Added for NTP

// Add these includes for FreeRTOS mutex
#include <freertos/semphr.h>

// Forward declarations if needed, e.g., if using WiFiClientSecure pointer
// class WiFiClientSecure;

// Structure to hold information about an available update
struct UpdateInfo {
    bool updateAvailable = false;
    String currentVersion = "";
    String availableVersion = "";
    String downloadUrl = "";
    String releaseNotes = ""; // Optional
    String error = ""; // Store error messages
};

// Structure to hold the status of an ongoing update
struct UpdateStatus {
    enum class State {
        IDLE,
        CHECKING_VERSION,
        DOWNLOADING,
        WRITING,
        SUCCESS,
        ERROR_WIFI,
        ERROR_HTTP_CHECK,
        ERROR_HTTP_DOWNLOAD,
        ERROR_JSON,
        ERROR_UPDATE_BEGIN,
        ERROR_UPDATE_WRITE,
        ERROR_UPDATE_END,
        ERROR_NO_ASSET,
        ERROR_NO_SPACE,
        MUTEX_BUSY,      // New state: OTA manager mutex was contended
        ERROR_INTERNAL   // New state: Internal error (e.g. mutex not initialized)
    };

    State status = State::IDLE;
    int progress = 0; // Percentage (0-100)
    String message = "Idle";
};

class OtaManager {
public:
    /**
     * @brief Constructor
     * @param currentVersion The firmware version currently running.
     * @param repoOwner GitHub repository owner (e.g., "PostHog").
     * @param repoName GitHub repository name (e.g., "DeskHog").
     */
    OtaManager(const String& currentVersion, const String& repoOwner, const String& repoName);

    /**
     * @brief Initiates a check for firmware updates in a non-blocking manner.
     * Spawns a FreeRTOS task to perform the actual check.
     * @return true if the check task was successfully started, false otherwise (e.g., already checking, WiFi not connected).
     */
    bool checkForUpdate();

    /**
     * @brief Begin the firmware update process using the provided download URL.
     * This function initiates the download and flashing process.
     * It should ideally run asynchronously (e.g., in a task) and update status internally.
     * @param downloadUrl The direct HTTPS URL to the firmware .bin file.
     * @return true if the update process was successfully initiated, false otherwise (e.g., update already in progress, invalid URL).
     */
    bool beginUpdate(const String& downloadUrl);

    /**
     * @brief Get the current status of the OTA process.
     * Non-blocking, returns the latest status tracked internally.
     * @return UpdateStatus struct.
     */
    UpdateStatus getStatus();

    UpdateInfo getLastCheckResult(); // Added to retrieve the result of the last check

    /**
     * @brief Ensures that the system time is synchronized with an NTP server.
     * This is a prerequisite for operations requiring accurate time, like checking OTA updates.
     * @return true if time is synced or was successfully synced, false on failure.
     */
    bool syncTimeIfNeeded();

    /**
     * @brief Checks if the system time has been successfully synchronized with an NTP server.
     * @return true if time is synced, false otherwise.
     */
    bool isTimeSynced();

    /**
      * @brief Process loop for the OtaManager.
      * Should be called periodically to handle asynchronous operations like download/write.
      * If using tasks, this might not be strictly necessary, depends on implementation.
      */
    void process(); // Optional, depending on async approach

private:
    String _currentVersion;
    String _repoOwner;
    String _repoName;
    String _firmwareAssetName = "firmware.bin"; // Default asset name
    const char* _githubApiRootCa = \
"-----BEGIN CERTIFICATE-----\n" \
"MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n" \
"MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n" \
"eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n" \
"JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n" \
"MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n" \
"Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n" \
"VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n" \
"aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n" \
"I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n" \
"o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n" \
"A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n" \
"VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n" \
"zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n" \
"RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n" \
"-----END CERTIFICATE-----\n" \
"-----BEGIN CERTIFICATE-----\n" \
"MIIF3jCCA8agAwIBAgIQAf1tMPyjylGoG7xkDjUDLTANBgkqhkiG9w0BAQwFADCB\n" \
"iDELMAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0pl\n" \
"cnNleSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNV\n" \
"BAMTJVVTRVJUcnVzdCBSU0EgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAw\n" \
"MjAxMDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNV\n" \
"BAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVU\n" \
"aGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBSU0EgQ2Vy\n" \
"dGlmaWNhdGlvbiBBdXRob3JpdHkwggIiMA0GCSqGSIb3DQEBAQUAA4ICDwAwggIK\n" \
"AoICAQCAEmUXNg7D2wiz0KxXDXbtzSfTTK1Qg2HiqiBNCS1kCdzOiZ/MPans9s/B\n" \
"3PHTsdZ7NygRK0faOca8Ohm0X6a9fZ2jY0K2dvKpOyuR+OJv0OwWIJAJPuLodMkY\n" \
"tJHUYmTbf6MG8YgYapAiPLz+E/CHFHv25B+O1ORRxhFnRghRy4YUVD+8M/5+bJz/\n" \
"Fp0YvVGONaanZshyZ9shZrHUm3gDwFA66Mzw3LyeTP6vBZY1H1dat//O+T23LLb2\n" \
"VN3I5xI6Ta5MirdcmrS3ID3KfyI0rn47aGYBROcBTkZTmzNg95S+UzeQc0PzMsNT\n" \
"79uq/nROacdrjGCT3sTHDN/hMq7MkztReJVni+49Vv4M0GkPGw/zJSZrM233bkf6\n" \
"c0Plfg6lZrEpfDKEY1WJxA3Bk1QwGROs0303p+tdOmw1XNtB1xLaqUkL39iAigmT\n" \
"Yo61Zs8liM2EuLE/pDkP2QKe6xJMlXzzawWpXhaDzLhn4ugTncxbgtNMs+1b/97l\n" \
"c6wjOy0AvzVVdAlJ2ElYGn+SNuZRkg7zJn0cTRe8yexDJtC/QV9AqURE9JnnV4ee\n" \
"UB9XVKg+/XRjL7FQZQnmWEIuQxpMtPAlR1n6BB6T1CZGSlCBst6+eLf8ZxXhyVeE\n" \
"Hg9j1uliutZfVS7qXMYoCAQlObgOK6nyTJccBz8NUvXt7y+CDwIDAQABo0IwQDAd\n" \
"BgNVHQ4EFgQUU3m/WqorSs9UgOHYm8Cd8rIDZsswDgYDVR0PAQH/BAQDAgEGMA8G\n" \
"A1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQEMBQADggIBAFzUfA3P9wF9QZllDHPF\n" \
"Up/L+M+ZBn8b2kMVn54CVVeWFPFSPCeHlCjtHzoBN6J2/FNQwISbxmtOuowhT6KO\n" \
"VWKR82kV2LyI48SqC/3vqOlLVSoGIG1VeCkZ7l8wXEskEVX/JJpuXior7gtNn3/3\n" \
"ATiUFJVDBwn7YKnuHKsSjKCaXqeYalltiz8I+8jRRa8YFWSQEg9zKC7F4iRO/Fjs\n" \
"8PRF/iKz6y+O0tlFYQXBl2+odnKPi4w2r78NBc5xjeambx9spnFixdjQg3IM8WcR\n" \
"iQycE0xyNN+81XHfqnHd4blsjDwSXWXavVcStkNr/+XeTWYRUc+ZruwXtuhxkYze\n" \
"Sf7dNXGiFSeUHM9h4ya7b6NnJSFd5t0dCy5oGzuCr+yDZ4XUmFF0sbmZgIn/f3gZ\n" \
"XHlKYC6SQK5MNyosycdiyA5d9zZbyuAlJQG03RoHnHcAP9Dc1ew91Pq7P8yF1m9/\n" \
"qS3fuQL39ZeatTXaw2ewh0qpKJ4jjv9cJ2vhsE/zB+4ALtRZh8tSQZXq9EfX7mRB\n" \
"VXyNWQKV3WKdwrnuWih0hKWbt5DHDAff9Yk2dDLWKMGwsAvgnEzDHNb842m1R0aB\n" \
"L6KCq9NjRHDEjf8tM7qtj3u1cIiuPhnPQCjY/MiQu12ZIvVS5ljFH4gxQ+6IHdfG\n" \
"jjxDah2nGN59PRbxYvnKkKj9\n" \
"-----END CERTIFICATE-----\n";

    UpdateStatus _currentStatus;
    UpdateInfo _lastCheckResult;
    TaskHandle_t _checkTaskHandle;
    TaskHandle_t _updateTaskHandle;
    bool _timeSynced = false;

    SemaphoreHandle_t _dataMutex; // Mutex for _currentStatus and _lastCheckResult

    // Static task runners
    static void _checkUpdateTaskRunner(void* pvParameters);
    static void _updateTaskRunner(void* pvParameters);

    // Private helper method prototypes
    void _setUpdateStatus(UpdateStatus::State state, const String& message, int progress = -1);
    String _performHttpsRequest(const char* url, const char* rootCa);
    UpdateInfo _parseGithubApiResponse(const String& jsonPayload);
    bool _ensureTimeSynced(); // Added for NTP

    // void _performUpdate(String url); // Function to run in a task
}; 