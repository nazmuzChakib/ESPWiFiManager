/**
 * @file BasicUsage.ino
 * @brief Non-blocking ESPWiFiManager example with Smart Connect (RSSI Sorting)
 * 
 * @details This example demonstrates how to use the ESPWiFiManager library to 
 * handle Wi-Fi connections gracefully without blocking the main event loop. 
 * It supports both ESP32 and ESP8266 platforms, dynamically selecting the 
 * right classes and libraries.
 * 
 * Key Features shown here:
 * - Smart Connect with RSSI Sorting (Connects to the strongest known network)
 * - Non-blocking asynchronous connection attempts
 * - Fallback to AP (Access Point) mode with a Captive Portal if all connections fail
 * - Serial Commands integration
 * - Integration with a normal WebServer
 */

#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  WebServer server(80);  /**< Global WebServer instance for ESP32 on port 80 */
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  ESP8266WebServer server(80); /**< Global WebServer instance for ESP8266 on port 80 */
#endif

#include <ESPWiFiManager.h>

/**
 * @brief Global WiFiManager instance
 * 
 * Initializes the library with the Access Point SSID and Password.
 * This is used as a fallback if no valid Wi-Fi credentials are found.
 */
WiFiManager wifiManager("Cypher_Portal", "12345678");

/** @brief Flag to ensure AP mode initialization runs only once */
bool apModeStarted = false;

/** @brief Flag to ensure post-connection setup runs only once */
bool connectionHandled = false;

/**
 * @brief Setup Function
 * 
 * Invoked once at startup. Initializes Serial communication,
 * prepares the WiFiManager, and requests an asynchronous Wi-Fi connection.
 */
void setup() {
  Serial.begin(115200);
  delay(1000); 

  // Initialize WiFiManager internal states and prints help menu
  wifiManager.begin();
  
  Serial.println("\n[Main] Initiating Non-blocking WiFi Connection...");
  
  // Start the background connection process (Non-Blocking)
  wifiManager.connectToWiFi();
}

/**
 * @brief Main execution loop
 * 
 * Continuously processes WiFiManager state transitions, handles DNS and
 * Web server requests without blocking. User logic can optionally be 
 * placed at the end of this loop to run fluidly alongside connectivity tasks.
 */
void loop() {
  // 1. Process WiFi Manager (Handles scanning, connection timeouts, and web UI)
  wifiManager.process();

  // Handle incoming commands dynamically over the Serial Monitor
  if (Serial.available()) {
    String serialData = Serial.readStringUntil('\n');
    serialData.trim();
    
    // if command start with "WIFI"
    if (serialData.startsWith("WIFI")) {
      String wifiCmd = serialData.substring(5); 
      wifiManager.executeCommand(wifiCmd, Serial);
    } else {
      Serial.println("[Main] Unknown command: " + serialData);
    }
  }

  // 2. Application Logic based on the internal State Machine of WiFiManager
  WiFiState currentState = wifiManager.getState();

  if (currentState == WIFI_STATE_CONNECTED && !connectionHandled) {
    Serial.println("[Main] Wi-Fi is Connected! Starting Web Server for general UI...");
    
    // Bind our main server to the WiFiManager's portal infrastructure
    wifiManager.setServer(&server);
    
    // Example route for your main application
    server.on("/hello", []() {
      server.send(200, "text/plain", "Hello from CypherNode!");
    });
    
    server.begin(); 
    connectionHandled = true;
  } 
  else if (currentState == WIFI_STATE_FAILED && !apModeStarted) {
    Serial.println("[Main] All connections failed. Falling back to AP Mode.");
    // Start the Access Point & Captive Portal to allow users to feed new WiFi credentials
    wifiManager.startAPMode(server);
    apModeStarted = true;
  }

  // Handle general web requests if connected or in AP mode
  if (connectionHandled || apModeStarted) {
    // Note: server.handleClient() is internally handled by wifiManager.process();
    // No need to explicitly call it here.
  }
  
  // ---------------------------------------------------------
  // Put your sensor readings, LED blinks, and physical switch 
  // logic here! It will run smoothly without freezing because 
  // no blocking while() loops were used for Wi-Fi connections.
  // ---------------------------------------------------------
}