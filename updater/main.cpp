// The updater: a second, small firmware in its own slot (see partitions.csv).
//
// It can do one thing: join the saved WiFi under the device's name, take a
// firmware file over HTTP and write it over the main firmware. The main
// firmware restarts into it to be updated; a device whose main firmware is
// missing or half-written ends up here by itself, because the bootloader
// starts this slot whenever no other one is selected. So an update that fails
// can always be tried again without a cable.
//
// Nothing here comes from the main firmware except the names of its settings
// in NVS. Keep it small: it has to fit in its 1 MB slot.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include <esp_ota_ops.h>
#include <esp_image_format.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>

#ifndef UPDATER_VERSION
#define UPDATER_VERSION "1"
#endif

namespace {

const uint32_t kGiveUpMs = 5UL * 60UL * 1000UL;  ///< Without an upload, go back to the main firmware after this long

WebServer server(80);
Adafruit_ST7789 tft(&SPI, TFT_CS, TFT_DC, TFT_RST);

const esp_partition_t* target = nullptr;  ///< The main firmware's slot
String deviceName = "deskhog";
bool mainIntact = false;  ///< The main firmware can be started as it is
bool writing = false;
bool failed = false;
bool done = false;
String problem;
esp_ota_handle_t handle = 0;
mbedtls_sha256_context sha;
size_t written = 0;
size_t expected = 0;
int shownPercent = -1;
uint32_t lastActivityMs = 0;
uint32_t restartAtMs = 0;

// ---------------------------------------------------------------- screen

void show(const char* headline, const String& detail, uint16_t color = ST77XX_WHITE) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextWrap(true);
    tft.setTextColor(0x8410);  // gray
    tft.setTextSize(1);
    tft.setCursor(8, 8);
    tft.print("DeskHog updater");
    tft.setTextColor(color);
    tft.setTextSize(2);
    tft.setCursor(8, 34);
    tft.print(headline);
    tft.setTextColor(0xC618);  // light gray
    tft.setTextSize(1);
    tft.setCursor(8, 70);
    tft.print(detail);
}

void showProgress() {
    if (!expected) return;
    const int percent = (int)((uint64_t)written * 100 / expected);
    if (percent == shownPercent) return;
    if (shownPercent < 0) show("Updating", "Do not unplug the device.");
    shownPercent = percent;
    const int width = tft.width() - 16;
    tft.drawRect(8, 100, width, 14, ST77XX_WHITE);
    tft.fillRect(10, 102, (width - 4) * percent / 100, 10, 0xFA20);  // orange
}

String address() {
    if (WiFi.status() != WL_CONNECTED) return "Joining the WiFi...";
    return "http://" + deviceName + ".local\nhttp://" + WiFi.localIP().toString();
}

// ---------------------------------------------------------------- writing

bool mainLooksIntact() {
    if (!target) return false;
    esp_image_metadata_t data;
    const esp_partition_pos_t where = {target->address, target->size};
    return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &where, &data) == ESP_OK;
}

void fail(const String& why) {
    if (failed) return;
    failed = true;
    problem = why;
    if (writing) esp_ota_abort(handle);
    writing = false;
    show("Update failed", why + "\nTry again at\n" + address(), ST77XX_RED);
}

String hex(const unsigned char* bytes, size_t count) {
    static const char digits[] = "0123456789abcdef";
    String out;
    for (size_t i = 0; i < count; i++) {
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 15];
    }
    return out;
}

void onUpload() {
    HTTPUpload& part = server.upload();
    lastActivityMs = millis();
    if (part.status == UPLOAD_FILE_START) {
        failed = false;
        done = false;
        problem = "";
        written = 0;
        shownPercent = -1;
        expected = (size_t)server.arg("size").toInt();
        if (!target) return fail("This device has no slot for the firmware.");
        if (expected > target->size) return fail("The file is too large for this device.");
        // From the first byte on, the old firmware is gone.
        mainIntact = false;
        if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
            return fail("Could not start writing.");
        }
        writing = true;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);
        show("Updating", "Do not unplug the device.");
    } else if (part.status == UPLOAD_FILE_WRITE) {
        if (failed || !writing) return;
        if (written + part.currentSize > target->size) return fail("The file is too large for this device.");
        if (esp_ota_write(handle, part.buf, part.currentSize) != ESP_OK) return fail("Writing failed.");
        mbedtls_sha256_update(&sha, part.buf, part.currentSize);
        written += part.currentSize;
        showProgress();
    } else if (part.status == UPLOAD_FILE_END) {
        if (failed || !writing) return;
        writing = false;
        unsigned char digest[32];
        mbedtls_sha256_finish(&sha, digest);
        if (expected && written != expected) {
            esp_ota_abort(handle);
            return fail("The file arrived incomplete.");
        }
        if (esp_ota_end(handle) != ESP_OK) return fail("That file is not DeskHog firmware.");
        String wanted = server.arg("sha256");
        wanted.toLowerCase();
        if (wanted.length() && wanted != hex(digest, sizeof(digest))) return fail("The file was damaged on the way.");
        if (esp_ota_set_boot_partition(target) != ESP_OK) return fail("Could not select the new firmware.");
        mainIntact = true;
        done = true;
    } else if (part.status == UPLOAD_FILE_ABORTED) {
        fail("The connection was lost.");
    }
}

// ---------------------------------------------------------------- web

void sendStatus(int code = 200) {
    String json = "{\"updater\":true,\"version\":\"" UPDATER_VERSION "\",\"name\":\"" + deviceName + "\"";
    json += ",\"main_intact\":" + String(mainIntact ? "true" : "false");
    json += ",\"writing\":" + String(writing ? "true" : "false");
    json += ",\"written\":" + String((unsigned long)written);
    json += ",\"done\":" + String(done ? "true" : "false");
    json += ",\"slot_size\":" + String((unsigned long)(target ? target->size : 0));
    String quoted = problem;
    quoted.replace("\\", "\\\\");
    quoted.replace("\"", "\\\"");
    quoted.replace("\n", " ");
    json += ",\"error\":\"" + quoted + "\"}";
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(code, "application/json", json);
}

const char kPage[] PROGMEM = R"HTML(<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskHog updater</title>
<style>body{font:16px system-ui,sans-serif;max-width:34em;margin:2em auto;padding:0 1em;color:#222}
button{font:inherit;padding:.4em 1em;margin:.3em .3em .3em 0}#say{margin:1em 0;font-weight:600}</style>
<h1>DeskHog updater</h1>
<p>This device is waiting for firmware. If you were updating it from its web page, that page will carry on by itself.
To install a firmware file by hand, choose it here.</p>
<p><input type="file" id="file" accept=".bin"> <button id="send">Install</button></p>
<p><button id="back">Restart without updating</button></p>
<p id="say"></p>
<script>
const say = t => document.getElementById('say').textContent = t;
const wait = () => setTimeout(() => location.reload(), 15000);
document.getElementById('send').onclick = async () => {
  const file = document.getElementById('file').files[0];
  if (!file) return say('Choose a firmware file first.');
  const body = new FormData(); body.append('firmware', file);
  say('Installing. Do not unplug the device...');
  try {
    const res = await fetch('/api/firmware?size=' + file.size, { method: 'POST', body });
    const status = await res.json();
    if (!res.ok) return say('That did not work: ' + status.error);
    say('Installed. The device is restarting...'); wait();
  } catch (e) { say('The connection was lost. Reload this page and try again.'); }
};
document.getElementById('back').onclick = async () => {
  const res = await fetch('/api/restart', { method: 'POST' });
  if (res.ok) { say('Restarting...'); wait(); }
  else say('The firmware on this device is not complete, so there is nothing to restart into. Install a file.');
};
</script>)HTML";

void startWeb() {
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html", kPage); });
    server.on("/api/status", HTTP_GET, [] { sendStatus(); });
    server.on("/api/firmware", HTTP_POST, [] {
        sendStatus(done ? 200 : 400);
        if (done) {
            show("Updated", "Restarting...", ST77XX_GREEN);
            restartAtMs = millis() + 800;
        }
    }, onUpload);
    server.on("/api/restart", HTTP_POST, [] {
        if (writing || !mainIntact) return sendStatus(409);
        if (esp_ota_set_boot_partition(target) != ESP_OK) return sendStatus(500);
        sendStatus();
        show("Restarting", "");
        restartAtMs = millis() + 800;
    });
    server.onNotFound([] { server.send(404, "text/plain", "The updater has no such page."); });
    server.begin();
}

}  // namespace

void setup() {
#if defined(TFT_I2C_POWER)
    pinMode(TFT_I2C_POWER, OUTPUT);
    digitalWrite(TFT_I2C_POWER, HIGH);
#endif
    pinMode(TFT_BACKLITE, OUTPUT);
    digitalWrite(TFT_BACKLITE, HIGH);
    SPI.begin();
    tft.init(135, 240);
    tft.setRotation(1);

    target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    mainIntact = mainLooksIntact();

    Preferences settings;
    settings.begin("wifi_config", true);
    const String ssid = settings.getString("ssid", "");
    const String password = settings.getString("password", "");
    const String name = settings.getString("dm_name", "");
    settings.end();
    if (name.length()) deviceName = name;

    show("Ready to update", "Joining the WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(deviceName.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
    startWeb();
    lastActivityMs = millis();
}

void loop() {
    static bool announced = false;
    if (!announced && WiFi.status() == WL_CONNECTED) {
        announced = true;
        if (MDNS.begin(deviceName.c_str())) MDNS.addService("http", "tcp", 80);
        if (!writing && !failed) {
            show(mainIntact ? "Ready to update" : "Needs firmware", "Waiting for the file at\n" + address());
        }
    }
    server.handleClient();

    const uint32_t now = millis();
    if (restartAtMs && (int32_t)(now - restartAtMs) >= 0) ESP.restart();
    // Nobody came: carry on with the firmware that is there, if it is whole.
    if (!restartAtMs && !writing && mainIntact && now - lastActivityMs > kGiveUpMs) {
        if (esp_ota_set_boot_partition(target) == ESP_OK) {
            show("Restarting", "No update arrived.");
            delay(800);
            ESP.restart();
        }
        lastActivityMs = now;
    }
    delay(2);
}
