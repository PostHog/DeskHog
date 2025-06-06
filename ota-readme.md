# DeskHog OTA Updates via GitHub Releases

This document outlines the plan for implementing Over-the-Air (OTA) firmware updates for the DeskHog project using GitHub Releases as the firmware source and integrating the process into the existing web configuration portal.

## Core Requirements for ESP32 OTA

Implementing OTA updates on the ESP32 with the Arduino framework requires several key components:

1.  **Partition Scheme:** The ESP32's flash memory needs an OTA-compatible partition table defined in `partitions.csv`. This typically includes at least two application partitions (e.g., `factory`, `ota_0`) and an `otadata` partition to manage which partition boots.
2.  **Network Connectivity:** The device must connect to Wi-Fi to check for and download updates.
3.  **Update Source:** A server to host the firmware binary (`.bin`). We will use GitHub Releases for this.
4.  **Arduino `Update` Library:** The `Update.h` library provided by the ESP32 Arduino core is used to write the downloaded firmware to the inactive partition and manage the boot process.
5.  **Update Logic:** Application code is needed to check for updates, download the binary, feed it to the `Update` library, handle errors, provide feedback, and trigger a reboot.
6.  **Firmware Binary:** The compiled `.bin` file generated by PlatformIO.
7.  **Security (Recommended):** Using HTTPS for downloads prevents tampering. Certificate management is required. Firmware signing is another potential enhancement.
8.  **Rollback:** The ESP32 bootloader provides basic rollback if a new firmware fails to boot successfully multiple times.

## Update Source: GitHub Releases Method

We will use the GitHub Releases feature of the project repository to host firmware updates:

1.  **Release Assets:** New firmware builds (`.bin` files) will be uploaded as assets to tagged releases (e.g., `v1.1.0`) on GitHub.
2.  **Checking for Updates:**
    *   The ESP32 will make an HTTPS GET request to the GitHub API endpoint: `https://api.github.com/repos/PostHog/DeskHog/releases/latest`.
    *   Requires an HTTPS client on the ESP32 and the necessary root CA certificate for `api.github.com`.
3.  **Parsing Response:**
    *   The JSON response from the API will be parsed (using `ArduinoJson`).
    *   Extract `tag_name` (version) and the `browser_download_url` for the `.bin` asset.
4.  **Version Comparison:** Compare the `tag_name` from GitHub with the currently running firmware version.
5.  **Downloading Binary:** If a newer version is found, make an HTTPS GET request to the `browser_download_url`.
6.  **Applying Update:** Stream the downloaded binary data to the Arduino `Update` library (`Update.writeStream()`). Call `Update.end()` and `ESP.restart()` on success.

## Web UI Integration (`portal.html` / `portal.js`)

The existing captive portal UI will be extended to manage OTA updates:

1.  **`portal.html` Changes:**
    *   Add a new "Firmware Update" section.
    *   Display "Current Version" (`<span id="current-version">`).
    *   Display "Available Version" (`<span id="available-version">`).
    *   Add a "Check for Updates" button (`<button id="check-update-btn">`).
    *   Add an initially hidden "Install Update" button (`<button id="install-update-btn">`).
    *   Add an optional area to display release notes (`<pre id="release-notes">`).
    *   Add a status display area for ongoing updates, including a progress bar (`<div id="update-status-container">`, `<div id="update-progress-bar">`).
    *   Add an area for displaying update errors (`<p id="update-error-message">`).

2.  **`portal.js` Changes:**
    *   **`checkFirmwareUpdate()`:** Called on page load and by the "Check" button. Fetches `/check-update`, updates the UI with current/available versions, and shows the "Install" button if an update is available.
    *   **`startFirmwareUpdate()`:** Called by the "Install" button. Confirms with the user, then POSTs to `/start-update`. On success, it initiates status polling.
    *   **`pollUpdateStatus()`:** (Optional but recommended) Called periodically after an update starts. Fetches `/update-status`, updates the progress bar and status message, and stops polling on success or error. Handles completion or errors.
    *   Modify `DOMContentLoaded` listener to call `checkFirmwareUpdate()` on initial page load.

## Backend Implementation (`CaptivePortal` / `OtaManager`)

The backend logic will reside primarily in a new class, coordinated by the existing `CaptivePortal`.

1.  **`OtaManager` (New Class):**
    *   **Responsibilities:**
        *   Encapsulate all core OTA logic.
        *   Store GitHub repository details and current firmware version.
        *   Manage HTTPS connections (including certificates) to `api.github.com`.
        *   Perform API calls (`/releases/latest`).
        *   Parse JSON responses.
        *   Download the `.bin` file via HTTPS using `WiFiClientSecure`.
        *   Interact with the Arduino `Update` library (`Update.begin`, `Update.writeStream`, `Update.end`).
        *   Track update state (idle, checking, downloading, writing, success, error) and progress.
        *   Trigger `ESP.restart()`.
    *   **Interface (Conceptual):**
        *   `struct UpdateInfo { bool updateAvailable; String currentVersion; String availableVersion; String downloadUrl; String releaseNotes; };`
        *   `UpdateInfo checkForUpdate();`
        *   `bool beginUpdate(String downloadUrl);`
        *   `struct UpdateStatus { String status; int progress; String message; };`
        *   `UpdateStatus getStatus();` // For polling

2.  **`CaptivePortal` Modifications:**
    *   **Dependencies:** Add a reference to an `OtaManager` instance (passed via constructor).
    *   **New Handler Methods:**
        *   `handleCheckUpdate()`: Calls `otaManager.checkForUpdate()`, formats the `UpdateInfo` struct into a JSON response for the UI.
        *   `handleStartUpdate()`: Retrieves the download URL (likely cached from the last check or passed from the UI), calls `otaManager.beginUpdate()`, responds with success/failure JSON.
        *   `handleUpdateStatus()`: Calls `otaManager.getStatus()`, formats the `UpdateStatus` struct into a JSON response.
    *   **`CaptivePortal::begin()`:** Register routes `/check-update` (GET), `/start-update` (POST), and `/update-status` (GET) to map to the new handler methods.

## Implementation Progress

*   **Partition Scheme (`partitions.csv`):** Updated to support OTA with two application partitions (~1.84MB each) and a smaller SPIFFS partition (256KB).
    *   _Affected file:_ `partitions.csv`
*   **`OtaManager` Class (`src/OtaManager.h`, `src/OtaManager.cpp`):
    *   Skeleton created with basic structure, structs (`UpdateInfo`, `UpdateStatus`), and method signatures.
    *   `_githubApiRootCa` updated with DigiCert Global Root G2 certificate.
    *   `checkForUpdate()` method implemented as **non-blocking** using a FreeRTOS task (`_checkUpdateTaskRunner`):
        *   Handles HTTPS GET request to GitHub API (`_performHttpsRequest`).
        *   Parses JSON response (`_parseGithubApiResponse`).
        *   Compares versions and identifies firmware asset URL.
        *   Updates internal status and makes results available via `getStatus()` and `getLastCheckResult()`.
    *   _Affected files:_ `src/OtaManager.h`, `src/OtaManager.cpp`

## Next Steps (OtaManager)

1.  Implement `OtaManager::beginUpdate()` using a FreeRTOS task to handle firmware download (HTTPS) and flashing with `Update.writeStream()`.
2.  Implement robust error handling and status reporting for the `beginUpdate` process.
3.  Implement a reliable mechanism for defining/retrieving `CURRENT_FIRMWARE_VERSION`.

This structure maintains separation of concerns, keeping web request handling (`CaptivePortal`) distinct from the OTA update mechanism (`OtaManager`). 