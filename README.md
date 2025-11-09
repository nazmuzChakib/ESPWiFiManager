# ESPWiFiManager (ESP32)

ESPWiFiManager is a small, easy-to-use library for ESP32 that provides a web-based captive portal and simple serial commands to configure Wi‑Fi credentials at runtime. It is designed to be lightweight, with no additional dependencies beyond the default ESP32 Arduino core libraries.

This README documents the goals, installation, public usage patterns, captive portal features, utilities included with the repository, and troubleshooting tips.

---

Table of contents
- Overview
- Features
- Repository layout
- Installation
- Quick start (example sketch)
- Usage details
  - Captive portal behavior
  - Serial configuration commands
  - Common API patterns
- Customizing the portal (HTML, AP name, timeouts)
- Utilities (html_to_header.py)
- Troubleshooting
- Contributing
- License

---

Overview
--------
ESPWiFiManager lets an ESP32 automatically present a Wi‑Fi configuration portal when it cannot connect to previously stored credentials. The device acts as an Access Point (AP) and a webserver that hosts a simple UI for entering SSID and password. Once credentials are saved, the library attempts to connect to the network and persists credentials in non-volatile storage (NVS). The portal can also be opened manually (for example via a serial command) to change credentials at any time.

Features
--------
- Web-based captive portal for entering Wi‑Fi SSID and password.
- Auto-redirect to the setup page when the device is in AP mode (captive portal behavior).
- Ability to change credentials via a web UI and via serial commands.
- No extra external dependencies — uses the default ESP32 Wi‑Fi and WebServer libraries.
- Includes utilities to convert a standalone HTML page into an embedded C header (page_index.h).
- Lightweight and easy to integrate into existing sketches.

Repository layout
-----------------
- src/
  - ESPWiFiManager.h
  - ESPWiFiManager.cpp
  - page_index.h (generated from utils/index.html, embedded web UI)
- utils/
  - index.html (web UI template for the captive portal)
  - html_to_header.py (script to convert index.html into page_index.h)
- example/
  - (example sketches — use as a starting point)
- library.properties
- README.md

(These files are included in the repo — see src/ and utils/ for source and the portal HTML.)

Installation
------------
There are two common ways to add this library to your Arduino/PlatformIO project:

1. Manual (copy)
   - Copy the `src/` directory into your Arduino library folder or into your project `lib/` folder.
   - Ensure `ESPWiFiManager.h` and `ESPWiFiManager.cpp` are visible to your build system.

2. PlatformIO
   - Add the library to your project `lib/` folder or add a library dependency entry that points to the local copy or GitHub repository.

Note: If the captive portal HTML was modified, you should regenerate `page_index.h` using the python script in `utils/` (instructions below).

Quick start (example sketch)
---------------------------
This is a minimal usage pattern. Check the header file for the exact class and method names if you need to match the API in your checked-out version.

```cpp
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
```

Usage details
-------------
Captive portal behavior
- When the ESP32 cannot connect to Wi‑Fi or when instructed to start the portal, the device creates its own Access Point (AP).
- The portal typically has a simple web UI where a user can pick an SSID and enter a password.
- The library attempts to redirect HTTP requests to the portal page so that opening any page in a browser brings up the configuration UI (captive behavior).

Serial configuration commands
- The project supports simple serial commands to start the portal or reset Wi‑Fi credentials. Typical commands include:
  - A command to start the configuration AP/portal manually (for example: "config" or a single character).
  - A command to reset stored Wi‑Fi credentials so the portal will appear on next boot.
- Check the sketch example or `ESPWiFiManager.cpp` for the exact serial CLI strings supported.

Public API (overview)
- The repository exposes a manager class that encapsulates portal startup, credential persistence, and connection attempts.
- Typical (common) methods you can expect:
  - constructor — to create the manager object
  - begin() — initialize internal state and server
  - autoConnect(apName [, apPassword]) — start portal or connect using stored credentials
  - startConfigPortal(apName [, apPassword, timeout]) — explicitly start the portal and wait for credentials
  - resetSettings() — clear stored Wi‑Fi credentials
  - setTimeout(seconds) — set portal timeout (if supported)
  - handle() — process background tasks / serve web requests (if your sketch needs to call it)
- For exact method names and signatures: refer to src/ESPWiFiManager.h (this README intentionally provides an overview and example usage pattern — check the header for exact API).

Customizing the portal (HTML, AP name, timeouts)
------------------------------------------------
- The captive portal UI is embedded in `src/page_index.h`. This header contains the HTML served by the device.
- To customize the UI:
  1. Edit `utils/index.html`.
  2. Use `utils/html_to_header.py` to convert the HTML into the `page_index.h` C header format used by the library.
     - Example (from your development machine):
       python3 utils/html_to_header.py utils/index.html src/page_index.h
     - The script wraps the HTML into a byte array or string literal that the ESP32 can serve from flash.
- You can also set the AP name and optional AP password when starting the portal via the manager API (see API overview above). Additionally, many implementations support a portal timeout so the device returns to normal operation if the portal is not used.

Utilities
---------
- utils/index.html
  - Default captive portal web UI. Edit this to change the look & text of your portal.
- utils/html_to_header.py
  - Converts an HTML file into a C header file containing the HTML to embed in the firmware.
  - Use this script whenever you change `index.html`.

Troubleshooting
---------------
- Portal does not appear
  - Ensure the device failed to connect with stored credentials (or that you explicitly started the portal).
  - Confirm the ESP32 created an AP — scan from your phone/PC for the AP name.
  - If the AP exists but the portal does not redirect, open the AP IP address (commonly 192.168.4.1) in a browser.
- Cannot connect after saving credentials
  - Check SSID and password correctness.
  - Verify region/channel/regulatory constraints for the network.
  - Inspect serial logs for detailed connection errors.
- HTML changes do not show on device
  - Regenerate `page_index.h` using `html_to_header.py`.
  - Rebuild and upload the firmware.

Contributing
------------
Contributions, bug reports, and improvements are welcome. If you change the portal HTML, please regenerate the `page_index.h` and include both the HTML and the generated header in your pull request so reviewers can see both the source and the generated artifact.

When opening issues:
- Provide the platform details (ESP32 board variant and Arduino core version).
- Paste serial logs (with Debug level if possible).
- Describe the steps to reproduce the issue.

References to repo files
-----------------------
- Source files:
  - src/ESPWiFiManager.h — public API & implementation declarations
    - https://github.com/nazmuzChakib/ESPWiFiManager/blob/main/src/ESPWiFiManager.h
  - src/ESPWiFiManager.cpp — implementation
    - https://github.com/nazmuzChakib/ESPWiFiManager/blob/main/src/ESPWiFiManager.cpp
  - src/page_index.h — embedded portal HTML (generated)
    - https://github.com/nazmuzChakib/ESPWiFiManager/blob/main/src/page_index.h
- Utilities:
  - utils/index.html — captive portal template
    - https://github.com/nazmuzChakib/ESPWiFiManager/blob/main/utils/index.html
  - utils/html_to_header.py — conversion script
    - https://github.com/nazmuzChakib/ESPWiFiManager/blob/main/utils/html_to_header.py
- Example sketches are provided in the example/ folder.

License
-------
This project is released under the MIT License. See the LICENSE file in the repository for details.

---

If you want, I can:
- Add or update an example sketch with a tested minimal usage for Arduino or PlatformIO.
- Regenerate page_index.h from a modified index.html if you provide changes to the portal UI.
- Extract exact public API signatures from src/ESPWiFiManager.h and include a precise programmable reference section in this README.
