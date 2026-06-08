/**
 * @file BasicUsage.ino
 * @brief ESPWiFiManager v5 — Standard WebServer example.
 *
 * Demonstrates:
 *  • Auto AP Fallback — portal starts automatically on connection failure
 *  • Event Callbacks  — no manual state-polling needed
 *  • Exponential Backoff reconnection
 *  • Background scan while portal is open — auto-reconnects to STA
 *  • Static IP configuration (optional)
 *  • Runtime Serial commands for credential management
 *  • Low-level WiFi tuning (Tx power, sleep mode)
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

// ── Global instance ────────────────────────────────────────────────────────
WiFiManager wifiManager("Cypher_Portal", "12345678");

// ── Application state ─────────────────────────────────────────────────────
bool serverStarted = false;

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESPWiFiManager v5 — BasicUsage Example ===");

  // ── Optional: Static IP ────────────────────────────────────────────────
  // Uncomment to use a fixed IP instead of DHCP:
  // wifiManager.setStaticIP(
  //   IPAddress(192, 168, 1, 100),   // IP
  //   IPAddress(192, 168, 1, 1),     // Gateway
  //   IPAddress(255, 255, 255, 0),   // Subnet
  //   IPAddress(8, 8, 8, 8)          // DNS
  // );

  // ── Optional: Low-level tuning ─────────────────────────────────────────
  // wifiManager.setTxPower(17.0f);   // 17 dBm Tx power
  // wifiManager.setWiFiSleep(false); // Disable modem sleep for lower latency

  // ── Optional: AP configuration ────────────────────────────────────────
  // wifiManager.setAPConfig(6, false, 4); // channel 6, visible, max 4 clients

  // ── Optional: AP timeout — revert to STA scan after 3 minutes ─────────
  // wifiManager.setAPTimeout(3 * 60 * 1000);

  // ── Optional: Log level ────────────────────────────────────────────────
  wifiManager.setLogLevel(WIFI_LOG_INFO);

  // ── Register the portal server for Auto AP Fallback ───────────────────
  wifiManager.setAutoAPFallback(true, &server);
  wifiManager.setBackgroundReconnect(true);

  // ── Event Callbacks ────────────────────────────────────────────────────
  wifiManager.onStateChange([](WiFiState oldState, WiFiState newState) {
    Serial.printf("[App] State: %d → %d\n", (int)oldState, (int)newState);
  });

  wifiManager.onStationConnected([](const String& ssid, IPAddress ip) {
    Serial.printf("[App] ✓ Connected to '%s'  IP: %s\n",
                  ssid.c_str(), ip.toString().c_str());

    // Register your application routes here
    // (avoid re-registering if called again after AP auto-recover)
    if (!serverStarted) {
      server.on("/hello", []() {
        server.send(200, "text/plain", "Hello from ESPWiFiManager v5!");
      });
      wifiManager.setServer(&server);
      server.begin();
      serverStarted = true;
    }
  });

  wifiManager.onStationDisconnected([](int reason) {
    Serial.printf("[App] ✗ Disconnected. Reason: %d\n", reason);
  });

  wifiManager.onAPModeStarted([](const String& ssid, IPAddress ip) {
    Serial.printf("[App] Portal UP  SSID: %s  IP: %s\n",
                  ssid.c_str(), ip.toString().c_str());
  });

  wifiManager.onAPModeStopped([]() {
    Serial.println("[App] Portal closed.");
  });

  wifiManager.onCredentialsChanged([]() {
    Serial.println("[App] Credentials updated.");
  });

  // ── begin() kicks off the first scan automatically ─────────────────────
  wifiManager.begin();
}

// ── Loop ───────────────────────────────────────────────────────────────────
void loop() {
  // Drive the state machine — this is all you need!
  wifiManager.process();

  // ── Optional serial command interface ──────────────────────────────────
  //   WIFI ADD "MySSID" "MyPass"   → save credential
  //   WIFI DEL "MySSID"            → remove credential
  //   WIFI LIST                    → list all saved SSIDs
  //   WIFI STATUS                  → print state + IPs
  //   WIFI RECONNECT               → force reconnect cycle
  //   WIFI APSTART                 → force-start portal
  //   WIFI APSTOP                  → stop portal
  //   WIFI LOGLEVEL 3              → set log level (0=none, 3=debug)
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("WIFI "))
      wifiManager.executeCommand(line.substring(5), Serial);
  }

  // ── Your application logic here — fully non-blocking! ──────────────────
}