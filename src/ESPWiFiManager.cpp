#include "ESPWiFiManager.h"
#include "page_index.h"

// -------------------- Constructor --------------------
WiFiManager::WiFiManager(const char* ap_ssid, const char* ap_password)
: _ap_ssid(ap_ssid), _ap_password(ap_password) {}

// -------------------- Storage Helpers (ESP32 & ESP8266) --------------------
String WiFiManager::_loadData() const {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, true);
  String json = _prefs.getString(NVS_KEY_MULTI, "[]");
  _prefs.end();
  return json;
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  int len = EEPROM.read(0) | (EEPROM.read(1) << 8); // Read length
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

String WiFiManager::getCredentialsJson() const {
  return _loadData();
}

// -------------------- Manage Credentials --------------------
void WiFiManager::addCredential(const char* ssid, const char* password) {
  String json = _loadData();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  JsonArray arr = err ? doc.to<JsonArray>() : doc.as<JsonArray>();

  bool updated = false;
  for (JsonObject obj : arr) {
    if (strcmp(obj["ssid"] | "", ssid) == 0) { // Optimized string compare
      obj["password"] = password;
      updated = true;
      break;
    }
  }
  
  if (!updated) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["password"] = password;
  }
  
  String out;
  serializeJson(arr, out);
  _saveData(out);
  Serial.printf("[WiFiManager] %s credential: %s\n", updated ? "Updated" : "Added", ssid);
}

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

// -------------------- Event-Driven / Non-Blocking Connect --------------------
void WiFiManager::connectToWiFi() {
  _currentNetworkIndex = 0;
  _isConnecting = true;
  _startNextConnection();
}

void WiFiManager::_startNextConnection() {
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json) || doc.as<JsonArray>().size() == 0) {
    Serial.println("[WiFiManager] No saved credentials.");
    _currentState = WIFI_STATE_FAILED;
    _isConnecting = false;
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  if (_currentNetworkIndex >= arr.size()) {
    Serial.println("[WiFiManager] Could not connect to any saved network.");
    _currentState = WIFI_STATE_FAILED;
    _isConnecting = false;
    return;
  }

  JsonObject cred = arr[_currentNetworkIndex];
  const char* ssid = cred["ssid"];
  const char* pass = cred["password"];

  Serial.printf("[WiFiManager] Trying SSID: %s\n", ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  
  _currentState = WIFI_STATE_CONNECTING;
  _startAttemptTime = millis();
}

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

  // Check timeout without blocking
  if (millis() - _startAttemptTime >= _connectionTimeout) {
    Serial.println("\n[WiFiManager] Failed. Trying next...");
    _currentNetworkIndex++;
    _startNextConnection();
  }
}

WiFiState WiFiManager::getState() const {
  return _currentState;
}

// -------------------- Core Loop --------------------
void WiFiManager::process() {
  // Handle async connections without blocking
  if (_currentState == WIFI_STATE_CONNECTING) {
    _checkConnectionStatus();
  }

  _dnsServer.processNextRequest();
  if (_server) {
    _server->handleClient();
  }
}

// -------------------- URL Decode (Memory Optimized) --------------------
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
void WiFiManager::setServer(ESP_WebServer* server) {
  _server = server;
}

void WiFiManager::startAPMode(ESP_WebServer& server) {
  _server = &server;
  _currentState = WIFI_STATE_AP_MODE;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ap_ssid, _ap_password);
  Serial.println("[WiFiManager] AP Mode started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  _dnsServer.start(53, "*", WiFi.softAPIP());

  _server->on("/", std::bind(&WiFiManager::_handleRoot, this));
  _server->on("/scan", std::bind(&WiFiManager::_handleScan, this));
  _server->on("/list", std::bind(&WiFiManager::_handleList, this));
  _server->on("/delete", std::bind(&WiFiManager::_handleDelete, this));
  _server->on("/save", HTTP_POST, std::bind(&WiFiManager::_handleSave, this));
  
  _server->onNotFound([this]() { _handleRoot(); });
  _server->begin();
}

void WiFiManager::_handleRoot() {
  _server->sendHeader(F("Content-Encoding"), F("gzip"));
  _server->send_P(200, "text/html", (const char*)page_index, page_index_len);
}

void WiFiManager::_handleScan() {
  int n = WiFi.scanNetworks(false, true); // Sync scan for web handler safety
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

void WiFiManager::_handleDelete() {
  if (!_server->hasArg("ssid")) {
    _server->send(400, "text/plain", "SSID missing");
    return;
  }
  deleteCredential(_urlDecode(_server->arg("ssid")).c_str());
  _server->send(200, "text/plain", "Deleted");
}

// -------------------- Serial Commands --------------------
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

void WiFiManager::handleSerialCommands(Stream& io) {
  if (!io.available()) return;
  String line = io.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;

  const int MAXP = 4;
  String p[MAXP];
  int n = _splitArgsQuoted(line, p, MAXP);
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

void WiFiManager::_printHelp() {
  Serial.println("[WiFiManager] Commands: ADD \"SSID\" \"PASS\", DEL \"SSID\", LIST, CLEAR, STATUS");
}

void WiFiManager::begin() {
  _printHelp();
}