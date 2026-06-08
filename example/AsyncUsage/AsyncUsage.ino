/**
 * @file AsyncUsage.ino
 * @brief ESPWiFiManager v5 — ESPAsyncWebServer example.
 *
 * ─────────────────────────────────────────────────────────────────────────
 *  BEFORE compiling:
 *  1. Open  libraries/ESPWiFiManager/src/ESPWiFiManagerConfig.h
 *  2. Uncomment:  #define WIFIMANAGER_USE_ASYNC_WEBSERVER
 *
 *  Required libraries (Arduino Library Manager / PlatformIO):
 *    • ESPAsyncWebServer
 *    • AsyncTCP       (ESP32)
 *    • ESPAsyncTCP    (ESP8266)
 * ─────────────────────────────────────────────────────────────────────────
 *
 * Demonstrates:
 *  • Auto AP Fallback (automatic, zero sketch code needed)
 *  • Event Callbacks for state transitions
 *  • Background reconnect while portal is active
 *  • Async request handling in STA mode
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

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESPWiFiManager v5 — AsyncUsage Example ===");

  // ── Optional tuning ────────────────────────────────────────────────────
  // wifiManager.setLogLevel(WIFI_LOG_DEBUG);
  // wifiManager.setAPTimeout(5 * 60 * 1000);  // 5-minute AP timeout
  // wifiManager.setAPConfig(11, false, 4);     // channel 11

  // ── Register portal server for Auto AP Fallback ────────────────────────
  wifiManager.setAutoAPFallback(true, &server);

  // ── Event Callbacks ────────────────────────────────────────────────────
  wifiManager.onStateChange([](WiFiState oldState, WiFiState newState) {
    Serial.printf("[App] State: %d → %d\n", (int)oldState, (int)newState);
  });

  wifiManager.onStationConnected([](const String& ssid, IPAddress ip) {
    Serial.printf("[App] ✓ Connected: '%s'  IP: %s\n",
                  ssid.c_str(), ip.toString().c_str());

    // Register application-level async routes
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req) {
      req->send(200, "application/json",
                "{\"status\":\"connected\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
    });
    server.begin();
  });

  wifiManager.onStationDisconnected([](int reason) {
    Serial.printf("[App] ✗ Disconnected. Reason: %d\n", reason);
  });

  wifiManager.onAPModeStarted([](const String& ssid, IPAddress ip) {
    Serial.printf("[App] Portal active  SSID: %s  →  http://%s\n",
                  ssid.c_str(), ip.toString().c_str());
  });

  wifiManager.onAPModeStopped([]() {
    Serial.println("[App] Portal stopped. Resuming normal operation.");
  });

  // ── begin() starts the first scan automatically ────────────────────────
  wifiManager.begin();
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  wifiManager.process();  // AsyncWebServer is self-driven; this handles DNS + state

  // Optional serial command interface:
  //   WIFI STATUS | WIFI LIST | WIFI ADD "SSID" "PASS" | WIFI RECONNECT
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("WIFI "))
      wifiManager.executeCommand(cmd.substring(5));
  }
}
