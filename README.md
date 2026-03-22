# ESPWiFiManager

ESPWiFiManager is a small, smart, and easy-to-use library for ESP32 & ESP8266 that provides a web-based captive portal and simple serial commands to configure Wi‑Fi credentials at runtime. It is designed to be lightweight, with no additional dependencies beyond the default Arduino core libraries.

This README documents the goals, installation, usage patterns, captive portal features, utilities included with the repository, and troubleshooting tips.

---

## Table of contents
- Overview
- Features
- Repository layout
- Installation
- Quick start (example sketch)
- Usage details
  - Captive portal behavior
  - Serial configuration commands
  - Public API (overview)
- Customizing the portal (HTML, AP name, timeouts)
- Utilities (html_to_header.py)
- Troubleshooting
- Contributing
- References to repo files
- License

---

## Overview
ESPWiFiManager lets an ESP32 or ESP8266 automatically attempt to connect to the strongest known network and fall back to a Wi‑Fi configuration portal when it cannot connect. The device acts as an Access Point (AP) and a webserver that hosts a simple UI for entering an SSID and password. Once credentials are saved, the library persists them in non-volatile storage (NVS for ESP32 or EEPROM for ESP8266). The user can then interact with the manager non-blockingly.

## Features
- **Smart Connect (RSSI Sorting):** Automatically scans and connects to the strongest known network in range.
- **Non-blocking Architecture:** Connections are handled entirely in the background without `while()` loops, keeping your main loop fluid.
- **FIFO Credential Memory:** Limits saved networks (10 for ESP32, 5 for ESP8266) and automatically removes the oldest when full.
- **Centralized Command Routing:** Allows passing string commands dynamically (e.g., `wifiManager.executeCommand("ADD \"SSID\" \"PASS\"", Serial)`).
- **Embedded Web UI:** Auto-redirects to the setup page when the device is in AP mode (captive portal behavior).
- **Zero External Dependencies:** Uses the default ESP32/ESP8266 Wi‑Fi and WebServer libraries.
- **Python Utility included:** Convert standalone HTML pages into an embedded C header.

## Repository layout
- `src/`
  - `ESPWiFiManager.h`
  - `ESPWiFiManager.cpp`
  - `page_index.h` (generated from utils/index.html)
- `utils/`
  - `index.html` (web UI template for the captive portal)
  - `html_to_header.py` (script to convert index.html into page_index.h)
- `example/`
  - `BasicUsage/BasicUsage.ino` (example sketch)
- `library.properties`
- `README.md`

## Installation
There are two common ways to add this library to your Arduino/PlatformIO project:

1. **Manual (copy)**
   - Copy the repository into your Arduino Library folder (`Documents/Arduino/libraries/ESPWiFiManager`).
   - Ensure `ESPWiFiManager.h` and `ESPWiFiManager.cpp` are visible to your build system.

2. **PlatformIO**
   - Add the library to your project `lib/` folder or add a library dependency entry that points to the local copy or GitHub repository.

*Note: If the captive portal HTML is modified, regenerate `page_index.h` using the python script in `utils/`.*

## Quick start (example sketch)
This is the recommended non-blocking usage pattern. Check the header file for exact method names.

```cpp
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
WiFiManager wifiManager("Cypher_Portal", "12345678");

bool apModeStarted = false;
bool connectionHandled = false;

void setup() {
  Serial.begin(115200);
  delay(1000); 

  wifiManager.begin();
  
  Serial.println("\n[Main] Initiating Non-blocking WiFi Connection...");
  // Start the background connection process
  wifiManager.connectToWiFi();
}

void loop() {
  // 1. Process WiFi Manager (Handles scanning, connection timeouts, and web UI)
  wifiManager.process();

  // Handle incoming commands dynamically over the Serial Monitor
  if (Serial.available()) {
    String serialData = Serial.readStringUntil('\n');
    serialData.trim();
    if (serialData.startsWith("WIFI")) {
      String wifiCmd = serialData.substring(5); 
      wifiManager.executeCommand(wifiCmd, Serial);
    }
  }

  // 2. Application Logic based on State Machine
  WiFiState currentState = wifiManager.getState();

  if (currentState == WIFI_STATE_CONNECTED && !connectionHandled) {
    Serial.println("[Main] Wi-Fi is Connected! Starting Web Server for general UI...");
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
    wifiManager.startAPMode(server);
    apModeStarted = true;
  }
}
```

## Usage details

### Captive portal behavior
- When the ESP device cannot connect to Wi‑Fi, it sets its state to `WIFI_STATE_FAILED`. You can detect this and boot an Access Point (AP).
- Calling `startAPMode(server)` assigns the provided web server instance to manage captive portal routing.
- DNS is hijacked so any domain request points to the setup UI.

### Serial configuration commands
- Using `executeCommand()`, you can route custom strings into the manager. Typical commands are:
  - `ADD "SSID" "PASS"` - Add or update a credential.
  - `DEL "SSID"` - Remove a network.
  - `LIST` - List networks.
  - `CLEAR` - Erase all credentials.
  - `STATUS` - Fetch the current active state and IP.

### Public API (overview)
- `WiFiManager(ap_ssid, ap_password)` — constructor.
- `begin()` — Initialize internals.
- `connectToWiFi()` — Non-blocking trigger to scan and connect using RSSI sorting.
- `process()` — Must be placed inside `loop()`. Manages state timeouts and web handler pipelines non-blockingly.
- `startAPMode(server)` — Starts SoftAP.
- `setServer(&server)` — Rebind the manager to the general Web Server after connection so credentials can be edited.
- `getState()` — Returns `WiFiState` enum (`IDLE`, `SCANNING`, `CONNECTING`, `CONNECTED`, `FAILED`, `AP_MODE`).
- `executeCommand(cmdStr, io)` — Passthrough for serial interactions.

## Customizing the portal (HTML, AP name, timeouts)
- The captive portal UI is embedded in `src/page_index.h` as an `unsigned char` array.
- To customize the UI:
  1. Edit `utils/index.html`.
  2. Use `utils/html_to_header.py` to convert the HTML into `page_index.h`.
     ```bash
     python3 utils/html_to_header.py utils/index.html
     ```
     This automatically creates `src/page_index.h` keeping the structure clean.
  3. Recompile the Arduino sketch to deploy changes.

## Utilities
- `utils/index.html` - Default web UI.
- `utils/html_to_header.py` - Compression and conversion script.

## Troubleshooting
- **Portal does not appear:** Confirm that `wifiManager.startAPMode(server)` was called (typically triggered when `getState() == WIFI_STATE_FAILED`). Connect to the device AP and visit `192.168.4.1`.
- **Cannot connect after saving credentials:** Check serial monitor to verify credentials passed validation. Max storage is 10 connections (ESP32) or 5 (ESP8266).
- **HTML changes do not show:** Make sure you ran the `html_to_header.py` script and successfully reflashed the firmware.

## Contributing
Contributions and bug reports are welcome. If you modify the portal HTML, generate the `page_index.h` and include both in your pull request. 

Please provide device logs and board details when filing an issue!

## References to repo files
- Source files:
  - `src/ESPWiFiManager.h`
  - `src/ESPWiFiManager.cpp`
  - `src/page_index.h`
- Utilities:
  - `utils/index.html`
  - `utils/html_to_header.py`

## License
This project is released under the MIT License. See the LICENSE file in the repository for details.
