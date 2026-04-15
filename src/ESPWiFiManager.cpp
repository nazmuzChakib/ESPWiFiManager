#include "ESPWiFiManager.h"
#include "page_index.h"

// -------------------- Constructor --------------------
/**
 * @brief Constructs a new WiFiManager object.
 * 
 * @param ap_ssid The SSID for the Access Point.
 * @param ap_password The password for the Access Point.
 */
WiFiManager::WiFiManager(const char* ap_ssid, const char* ap_password)
: _ap_ssid(ap_ssid), _ap_password(ap_password) {}

// -------------------- Storage Helpers (ESP32 & ESP8266) --------------------
/**
 * @brief Loads the credentials data from persistent storage.
 * 
 * @return String JSON data containing the credentials.
 */
String WiFiManager::_loadData() const {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, true);
  String json = _prefs.getString(NVS_KEY_MULTI, "[]");
  _prefs.end();
  return json;
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  int len = EEPROM.read(0) | (EEPROM.read(1) << 8); 
  if (len == 0 || len > EEPROM_SIZE - 2 || len == 0xFFFF) {
    return "[]";
  }
  String json;
  json.reserve(len);
  for (int i = 0; i < len; i++) {
    json += (char)EEPROM.read(2 + i);
  }
  return json;
#endif
}

/**
 * @brief Saves the credentials data to persistent storage.
 * 
 * @param js JSON string to save.
 */
void WiFiManager::_saveData(const String& js) {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString(NVS_KEY_MULTI, js);
  _prefs.end();
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  int len = js.length();
  EEPROM.write(0, len & 0xFF);
  EEPROM.write(1, (len >> 8) & 0xFF);
  for (int i = 0; i < len; i++) {
    EEPROM.write(2 + i, js[i]);
  }
  EEPROM.commit();
#endif
}

/**
 * @brief Returns a JSON string containing the saved credentials.
 * 
 * @return String JSON representation of the credentials.
 */
String WiFiManager::getCredentialsJson() const {
  return _loadData();
}

// -------------------- Manage Credentials (FIFO Limit) --------------------
/**
 * @brief Adds or updates a WiFi credential.
 * 
 * @param ssid The SSID of the network.
 * @param password The password for the network.
 */
void WiFiManager::addCredential(const char* ssid, const char* password) {
  String json = _loadData();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  JsonArray arr = err ? doc.to<JsonArray>() : doc.as<JsonArray>();

  bool updated = false;
  for (JsonObject obj : arr) {
    if (strcmp(obj["ssid"] | "", ssid) == 0) { 
      obj["password"] = password;
      updated = true;
      break;
    }
  }
  
  if (!updated) {
    if (arr.size() >= MAX_CREDENTIALS) {
      Serial.printf("[WiFiManager] Limit reached (%d). Removing oldest: %s\n", MAX_CREDENTIALS, arr[0]["ssid"].as<const char*>());
      arr.remove(0); 
    }
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["password"] = password;
  }
  
  String out;
  serializeJson(arr, out);
  _saveData(out);
  Serial.printf("[WiFiManager] %s credential: %s\n", updated ? "Updated" : "Added", ssid);
}

/**
 * @brief Deletes a saved WiFi credential.
 * 
 * @param ssid The SSID of the credential to delete.
 */
void WiFiManager::deleteCredential(const char* ssid) {
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  JsonArray arr = doc.as<JsonArray>();
  for (size_t i = 0; i < arr.size(); ++i) {
    if (strcmp(arr[i]["ssid"] | "", ssid) == 0) {
      arr.remove(i);
      String out;
      serializeJson(arr, out);
      _saveData(out);
      Serial.printf("[WiFiManager] Deleted credential: %s\n", ssid);
      return;
    }
  }
}

/**
 * @brief Clears all saved WiFi credentials.
 */
void WiFiManager::clearCredentials() {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  for(int i=0; i<EEPROM_SIZE; i++) EEPROM.write(i, 0xFF);
  EEPROM.commit();
#endif
  Serial.println("[WiFiManager] All credentials cleared!");
}

/**
 * @brief Prints the list of saved credentials to the Serial monitor.
 */
void WiFiManager::listCredentialsToSerial() const {
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    Serial.println("[WiFiManager] []");
    return;
  }
  Serial.println("[WiFiManager] Stored WiFi:");
  for (JsonObject obj : doc.as<JsonArray>()) {
    Serial.printf(" - %s\n", obj["ssid"].as<const char*>());
  }
}

// -------------------- Smart Connection Logic --------------------
/**
 * @brief Initiates a non-blocking WiFi connection using Smart Scan.
 */
void WiFiManager::connectToWiFi() {
  Serial.println("[WiFiManager] Scanning air to find known networks...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(50);

  WiFi.scanNetworks(true, true);
  _scanStartTime = millis(); 
  _currentState = WIFI_STATE_SCANNING;
}

/**
 * @brief Checks the WiFi scan status and matches against saved credentials.
 */
void WiFiManager::_checkScanStatus() {
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - _scanStartTime >= _scanTimeout) {
      Serial.println("[WiFiManager] Scan timed out! Falling back to AP mode.");
      WiFi.scanDelete();
      _currentState = WIFI_STATE_FAILED;
    }
    return;
  }

  _matchedSSIDs.clear();
  _matchedPasses.clear();

  String json = _loadData();
  JsonDocument doc;
  
  if (!deserializeJson(doc, json)) {
    JsonArray arr = doc.as<JsonArray>();
    
    if (n > 0) {
      struct MatchedNetwork {
        String ssid;
        String pass;
        int32_t rssi;
      };
      std::vector<MatchedNetwork> foundNetworks;

      for (int i = 0; i < n; ++i) {
        String scannedSSID = WiFi.SSID(i);
        int32_t rssi = WiFi.RSSI(i);

        for (JsonObject cred : arr) {
          if (scannedSSID == cred["ssid"].as<String>()) {
            bool exists = false;
            for (auto& fn : foundNetworks) {
              if (fn.ssid == scannedSSID) {
                exists = true;
                if (rssi > fn.rssi) fn.rssi = rssi; 
                break;
              }
            }
            if (!exists) {
              foundNetworks.push_back({scannedSSID, cred["password"].as<String>(), rssi});
            }
          }
        }
      }

      std::sort(foundNetworks.begin(), foundNetworks.end(), [](const MatchedNetwork& a, const MatchedNetwork& b) {
        return a.rssi > b.rssi;
      });

      for (const auto& fn : foundNetworks) {
        _matchedSSIDs.push_back(fn.ssid);
        _matchedPasses.push_back(fn.pass);
        Serial.printf("[WiFiManager] Found Match: %s (RSSI: %d)\n", fn.ssid.c_str(), fn.rssi);
      }
      
    } else {
      for (JsonObject cred : arr) {
        _matchedSSIDs.push_back(cred["ssid"].as<String>());
        _matchedPasses.push_back(cred["password"].as<String>());
      }
    }
  }
  
  WiFi.scanDelete(); 

  if (_matchedSSIDs.empty()) {
    Serial.println("[WiFiManager] No known networks are currently in range.");
    _currentState = WIFI_STATE_FAILED;
    return;
  }

  _currentNetworkIndex = 0;
  _isConnecting = true;
  _startNextConnection();
}

void WiFiManager::_setupEventHandlers() {
#if defined(ESP32)
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info){
    Serial.println("\n[Hardware Event] Wi-Fi Disconnected!");
    this->_currentState = WIFI_STATE_FAILED;
    this->_disconnectTriggered = true;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

#elif defined(ESP8266)
  // Safely storing handler in class member to prevent immediate destruction
  _disconnectHandler = WiFi.onStationModeDisconnected(
    [this](const WiFiEventStationModeDisconnected& event) {
      Serial.println("\n[Hardware Event] Wi-Fi Disconnected!");
      this->_currentState = WIFI_STATE_FAILED;
      this->_disconnectTriggered = true;
    });
#endif
}

/**
 * @brief Attempts connection to the next matched network.
 */
void WiFiManager::_startNextConnection() {
  if (_currentNetworkIndex >= _matchedSSIDs.size()) {
    Serial.println("[WiFiManager] Could not connect to any available network.");
    _currentState = WIFI_STATE_FAILED;
    _isConnecting = false;
    return;
  }

  String ssid = _matchedSSIDs[_currentNetworkIndex];
  String pass = _matchedPasses[_currentNetworkIndex];

  Serial.printf("[WiFiManager] Attempting Connection to: %s\n", ssid.c_str());
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  
  _currentState = WIFI_STATE_CONNECTING;
  _startAttemptTime = millis();
}

/**
 * @brief Monitors the ongoing connection attempt.
 */
void WiFiManager::_checkConnectionStatus() {
  if (!_isConnecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    _currentState = WIFI_STATE_CONNECTED;
    _isConnecting = false;
    Serial.println("\n[WiFiManager] Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    return;
  }

  if (millis() - _startAttemptTime >= _connectionTimeout) {
    Serial.println("\n[WiFiManager] Failed. Trying next...");
    _currentNetworkIndex++; 
    _startNextConnection();
  }
}

/**
 * @brief Gets the current state of the WiFi manager.
 * 
 * @return WiFiState The current WiFi connection state.
 */
WiFiState WiFiManager::getState() const {
  return _currentState;
}

// -------------------- Core Loop --------------------
/**
 * @brief Main processing loop. Must be called frequently in `loop()`.
 */
void WiFiManager::process() {
  if (_disconnectTriggered) {
    _disconnectTriggered = false; 
    Serial.println("[WiFiManager] Re-evaluating connection due to drop...");
    connectToWiFi(); 
  }

  if (_currentState == WIFI_STATE_SCANNING) {
    _checkScanStatus();
  } else if (_currentState == WIFI_STATE_CONNECTING) {
    _checkConnectionStatus();
  }

  _dnsServer.processNextRequest();
#ifndef WIFIMANAGER_USE_ASYNC_WEBSERVER
  if (_server) {
    _server->handleClient();
  }
#endif
}

// -------------------- URL Decode --------------------
/**
 * @brief Decodes a URL-encoded string.
 * 
 * @param str The URL-encoded string.
 * @return String The decoded string.
 */
String WiFiManager::_urlDecode(const String& str) {
  String out;
  out.reserve(str.length());
  for (size_t i = 0; i < str.length(); ++i) {
    char c = str[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < str.length()) {
      char hex[3] = {str[i + 1], str[i + 2], 0};
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

// -------------------- Web Handlers & Setup --------------------
/**
 * @brief Sets the web server instance for routing.
 * 
 * @param server Pointer to the web server instance.
 */
void WiFiManager::setServer(ESP_WebServer* server) {
  _server = server;
}

/**
 * @brief Starts the Access Point mode and configures the web server routes.
 * 
 * @param server Reference to the web server instance.
 */
void WiFiManager::startAPMode(ESP_WebServer& server) {
  _server = &server;
  _currentState = WIFI_STATE_AP_MODE;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ap_ssid, _ap_password);
  Serial.println("[WiFiManager] AP Mode started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  _dnsServer.start(53, "*", WiFi.softAPIP());

#ifdef WIFIMANAGER_USE_ASYNC_WEBSERVER
  _server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) { _handleRoot(request); });
  _server->on("/scan", HTTP_GET, [this](AsyncWebServerRequest *request) { _handleScan(request); });
  _server->on("/list", HTTP_GET, [this](AsyncWebServerRequest *request) { _handleList(request); });
  _server->on("/delete", HTTP_ANY, [this](AsyncWebServerRequest *request) { _handleDelete(request); });
  _server->on("/save", HTTP_POST, [this](AsyncWebServerRequest *request) { _handleSave(request); });
  
  _server->onNotFound([this](AsyncWebServerRequest *request) { _handleRoot(request); });
#else
  _server->on("/", std::bind(&WiFiManager::_handleRoot, this));
  _server->on("/scan", std::bind(&WiFiManager::_handleScan, this));
  _server->on("/list", std::bind(&WiFiManager::_handleList, this));
  _server->on("/delete", std::bind(&WiFiManager::_handleDelete, this));
  _server->on("/save", HTTP_POST, std::bind(&WiFiManager::_handleSave, this));
  
  _server->onNotFound([this]() { _handleRoot(); });
#endif
  _server->begin();
}

#ifdef WIFIMANAGER_USE_ASYNC_WEBSERVER
void WiFiManager::_handleRoot(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", page_index, page_index_len);
  response->addHeader("Content-Encoding", "gzip");
  request->send(response);
}

void WiFiManager::_handleScan(AsyncWebServerRequest *request) {
  int n = WiFi.scanNetworks(false, true); 
  if (n == WIFI_SCAN_FAILED || n <= 0) {
    request->send(200, "application/json", "[]");
    return;
  }

  JsonDocument doc;
  for (int i = 0; i < n; ++i) {
    JsonObject obj = doc.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
  
  WiFi.scanDelete();
}

void WiFiManager::_handleSave(AsyncWebServerRequest *request) {
  String ssid, password;
  if (request->hasParam("ssid", true)) {
    ssid = request->getParam("ssid", true)->value();
  } else if (request->hasParam("ssid")) {
    ssid = request->getParam("ssid")->value();
  }
  
  if (request->hasParam("password", true)) {
    password = request->getParam("password", true)->value();
  } else if (request->hasParam("password")) {
    password = request->getParam("password")->value();
  }

  if (ssid.length() == 0) {
    request->send(400, "text/html", "<p>SSID cannot be empty!</p>");
    return;
  }

  addCredential(ssid.c_str(), password.c_str());
  request->send(200, "text/html", "<p>Saved. Rebooting ESP...</p>");
  delay(500); 
  ESP.restart();
}

void WiFiManager::_handleList(AsyncWebServerRequest *request) {
  String json = _loadData();
  JsonDocument doc;
  JsonDocument out;
  JsonArray arrOut = out.to<JsonArray>();

  if (!deserializeJson(doc, json)) {
    for (JsonObject obj : doc.as<JsonArray>()) {
      JsonObject o = arrOut.add<JsonObject>();
      o["ssid"] = obj["ssid"];
    }
  }
  String body;
  serializeJson(arrOut, body);
  request->send(200, "application/json", body);
}

void WiFiManager::_handleDelete(AsyncWebServerRequest *request) {
  String ssid;
  if (request->hasParam("ssid", true)) {
    ssid = request->getParam("ssid", true)->value();
  } else if (request->hasParam("ssid")) {
    ssid = request->getParam("ssid")->value();
  } else {
    request->send(400, "text/plain", "SSID missing");
    return;
  }
  deleteCredential(ssid.c_str());
  request->send(200, "text/plain", "Deleted");
}
#else
/**
 * @brief Handles the root web server route.
 */
void WiFiManager::_handleRoot() {
  _server->sendHeader(F("Content-Encoding"), F("gzip"));
  _server->send_P(200, "text/html", (const char*)page_index, page_index_len);
}

/**
 * @brief Handles the WiFi scan request route.
 */
void WiFiManager::_handleScan() {
  int n = WiFi.scanNetworks(false, true); 
  if (n == WIFI_SCAN_FAILED || n <= 0) {
    _server->send(200, "application/json", "[]");
    return;
  }

  JsonDocument doc;
  for (int i = 0; i < n; ++i) {
    JsonObject obj = doc.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
    obj["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  String json;
  serializeJson(doc, json);
  _server->send(200, "application/json", json);
  
  WiFi.scanDelete();
}

/**
 * @brief Handles the credential save request route.
 */
void WiFiManager::_handleSave() {
  String ssid = _urlDecode(_server->arg("ssid"));
  String password = _urlDecode(_server->arg("password"));

  if (ssid.length() == 0) {
    _server->send(400, "text/html", "<p>SSID cannot be empty!</p>");
    return;
  }

  addCredential(ssid.c_str(), password.c_str());
  _server->send(200, "text/html", "<p>Saved. Rebooting ESP...</p>");
  delay(500); 
  ESP.restart();
}

/**
 * @brief Handles the credential list request route.
 */
void WiFiManager::_handleList() {
  String json = _loadData();
  JsonDocument doc;
  JsonDocument out;
  JsonArray arrOut = out.to<JsonArray>();

  if (!deserializeJson(doc, json)) {
    for (JsonObject obj : doc.as<JsonArray>()) {
      JsonObject o = arrOut.add<JsonObject>();
      o["ssid"] = obj["ssid"];
    }
  }
  String body;
  serializeJson(arrOut, body);
  _server->send(200, "application/json", body);
}

/**
 * @brief Handles the credential delete request route.
 */
void WiFiManager::_handleDelete() {
  if (!_server->hasArg("ssid")) {
    _server->send(400, "text/plain", "SSID missing");
    return;
  }
  deleteCredential(_urlDecode(_server->arg("ssid")).c_str());
  _server->send(200, "text/plain", "Deleted");
}
#endif

// -------------------- Serial Commands --------------------
/**
 * @brief Splits a command line string into parts, respecting quotes.
 * 
 * @param line The command line string.
 * @param outParts Array to hold the resulting parts.
 * @param maxParts Maximum number of parts to split into.
 * @return int The number of parts successfully separated.
 */
int WiFiManager::_splitArgsQuoted(const String& line, String outParts[], int maxParts) {
  bool inQuotes = false;
  String cur;
  int count = 0;
  for (size_t i = 0; i < line.length(); ++i) {
    char c = line[i];
    if (c == '"') { inQuotes = !inQuotes; continue; }
    if (!inQuotes && isspace(c)) {
      if (cur.length()) { if (count < maxParts) outParts[count++] = cur; cur = ""; }
    } else { cur += c; }
  }
  if (cur.length() && count < maxParts) outParts[count++] = cur;
  return count;
}

/**
 * @brief Processes serial commands for managing the WiFiManager.
 * 
 * @param cmdLine The command line string.
 * @param io Stream object to read from and write to (default is Serial).
 */
void WiFiManager::executeCommand(String cmdLine, Stream& io) {
  cmdLine.trim();
  if (!cmdLine.length()) return;

  const int MAXP = 4;
  String p[MAXP];
  int n = _splitArgsQuoted(cmdLine, p, MAXP);
  if (n == 0) return;

  String cmd = p[0];
  cmd.toUpperCase();

  if (cmd == "ADD" && n >= 2) {
    addCredential(p[1].c_str(), (n >= 3) ? p[2].c_str() : "");
  } else if ((cmd == "DEL" || cmd == "DELETE") && n >= 2) {
    deleteCredential(p[1].c_str());
  } else if (cmd == "CLEAR") {
    clearCredentials();
  } else if (cmd == "LIST") {
    listCredentialsToSerial();
  } else if (cmd == "STATUS") {
    io.printf("State: %d\n", _currentState);
    if(_currentState == WIFI_STATE_CONNECTED) io.printf("IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    _printHelp();
  }
}

/**
 * @brief Prints the available serial commands.
 */
void WiFiManager::_printHelp() {
  Serial.println("[WiFiManager] Commands: ADD \"SSID\" \"PASS\", DEL \"SSID\", LIST, CLEAR, STATUS");
}

/**
 * @brief Initializes the WiFiManager, loading saved credentials.
 */
void WiFiManager::begin() {
  _setupEventHandlers();
  _printHelp();
}


// #include "ESPWiFiManager.h"
// #include "page_index.h"

// // -------------------- Constructor --------------------
// WiFiManager::WiFiManager(const char* ap_ssid, const char* ap_password)
// : _ap_ssid(ap_ssid), _ap_password(ap_password) {}

// // -------------------- Storage Helpers (ESP32 & ESP8266) --------------------
// String WiFiManager::_loadData() const {
// #if defined(ESP32)
//   _prefs.begin(NVS_NAMESPACE, true);
//   String json = _prefs.getString(NVS_KEY_MULTI, "[]");
//   _prefs.end();
//   return json;
// #elif defined(ESP8266)
//   EEPROM.begin(EEPROM_SIZE);
//   int len = EEPROM.read(0) | (EEPROM.read(1) << 8); 
//   if (len == 0 || len > EEPROM_SIZE - 2 || len == 0xFFFF) {
//     return "[]";
//   }
//   String json;
//   json.reserve(len);
//   for (int i = 0; i < len; i++) {
//     json += (char)EEPROM.read(2 + i);
//   }
//   return json;
// #endif
// }

// void WiFiManager::_saveData(const String& js) {
// #if defined(ESP32)
//   _prefs.begin(NVS_NAMESPACE, false);
//   _prefs.putString(NVS_KEY_MULTI, js);
//   _prefs.end();
// #elif defined(ESP8266)
//   EEPROM.begin(EEPROM_SIZE);
//   int len = js.length();
//   EEPROM.write(0, len & 0xFF);
//   EEPROM.write(1, (len >> 8) & 0xFF);
//   for (int i = 0; i < len; i++) {
//     EEPROM.write(2 + i, js[i]);
//   }
//   EEPROM.commit();
// #endif
// }

// String WiFiManager::getCredentialsJson() const {
//   return _loadData();
// }

// // -------------------- Manage Credentials (FIFO Limit) --------------------
// void WiFiManager::addCredential(const char* ssid, const char* password) {
//   // Load existing credentials from storage
//   String json = _loadData();
//   JsonDocument doc;
//   DeserializationError err = deserializeJson(doc, json);
//   JsonArray arr = err ? doc.to<JsonArray>() : doc.as<JsonArray>();

//   // Check if the SSID already exists to update its password
//   bool updated = false;
//   for (JsonObject obj : arr) {
//     if (strcmp(obj["ssid"] | "", ssid) == 0) { 
//       obj["password"] = password;
//       updated = true;
//       break;
//     }
//   }
  
//   // If credential is new, add it
//   if (!updated) {
//     // FIFO Logic: Remove oldest if limit reached to save memory
//     if (arr.size() >= MAX_CREDENTIALS) {
//       Serial.printf("[WiFiManager] Limit reached (%d). Removing oldest: %s\n", MAX_CREDENTIALS, arr[0]["ssid"].as<const char*>());
//       arr.remove(0); 
//     }
//     JsonObject o = arr.add<JsonObject>();
//     o["ssid"] = ssid;
//     o["password"] = password;
//   }
  
//   // Serialize back to JSON and save to persistent storage
//   String out;
//   serializeJson(arr, out);
//   _saveData(out);
//   Serial.printf("[WiFiManager] %s credential: %s\n", updated ? "Updated" : "Added", ssid);
// }

// void WiFiManager::deleteCredential(const char* ssid) {
//   String json = _loadData();
//   JsonDocument doc;
//   if (deserializeJson(doc, json)) return;

//   JsonArray arr = doc.as<JsonArray>();
//   for (size_t i = 0; i < arr.size(); ++i) {
//     if (strcmp(arr[i]["ssid"] | "", ssid) == 0) {
//       arr.remove(i);
//       String out;
//       serializeJson(arr, out);
//       _saveData(out);
//       Serial.printf("[WiFiManager] Deleted credential: %s\n", ssid);
//       return;
//     }
//   }
// }

// void WiFiManager::clearCredentials() {
// #if defined(ESP32)
//   _prefs.begin(NVS_NAMESPACE, false);
//   _prefs.clear();
//   _prefs.end();
// #elif defined(ESP8266)
//   EEPROM.begin(EEPROM_SIZE);
//   for(int i=0; i<EEPROM_SIZE; i++) EEPROM.write(i, 0xFF);
//   EEPROM.commit();
// #endif
//   Serial.println("[WiFiManager] All credentials cleared!");
// }

// void WiFiManager::listCredentialsToSerial() const {
//   String json = _loadData();
//   JsonDocument doc;
//   if (deserializeJson(doc, json)) {
//     Serial.println("[WiFiManager] []");
//     return;
//   }
//   Serial.println("[WiFiManager] Stored WiFi:");
//   for (JsonObject obj : doc.as<JsonArray>()) {
//     Serial.printf(" - %s\n", obj["ssid"].as<const char*>());
//   }
// }

// // -------------------- Smart Connection Logic --------------------
// void WiFiManager::connectToWiFi() {
//   Serial.println("[WiFiManager] Scanning air to find known networks...");

//   // ---> setup STA mode before scanning
//   WiFi.mode(WIFI_STA);
//   WiFi.disconnect(true);
//   delay(50);

//   // Start an async/non-blocking scan of nearby networks
//   WiFi.scanNetworks(true, true);
//   _scanStartTime = millis(); // Track when scan began for timeout detection
//   _currentState = WIFI_STATE_SCANNING;
// }

// void WiFiManager::_checkScanStatus() {
//   int n = WiFi.scanComplete();

//   // Wait until scan is done — but abort if it takes too long
//   if (n == WIFI_SCAN_RUNNING) {
//     if (millis() - _scanStartTime >= _scanTimeout) {
//       Serial.println("[WiFiManager] Scan timed out! Falling back to AP mode.");
//       WiFi.scanDelete();
//       _currentState = WIFI_STATE_FAILED;
//     }
//     return;
//   }

//   _matchedSSIDs.clear();
//   _matchedPasses.clear();

//   String json = _loadData();
//   JsonDocument doc;
  
//   if (!deserializeJson(doc, json)) {
//     JsonArray arr = doc.as<JsonArray>();
    
//     // If networks were found during scan
//     if (n > 0) {
//       struct MatchedNetwork {
//         String ssid;
//         String pass;
//         int32_t rssi;
//       };
//       // Temporary list to hold networks that match our saved credentials
//       std::vector<MatchedNetwork> foundNetworks;

//       for (int i = 0; i < n; ++i) {
//         String scannedSSID = WiFi.SSID(i);
//         int32_t rssi = WiFi.RSSI(i);

//         // Check if scanned SSID is in our saved credentials
//         for (JsonObject cred : arr) {
//           if (scannedSSID == cred["ssid"].as<String>()) {
//             bool exists = false;
//             for (auto& fn : foundNetworks) {
//               if (fn.ssid == scannedSSID) {
//                 exists = true;
//                 if (rssi > fn.rssi) fn.rssi = rssi; // Update to strongest Mesh node
//                 break;
//               }
//             }
//             if (!exists) {
//               foundNetworks.push_back({scannedSSID, cred["password"].as<String>(), rssi});
//             }
//           }
//         }
//       }

//       // Sort found networks by strongest signal (highest RSSI)
//       std::sort(foundNetworks.begin(), foundNetworks.end(), [](const MatchedNetwork& a, const MatchedNetwork& b) {
//         return a.rssi > b.rssi;
//       });

//       // Populate our connection queue with sorted networks
//       for (const auto& fn : foundNetworks) {
//         _matchedSSIDs.push_back(fn.ssid);
//         _matchedPasses.push_back(fn.pass);
//         Serial.printf("[WiFiManager] Found Match: %s (RSSI: %d)\n", fn.ssid.c_str(), fn.rssi);
//       }
      
//     } else {
//       // Fallback: If scan fails for some reason, blindly try all saved networks
//       for (JsonObject cred : arr) {
//         _matchedSSIDs.push_back(cred["ssid"].as<String>());
//         _matchedPasses.push_back(cred["password"].as<String>());
//       }
//     }
//   }
  
//   // Clean up scan results from memory
//   WiFi.scanDelete(); 

//   // If no saved networks match current environment, abort connection
//   if (_matchedSSIDs.empty()) {
//     Serial.println("[WiFiManager] No known networks are currently in range.");
//     _currentState = WIFI_STATE_FAILED;
//     return;
//   }

//   // Begin connecting to the best matched network
//   _currentNetworkIndex = 0;
//   _isConnecting = true;
//   _startNextConnection();
// }

// void WiFiManager::_setupEventHandlers() {
//   #if defined(ESP32)
//     WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info){
//       Serial.println("\n[Hardware Event] Wi-Fi Disconnected!");
//       this->_currentState = WIFI_STATE_FAILED;
//       this->_disconnectTriggered = true;
//     }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

//   #elif defined(ESP8266)
//     static WiFiEventHandler disconnectHandler = WiFi.onStationModeDisconnected(
//     [this](const WiFiEventStationModeDisconnected& event) {
//       Serial.println("\n[Hardware Event] Wi-Fi Disconnected!");
//       this->_currentState = WIFI_STATE_FAILED;
//       this->_disconnectTriggered = true;
//     });
//   #endif
// }

// void WiFiManager::_startNextConnection() {
//   // If we have tried all matched networks, we fail
//   if (_currentNetworkIndex >= _matchedSSIDs.size()) {
//     Serial.println("[WiFiManager] Could not connect to any available network.");
//     _currentState = WIFI_STATE_FAILED;
//     _isConnecting = false;
//     return;
//   }

//   String ssid = _matchedSSIDs[_currentNetworkIndex];
//   String pass = _matchedPasses[_currentNetworkIndex];

//   Serial.printf("[WiFiManager] Attempting Connection to: %s\n", ssid.c_str());
  
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(ssid.c_str(), pass.c_str());
  
//   _currentState = WIFI_STATE_CONNECTING;
//   _startAttemptTime = millis();
// }

// void WiFiManager::_checkConnectionStatus() {
//   if (!_isConnecting) return;

//   // Connection successful
//   if (WiFi.status() == WL_CONNECTED) {
//     _currentState = WIFI_STATE_CONNECTED;
//     _isConnecting = false;
//     Serial.println("\n[WiFiManager] Connected!");
//     Serial.print("IP: ");
//     Serial.println(WiFi.localIP());
//     return;
//   }

//   // Check timeout without blocking execution
//   if (millis() - _startAttemptTime >= _connectionTimeout) {
//     Serial.println("\n[WiFiManager] Failed. Trying next...");
//     _currentNetworkIndex++; // Move to the next network in the queue
//     _startNextConnection();
//   }
// }

// WiFiState WiFiManager::getState() const {
//   return _currentState;
// }

// // -------------------- Core Loop --------------------
// void WiFiManager::process() {
//   // new logic
//   if (_disconnectTriggered) {
//     _disconnectTriggered = false; // Reset flag
//     Serial.println("[WiFiManager] Re-evaluating connection...");
//     connectToWiFi(); // Punoray connect korar process suru kora
//   }

//   // Manage state transitions continuously
//   if (_currentState == WIFI_STATE_SCANNING) {
//     _checkScanStatus();
//   } else if (_currentState == WIFI_STATE_CONNECTING) {
//     _checkConnectionStatus();
//   }

//   // Process DNS and Web requests (useful mostly in AP mode)
//   _dnsServer.processNextRequest();
//   if (_server) {
//     _server->handleClient();
//   }
// }

// // -------------------- URL Decode --------------------
// String WiFiManager::_urlDecode(const String& str) {
//   // Decode strings received from HTTP endpoints (e.g. form submissions)
//   String out;
//   out.reserve(str.length());
//   for (size_t i = 0; i < str.length(); ++i) {
//     char c = str[i];
//     if (c == '+') {
//       out += ' ';
//     } else if (c == '%' && i + 2 < str.length()) {
//       char hex[3] = {str[i + 1], str[i + 2], 0};
//       out += (char)strtol(hex, nullptr, 16);
//       i += 2;
//     } else {
//       out += c;
//     }
//   }
//   return out;
// }

// // -------------------- Web Handlers & Setup --------------------
// void WiFiManager::setServer(ESP_WebServer* server) {
//   _server = server;
// }

// void WiFiManager::startAPMode(ESP_WebServer& server) {
//   _server = &server;
//   _currentState = WIFI_STATE_AP_MODE;

//   // Configure ESP as an Access Point
//   WiFi.mode(WIFI_AP);
//   WiFi.softAP(_ap_ssid, _ap_password);
//   Serial.println("[WiFiManager] AP Mode started");
//   Serial.print("AP IP: ");
//   Serial.println(WiFi.softAPIP());

//   // Start captive portal DNS server
//   _dnsServer.start(53, "*", WiFi.softAPIP());

//   // Register HTTP endpoint callbacks
//   _server->on("/", std::bind(&WiFiManager::_handleRoot, this));
//   _server->on("/scan", std::bind(&WiFiManager::_handleScan, this));
//   _server->on("/list", std::bind(&WiFiManager::_handleList, this));
//   _server->on("/delete", std::bind(&WiFiManager::_handleDelete, this));
//   _server->on("/save", HTTP_POST, std::bind(&WiFiManager::_handleSave, this));
  
//   _server->onNotFound([this]() { _handleRoot(); });
//   _server->begin();
// }

// void WiFiManager::_handleRoot() {
//   // Serve the compressed HTML page from page_index.h
//   _server->sendHeader(F("Content-Encoding"), F("gzip"));
//   _server->send_P(200, "text/html", (const char*)page_index, page_index_len);
// }

// void WiFiManager::_handleScan() {
//   // Synchronous scan for providing results via web UI safely
//   int n = WiFi.scanNetworks(false, true); 
//   if (n == WIFI_SCAN_FAILED || n <= 0) {
//     _server->send(200, "application/json", "[]");
//     return;
//   }

//   JsonDocument doc;
//   for (int i = 0; i < n; ++i) {
//     JsonObject obj = doc.add<JsonObject>();
//     obj["ssid"] = WiFi.SSID(i);
//     obj["rssi"] = WiFi.RSSI(i);
//     obj["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
//   }
//   String json;
//   serializeJson(doc, json);
//   _server->send(200, "application/json", json);
  
//   // Clean up memory
//   WiFi.scanDelete();
// }

// void WiFiManager::_handleSave() {
//   // Process the save request containing new credentials
//   String ssid = _urlDecode(_server->arg("ssid"));
//   String password = _urlDecode(_server->arg("password"));

//   if (ssid.length() == 0) {
//     _server->send(400, "text/html", "<p>SSID cannot be empty!</p>");
//     return;
//   }

//   // Save the requested credential and restart device
//   addCredential(ssid.c_str(), password.c_str());
//   _server->send(200, "text/html", "<p>Saved. Rebooting ESP...</p>");
//   delay(500); // Give enough time for the ESP to send response
//   ESP.restart();
// }

// void WiFiManager::_handleList() {
//   String json = _loadData();
//   JsonDocument doc;
//   JsonDocument out;
//   JsonArray arrOut = out.to<JsonArray>();

//   // Send the list of saved SSIDs (without passwords)
//   if (!deserializeJson(doc, json)) {
//     for (JsonObject obj : doc.as<JsonArray>()) {
//       JsonObject o = arrOut.add<JsonObject>();
//       o["ssid"] = obj["ssid"];
//     }
//   }
//   String body;
//   serializeJson(arrOut, body);
//   _server->send(200, "application/json", body);
// }

// void WiFiManager::_handleDelete() {
//   if (!_server->hasArg("ssid")) {
//     _server->send(400, "text/plain", "SSID missing");
//     return;
//   }
//   deleteCredential(_urlDecode(_server->arg("ssid")).c_str());
//   _server->send(200, "text/plain", "Deleted");
// }

// // -------------------- Serial Commands --------------------
// int WiFiManager::_splitArgsQuoted(const String& line, String outParts[], int maxParts) {
//   // Split serial commands ignoring spaces inside double quotes
//   bool inQuotes = false;
//   String cur;
//   int count = 0;
//   for (size_t i = 0; i < line.length(); ++i) {
//     char c = line[i];
//     if (c == '"') { inQuotes = !inQuotes; continue; }
//     if (!inQuotes && isspace(c)) {
//       if (cur.length()) { if (count < maxParts) outParts[count++] = cur; cur = ""; }
//     } else { cur += c; }
//   }
//   if (cur.length() && count < maxParts) outParts[count++] = cur;
//   return count;
// }

// void WiFiManager::executeCommand(String cmdLine, Stream& io) {
//   cmdLine.trim();
//   if (!cmdLine.length()) return;

//   const int MAXP = 4;
//   String p[MAXP];
//   int n = _splitArgsQuoted(cmdLine, p, MAXP);
//   if (n == 0) return;

//   String cmd = p[0];
//   cmd.toUpperCase();

//   if (cmd == "ADD" && n >= 2) {
//     addCredential(p[1].c_str(), (n >= 3) ? p[2].c_str() : "");
//   } else if ((cmd == "DEL" || cmd == "DELETE") && n >= 2) {
//     deleteCredential(p[1].c_str());
//   } else if (cmd == "CLEAR") {
//     clearCredentials();
//   } else if (cmd == "LIST") {
//     listCredentialsToSerial();
//   } else if (cmd == "STATUS") {
//     io.printf("State: %d\n", _currentState);
//     if(_currentState == WIFI_STATE_CONNECTED) io.printf("IP: %s\n", WiFi.localIP().toString().c_str());
//   } else {
//     _printHelp();
//   }
// }

// void WiFiManager::_printHelp() {
//   Serial.println("[WiFiManager] Commands: ADD \"SSID\" \"PASS\", DEL \"SSID\", LIST, CLEAR, STATUS");
// }

// void WiFiManager::begin() {
//   _setupEventHandlers();
//   _printHelp();
// }