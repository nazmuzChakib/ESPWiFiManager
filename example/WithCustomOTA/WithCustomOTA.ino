#include <WiFi.h>
#include <WebServer.h>
#include <ESPWiFiManager.h>
#include <OTAUpdater.h>

WebServer server(80);
OTAUpdater ota(&server);
WiFiManager wifiManager("ESP32_Setup", "12345678"); // AP SSID/Pass (AP only)

const char homepage[] PROGMEM = R"=====(
  <html>
    <head>
    <title>ESP32 Web Server</title>
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            margin-top: 50px;
        }
        button {
            padding: 10px 20px;
            font-size: 16px;
            cursor: pointer;
        }
        h1, h2 {
            color: #333;
        }
        p {
            font-size: 14px;
            color: #666;
        }
        /* Responsive UI */
        @media (max-width: 600px) {
            body {
                margin-top: 20px;
            }
            button {
                width: 100%;
                font-size: 14px;
            }
        }

        /* color combinations */
        body {
            background-color: #f0f0f0;
            color: #333;
        }
        button {
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 5px;
        }
        button:hover {
            background-color: #45a049;
        }
    </style>
    </head>
  <body>
    <h1>Welcome to the ESP32 Web Server</h1>
    <h2>
      Go to the "Tools" menu in the Arduino IDE and select "Port" to find your
      ESP32 board's IP for OTA Updates.
    </h2>
    <button onclick="firmwareUpdate()">Firmware Update</button>
  </body>
  <script>
    function firmwareUpdate() {
      window.location.href = "/system-update";
    }
  </script>
</html>
)=====";

void handleRoot() {
  server.send_P(200, "text/html", homepage);
}

void printHelp() {
  Serial.println("=== WiFiManager Serial Commands ===");
  Serial.println("ADD \"SSID with spaces\" \"password with spaces\"");
  Serial.println("DEL \"SSID with spaces\"");
  Serial.println("LIST");
  Serial.println("CLEAR");
  Serial.println("HELP");
  Serial.println("REBOOT");
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
    Serial.println(WiFi.localIP());
    // You may still want to serve UI while connected:
    // start server on root path
    wifiManager.setServer(&server);
    // Start the web server
    server.on("/", handleRoot);
    server.begin();
    Serial.println("Web server started.");
    ota.begin();
    ota.serveWebInterface();
    Serial.println("OTA function is enabled!");
    // wifiManager.startAPMode(server); // will serve UI on STA IP as well
  }

  printHelp();
}

void loop() {
  wifiManager.process();
  wifiManager.handleSerialCommands(Serial);
  // if (WiFi.status() == WL_CONNECTED) {
  //   server.handleClient(); // Handle web requests
  // }
}