/**
 * @file BasicUsage.ino
 * @author Cypher-Z (cypherz@gmail.com)
 * @brief This example demonstrates the basic usage of the ESPWiFiManager library
 *        for managing WiFi connections on an ESP32 device.
 * @version 1.0
 * @date 2025-11-09
 * 
 * @copyright Copyright Cypher-Z (c) 2025
 * 
 */

#include <WiFi.h>           // ESP32 WiFi support (station + softAP)
#include <WebServer.h>      // Simple web server for handling HTTP requests
#include <ESPWiFiManager.h> // Library that manages WiFi connection + captive portal

// Create a WebServer instance that listens on port 80
WebServer server(80);

// Create WiFiManager object:
// - First parameter is the Access Point (AP) SSID to be used when starting the setup portal
// - Second parameter is the AP password (AP-only; used when the library starts the portal)
WiFiManager wifiManager("ESP32_Setup", "12345678");

void setup() {
  // Initialize serial port for debug messages
  Serial.begin(115200);
  delay(200); // short delay to allow Serial to initialize

  // Try to connect to WiFi using previously saved credentials (STA mode).
  // connectToWiFi() returns true if connection succeeded, false otherwise.
  if (!wifiManager.connectToWiFi()) {
    // No saved creds or connection failed: start AP portal for user to configure WiFi
    Serial.println("Starting AP portal...");
    // This starts a softAP and serves the configuration web UI using the provided server
    wifiManager.startAPMode(server);
  } else {
    // Connected in STA mode
    Serial.print("Connected. IP: ");
    // Set the WebServer to be used when in STA mode so the same UI can be served
    wifiManager.setServer(&server);
    // Print the assigned IP address for the station interface
    Serial.println(WiFi.localIP());
    // Note: you can also start the AP portal while connected if you want to serve the UI on the STA IP:
    // wifiManager.startAPMode(server);
  }

  // Initialize WiFiManager internals and enable debug output on Serial.
  // Call this after you've set up the server/connection state.
  wifiManager.begin();
}

void loop() {
  // Regularly call process() to let WiFiManager handle DNS redirection (captive portal),
  // serve web pages, and handle WiFi portal requests.
  wifiManager.process();

  // Allow interacting with the WiFiManager via Serial commands (if supported by the library).
  // This can let you trigger actions (like resetting saved credentials) over Serial.
  wifiManager.handleSerialCommands(Serial);
}