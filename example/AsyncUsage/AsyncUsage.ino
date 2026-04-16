/**
 * @file AsyncUsage.ino
 * @brief Non-blocking ESPWiFiManager example — ESPAsyncWebServer edition.
 *
 * ─────────────────────────────────────────────────────────────────────────
 *  BEFORE compiling this example:
 *
 *  1. Open  libraries/ESPWiFiManager/src/ESPWiFiManagerConfig.h
 *  2. Uncomment the following line:
 *       #define WIFIMANAGER_USE_ASYNC_WEBSERVER
 *
 *  That is the only change needed — do not modify your sketch.
 *
 *  Required libraries (install via Arduino Library Manager or PlatformIO):
 *    • ESPAsyncWebServer
 *    • AsyncTCP       (ESP32)
 *    • ESPAsyncTCP    (ESP8266)
 * ─────────────────────────────────────────────────────────────────────────
 */

// ── Platform TCP dependency ────────────────────────────────────────────────
#if defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>

#include <ESPWiFiManager.h>

// ── Global instances ───────────────────────────────────────────────────────
AsyncWebServer server(80);
WiFiManager    wifiManager("ESP_Async_Portal", "12345678");

bool apModeStarted = false;

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESPWiFiManager Async Example ---");

  wifiManager.begin();
  wifiManager.connectToWiFi();
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  // Drive the state machine — AsyncWebServer handles its own requests
  wifiManager.process();

  if (wifiManager.getState() == WIFI_STATE_FAILED && !apModeStarted) {
    Serial.println("[Main] Starting Captive Portal (Async)...");
    wifiManager.startAPMode(server);
    apModeStarted = true;
  }

  // Optional serial commands: WIFI ADD "SSID" "PASS" | WIFI LIST | etc.
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("WIFI "))
      wifiManager.executeCommand(cmd.substring(5));
  }

  // Your application logic here — fully non-blocking!
}
