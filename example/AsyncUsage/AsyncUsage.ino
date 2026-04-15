/*
 * ESPWiFiManager - AsyncWebServer Example
 * 
 * IMPORTANT: To run this example, you MUST uncomment the following line
 * inside the `src/ESPWiFiManager.h` file:
 * #define WIFIMANAGER_USE_ASYNC_WEBSERVER
 * 
 * Dependencies:
 * - ESPAsyncWebServer library
 * - AsyncTCP (for ESP32) or ESPAsyncTCP (for ESP8266)
 */

#include <Arduino.h>
#include <ESPWiFiManager.h>

// 1. Include Async Web Server headers based on the platform
#if defined(ESP32)
  #include <WiFi.h>
  #include <AsyncTCP.h>
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>

// 2. Create the AsyncWebServer instance on port 80
AsyncWebServer server(80);

// 3. Initialize ESPWiFiManager (AP_SSID, AP_Password)
WiFiManager wifiManager("ESP_Async_Portal", "12345678");

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESPWiFiManager Async Example ---");
  
  // 4. Initialize internals (loads saved credentials)
  wifiManager.begin();
  
  // 5. Trigger connection attempt (Smart Connect)
  wifiManager.connectToWiFi();
}

void loop() {
  // 6. Keep the manager processing (Essential for state transitions and DNS)
  wifiManager.process();

  // 7. Handle state transitions
  WiFiState state = wifiManager.getState();
  static bool apModeStarted = false;

  if (state == WIFI_STATE_FAILED && !apModeStarted) {
    // Connection failed. Start the captive portal using the AsyncWebServer.
    Serial.println("Starting Captive Portal in AP Mode...");
    wifiManager.startAPMode(server);
    apModeStarted = true;
  }
  
  // 8. Handle Serial Commands (Optional)
  // Available commands: ADD "SSID" "PASS", DEL "SSID", LIST, CLEAR, STATUS
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    wifiManager.executeCommand(cmd);
  }
}
