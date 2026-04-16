/**
 * @file BasicUsage.ino
 * @brief Non-blocking ESPWiFiManager example — standard WebServer edition.
 *
 * Supports ESP32 and ESP8266.
 *
 * Features demonstrated:
 *  • Smart Connect — connects to the strongest known network (RSSI sorted)
 *  • Non-blocking state machine — loop() never sleeps
 *  • Captive Portal fallback — opens AP when all connections fail
 *  • Serial command interface for runtime credential management
 */

// ── Platform-specific server ───────────────────────────────────────────────
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

// ── Global instances ───────────────────────────────────────────────────────
WiFiManager wifiManager("Cypher_Portal", "12345678");

bool apModeStarted     = false;
bool connectionHandled = false;

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialise internals and print available serial commands to monitor
  wifiManager.begin();

  Serial.println("\n[Main] Starting non-blocking WiFi scan...");
  wifiManager.connectToWiFi();
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  // ① Drive the WiFiManager (scanning, connecting, DNS, handleClient)
  wifiManager.process();

  // ② Optional serial command interface
  //    Prefix commands with "WIFI " e.g.:
  //      WIFI ADD "MySSID" "MyPass"
  //      WIFI DEL "MySSID"
  //      WIFI LIST
  //      WIFI STATUS
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("WIFI "))
      wifiManager.executeCommand(line.substring(5), Serial);
  }

  // ③ React to state changes
  WiFiState state = wifiManager.getState();

  if (state == WIFI_STATE_CONNECTED && !connectionHandled) {
    Serial.println("[Main] Connected! Starting application web server...");

    // Add your application routes here
    server.on("/hello", []() {
      server.send(200, "text/plain", "Hello from CypherNode!");
    });

    // Register server with manager so process() calls handleClient()
    wifiManager.setServer(&server);
    server.begin();
    connectionHandled = true;
  }

  if (state == WIFI_STATE_FAILED && !apModeStarted) {
    Serial.println("[Main] All connections failed. Starting captive portal...");
    wifiManager.startAPMode(server);
    apModeStarted = true;
  }

  // ④ Your sensor readings, LED blinks, etc. go here — no blocking waits!
}