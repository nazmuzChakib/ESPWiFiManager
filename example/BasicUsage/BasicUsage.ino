/**
 * @file BasicUsage.ino
 * @brief Non-blocking ESPWiFiManager example for ESP32 & ESP8266
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  WebServer server(80);
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  ESP8266WebServer server(80);
#endif

#include <ESPWiFiManager.h>

// Initialize library
WiFiManager wifiManager("ESP_Setup_Portal", "12345678");

// We use a simple flag to track when we switch to AP mode or successful connection
bool apModeStarted = false;
bool connectionHandled = false;

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to stabilize

  wifiManager.begin();
  
  Serial.println("\n[Main] Initiating Non-blocking WiFi Connection...");
  // Start the connection process (Non-Blocking!)
  wifiManager.connectToWiFi();
}

void loop() {
  // 1. Process WiFi Manager (Handles connection timeouts, web server, and DNS)
  wifiManager.process();

  // 2. Handle Serial Interactions
  wifiManager.handleSerialCommands(Serial);

  // 3. Application Logic based on State Machine
  WiFiState currentState = wifiManager.getState();

  if (currentState == WIFI_STATE_CONNECTED && !connectionHandled) {
    Serial.println("[Main] Wi-Fi is Connected! Starting Web Server for general UI...");
    wifiManager.setServer(&server);
    server.begin(); 
    connectionHandled = true;
    
    // You can start doing other smart home tasks here!
  } 
  else if (currentState == WIFI_STATE_FAILED && !apModeStarted) {
    Serial.println("[Main] All connections failed. Falling back to AP Mode.");
    wifiManager.startAPMode(server);
    apModeStarted = true;
  }

  // ---- Other Non-Blocking Logic Can Go Here ----
  // Since we removed while(), this code will execute smoothly!
  // Example: blink an LED, read sensors, etc.
}