#include <WiFi.h>
#include <WebServer.h>
#include <ESPWiFiManager.h>

WebServer server(80);
WiFiManager wifiManager("ESP32_Setup", "12345678"); // AP SSID/Pass (AP only)

void printHelp() {
  Serial.println("=== WiFiManager Serial Commands ===");
  Serial.println("ADD \"SSID with spaces\" \"password with spaces\"");
  Serial.println("DEL \"SSID with spaces\"");
  Serial.println("LIST");
  Serial.println("CLEAR");
  Serial.println("HELP");
  Serial.println("-----------------------------------");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Try STA first with saved creds; if fails, start AP portal
  if (!wifiManager.connectToWiFi()) {
    Serial.println("Starting AP portal...");
    wifiManager.startAPMode(server);
  } else {
    Serial.print("Connected. IP: ");
    // set server for sta mode as well
    wifiManager.setServer(&server);
    Serial.println(WiFi.localIP());
    // You may still want to serve UI while connected:
    wifiManager.startAPMode(server); // will serve UI on STA IP as well
  }

  printHelp();
}

void loop() {
  wifiManager.process(); // Handle DNS and web requests
  wifiManager.handleSerialCommands(Serial);
}