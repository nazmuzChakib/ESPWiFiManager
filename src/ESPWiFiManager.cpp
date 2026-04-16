/**
 * @file ESPWiFiManager.cpp
 * @brief Implementation of WiFiManager.
 *
 * ESPWiFiManagerConfig.h is pulled in transitively through ESPWiFiManager.h,
 * so WIFIMANAGER_USE_ASYNC_WEBSERVER is always in scope here — no manual
 * re-inclusion required.
 */

#include "ESPWiFiManager.h"

// ── Constructor ───────────────────────────────────────────────────────────
WiFiManager::WiFiManager(const char* ap_ssid, const char* ap_password)
  : _ap_ssid(ap_ssid), _ap_password(ap_password) {}

// ══════════════════════════════════════════════════════════════════════════
//  PERSISTENT STORAGE
// ══════════════════════════════════════════════════════════════════════════

String WiFiManager::_loadData() const {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  String json = _prefs.getString(NVS_KEY_MULTI, "[]");
  _prefs.end();
  return json;
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  int len = (int)EEPROM.read(0) | ((int)EEPROM.read(1) << 8);
  if (len <= 0 || len > EEPROM_SIZE - 2 || len == 0xFFFF) return "[]";
  String json;
  json.reserve(len);
  for (int i = 0; i < len; i++) json += (char)EEPROM.read(2 + i);
  return json;
#endif
}

void WiFiManager::_saveData(const String& js) {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  _prefs.putString(NVS_KEY_MULTI, js);
  _prefs.end();
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  int len = js.length();
  EEPROM.write(0, (uint8_t)(len & 0xFF));
  EEPROM.write(1, (uint8_t)((len >> 8) & 0xFF));
  for (int i = 0; i < len; i++) EEPROM.write(2 + i, (uint8_t)js[i]);
  EEPROM.commit();
#endif
}

String WiFiManager::getCredentialsJson() const { return _loadData(); }

// ══════════════════════════════════════════════════════════════════════════
//  CREDENTIAL MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::addCredential(const char* ssid, const char* password) {
  String json = _loadData();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  JsonArray arr = err ? doc.to<JsonArray>() : doc.as<JsonArray>();

  // Update password if SSID already exists
  for (JsonObject obj : arr) {
    if (strcmp(obj["ssid"] | "", ssid) == 0) {
      obj["password"] = password;
      String out; serializeJson(arr, out); _saveData(out);
      Serial.printf("[WiFiManager] Updated credential: %s\n", ssid);
      return;
    }
  }

  // Enforce FIFO capacity limit
  if (arr.size() >= MAX_CREDS) {
    Serial.printf("[WiFiManager] Limit (%u) reached, removing oldest: %s\n",
                  (unsigned)MAX_CREDS, arr[0]["ssid"].as<const char*>());
    arr.remove(0);
  }

  JsonObject o = arr.add<JsonObject>();
  o["ssid"]     = ssid;
  o["password"] = password;
  String out; serializeJson(arr, out); _saveData(out);
  Serial.printf("[WiFiManager] Added credential: %s\n", ssid);
}

void WiFiManager::deleteCredential(const char* ssid) {
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  JsonArray arr = doc.as<JsonArray>();
  for (size_t i = 0; i < arr.size(); ++i) {
    if (strcmp(arr[i]["ssid"] | "", ssid) == 0) {
      arr.remove(i);
      String out; serializeJson(arr, out); _saveData(out);
      Serial.printf("[WiFiManager] Deleted credential: %s\n", ssid);
      return;
    }
  }
  Serial.printf("[WiFiManager] Credential not found: %s\n", ssid);
}

void WiFiManager::clearCredentials() {
#if defined(ESP32)
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
#elif defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
  // Write a zero-length header — faster than wiping the whole 1 KB
  EEPROM.write(0, 0);
  EEPROM.write(1, 0);
  EEPROM.commit();
#endif
  Serial.println(F("[WiFiManager] All credentials cleared."));
}

void WiFiManager::listCredentialsToSerial() const {
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json) || doc.as<JsonArray>().size() == 0) {
    Serial.println(F("[WiFiManager] No credentials stored."));
    return;
  }
  Serial.println(F("[WiFiManager] Stored networks:"));
  for (JsonObject obj : doc.as<JsonArray>())
    Serial.printf("  - %s\n", obj["ssid"].as<const char*>());
}

// ══════════════════════════════════════════════════════════════════════════
//  INITIALISATION & EVENT HANDLERS
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::begin() {
  _setupEventHandlers();
  _printHelp();
}

void WiFiManager::_setupEventHandlers() {
#if defined(ESP32)
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t /*info*/) {
    if (_apModeActive) return; // Never reconnect while serving the captive portal
    Serial.println(F("\n[WiFiManager] Disconnected (hardware event)."));
    _currentState        = WIFI_STATE_FAILED;
    _disconnectTriggered = true;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

#elif defined(ESP8266)
  // Handler stored in member — a local variable would unsubscribe on destruction
  _disconnectHandler = WiFi.onStationModeDisconnected(
    [this](const WiFiEventStationModeDisconnected& /*e*/) {
      if (_apModeActive) return;
      Serial.println(F("\n[WiFiManager] Disconnected (hardware event)."));
      _currentState        = WIFI_STATE_FAILED;
      _disconnectTriggered = true;
    });
#endif
}

// ══════════════════════════════════════════════════════════════════════════
//  SMART CONNECT
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::connectToWiFi() {
  Serial.println(F("[WiFiManager] Scanning for known networks..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false); // false = keep the radio on (avoids modem off on ESP32)
  delay(50);
  WiFi.scanNetworks(/*async=*/true, /*showHidden=*/true);
  _scanStartTime = millis();
  _currentState  = WIFI_STATE_SCANNING;
}

void WiFiManager::_checkScanStatus() {
  int n = WiFi.scanComplete();

  // Still running
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - _scanStartTime >= SCAN_TIMEOUT_MS) {
      Serial.println(F("[WiFiManager] Scan timed out. Falling back to AP mode."));
      WiFi.scanDelete();
      _currentState = WIFI_STATE_FAILED;
    }
    return;
  }

  // Hardware error
  if (n == WIFI_SCAN_FAILED) {
    Serial.println(F("[WiFiManager] Scan failed. Falling back to AP mode."));
    _currentState = WIFI_STATE_FAILED;
    return;
  }

  // Scan complete (n >= 0) — match against saved credentials
  _matchedSSIDs.clear();
  _matchedPasses.clear();

  String json = _loadData();
  JsonDocument doc;

  if (!deserializeJson(doc, json)) {
    JsonArray arr = doc.as<JsonArray>();

    if (n > 0) {
      // Collect matches and deduplicate, keeping the best RSSI for each SSID
      struct Match { String ssid, pass; int32_t rssi; };
      std::vector<Match> found;

      for (int i = 0; i < n; ++i) {
        String  scanned = WiFi.SSID(i);
        int32_t rssi    = WiFi.RSSI(i);

        for (JsonObject cred : arr) {
          if (scanned == cred["ssid"].as<String>()) {
            bool exists = false;
            for (auto& f : found) {
              if (f.ssid == scanned) {
                exists = true;
                if (rssi > f.rssi) f.rssi = rssi; // keep strongest RSSI
                break;
              }
            }
            if (!exists) found.push_back({scanned, cred["password"].as<String>(), rssi});
            break; // stop inner credential loop once SSID is matched
          }
        }
      }

      // Sort strongest signal first
      std::sort(found.begin(), found.end(),
                [](const Match& a, const Match& b){ return a.rssi > b.rssi; });

      for (const auto& f : found) {
        _matchedSSIDs.push_back(f.ssid);
        _matchedPasses.push_back(f.pass);
        Serial.printf("[WiFiManager] Matched: %s (RSSI: %d)\n", f.ssid.c_str(), f.rssi);
      }
    } else {
      // No visible networks — try all saved credentials sequentially as fallback
      for (JsonObject cred : arr) {
        _matchedSSIDs.push_back(cred["ssid"].as<String>());
        _matchedPasses.push_back(cred["password"].as<String>());
      }
    }
  }

  WiFi.scanDelete();

  if (_matchedSSIDs.empty()) {
    Serial.println(F("[WiFiManager] No known networks found."));
    _currentState = WIFI_STATE_FAILED;
    return;
  }

  _currentNetworkIndex = 0;
  _isConnecting        = true;
  _startNextConnection();
}

void WiFiManager::_startNextConnection() {
  if (_currentNetworkIndex >= (int)_matchedSSIDs.size()) {
    Serial.println(F("[WiFiManager] All networks exhausted. Connection failed."));
    _currentState = WIFI_STATE_FAILED;
    _isConnecting = false;
    return;
  }
  const String& ssid = _matchedSSIDs[_currentNetworkIndex];
  const String& pass = _matchedPasses[_currentNetworkIndex];
  Serial.printf("[WiFiManager] Trying: %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  _currentState     = WIFI_STATE_CONNECTING;
  _startAttemptTime = millis();
}

void WiFiManager::_checkConnectionStatus() {
  if (!_isConnecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    _currentState = WIFI_STATE_CONNECTED;
    _isConnecting = false;
    Serial.println(F("\n[WiFiManager] Connected!"));
    Serial.printf("[WiFiManager] IP: %s\n", WiFi.localIP().toString().c_str());
    return;
  }

  if (millis() - _startAttemptTime >= CONNECTION_TIMEOUT_MS) {
    Serial.printf("\n[WiFiManager] Timeout for '%s'. Trying next...\n",
                  _matchedSSIDs[_currentNetworkIndex].c_str());
    _currentNetworkIndex++;
    _startNextConnection();
  }
}

WiFiState WiFiManager::getState() const { return _currentState; }

// ══════════════════════════════════════════════════════════════════════════
//  MAIN PROCESS LOOP
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::process() {
  // Non-blocking scheduled restart (avoids delay() inside web handlers)
  if (_restartPending && millis() >= _restartAt) {
    ESP.restart();
  }

  // Reconnect after a drop — but not while the captive portal is active
  if (_disconnectTriggered && !_apModeActive) {
    _disconnectTriggered = false;
    Serial.println(F("[WiFiManager] Reconnecting after drop..."));
    connectToWiFi();
  }

  // State machine tick
  if      (_currentState == WIFI_STATE_SCANNING)   _checkScanStatus();
  else if (_currentState == WIFI_STATE_CONNECTING)  _checkConnectionStatus();

  // DNS redirect only meaningful when the captive portal is running
  if (_apModeActive) _dnsServer.processNextRequest();

  // Standard WebServer needs manual client pumping; AsyncWebServer handles itself
#ifndef WIFIMANAGER_USE_ASYNC_WEBSERVER
  if (_server) _server->handleClient();
#endif
}

// ══════════════════════════════════════════════════════════════════════════
//  SERVER SETUP
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::setServer(ESP_WebServer* server) { _server = server; }

void WiFiManager::startAPMode(ESP_WebServer& server) {
  _server       = &server;
  _apModeActive = true;
  _currentState = WIFI_STATE_AP_MODE;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ap_ssid, _ap_password);
  Serial.printf("[WiFiManager] AP started  SSID: %-20s  IP: %s\n",
                _ap_ssid, WiFi.softAPIP().toString().c_str());

  _dnsServer.start(53, "*", WiFi.softAPIP());

#ifdef WIFIMANAGER_USE_ASYNC_WEBSERVER
  _server->on("/",       HTTP_GET,  [this](AsyncWebServerRequest* r){ _handleRoot(r);   });
  _server->on("/scan",   HTTP_GET,  [this](AsyncWebServerRequest* r){ _handleScan(r);   });
  _server->on("/list",   HTTP_GET,  [this](AsyncWebServerRequest* r){ _handleList(r);   });
  _server->on("/delete", HTTP_ANY,  [this](AsyncWebServerRequest* r){ _handleDelete(r); });
  _server->on("/save",   HTTP_POST, [this](AsyncWebServerRequest* r){ _handleSave(r);   });
  _server->onNotFound(              [this](AsyncWebServerRequest* r){ _handleRoot(r);   });
#else
  _server->on("/",       std::bind(&WiFiManager::_handleRoot,   this));
  _server->on("/scan",   std::bind(&WiFiManager::_handleScan,   this));
  _server->on("/list",   std::bind(&WiFiManager::_handleList,   this));
  _server->on("/delete", std::bind(&WiFiManager::_handleDelete, this));
  _server->on("/save",   HTTP_POST, std::bind(&WiFiManager::_handleSave, this));
  _server->onNotFound([this](){ _handleRoot(); });
#endif

  _server->begin();
}

// ══════════════════════════════════════════════════════════════════════════
//  URL DECODE
// ══════════════════════════════════════════════════════════════════════════

String WiFiManager::_urlDecode(const String& str) {
  String out;
  out.reserve(str.length());
  for (size_t i = 0; i < str.length(); ++i) {
    char c = str[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < str.length()) {
      char hex[3] = { str[i + 1], str[i + 2], '\0' };
      out += (char)strtol(hex, nullptr, 16);
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

// ══════════════════════════════════════════════════════════════════════════
//  WEB HANDLERS — AsyncWebServer
// ══════════════════════════════════════════════════════════════════════════
#ifdef WIFIMANAGER_USE_ASYNC_WEBSERVER

void WiFiManager::_handleRoot(AsyncWebServerRequest* req) {
  AsyncWebServerResponse* resp =
    req->beginResponse_P(200, "text/html", page_index, page_index_len);
  resp->addHeader("Content-Encoding", "gzip");
  req->send(resp);
}

void WiFiManager::_handleScan(AsyncWebServerRequest* req) {
  int n = WiFi.scanNetworks(/*async=*/false, /*showHidden=*/true);
  if (n <= 0) {
    WiFi.scanDelete();
    req->send(200, "application/json", "[]");
    return;
  }
  JsonDocument doc;
  for (int i = 0; i < n; ++i) {
    JsonObject obj    = doc.add<JsonObject>();
    obj["ssid"]       = WiFi.SSID(i);
    obj["rssi"]       = WiFi.RSSI(i);
#if defined(ESP32)
    obj["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
#elif defined(ESP8266)
    obj["encryption"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
#endif
  }
  String json; serializeJson(doc, json);
  WiFi.scanDelete();
  req->send(200, "application/json", json);
}

void WiFiManager::_handleSave(AsyncWebServerRequest* req) {
  // Accept params from both POST body and query string
  auto param = [&](const char* name) -> String {
    if (req->hasParam(name, true)) return req->getParam(name, true)->value();
    if (req->hasParam(name))       return req->getParam(name)->value();
    return "";
  };
  String ssid = param("ssid");
  if (ssid.isEmpty()) { req->send(400, "text/plain", "SSID cannot be empty"); return; }
  addCredential(ssid.c_str(), param("password").c_str());
  req->send(200, "text/html", "<p>Saved! Rebooting...</p>");
  // Non-blocking restart — avoids calling delay() inside an async callback
  _restartPending = true;
  _restartAt      = millis() + RESTART_DELAY_MS;
}

void WiFiManager::_handleList(AsyncWebServerRequest* req) {
  String json = _loadData();
  JsonDocument doc, out;
  JsonArray arrOut = out.to<JsonArray>();
  if (!deserializeJson(doc, json))
    for (JsonObject obj : doc.as<JsonArray>()) {
      JsonObject o = arrOut.add<JsonObject>();
      o["ssid"] = obj["ssid"];
    }
  String body; serializeJson(arrOut, body);
  req->send(200, "application/json", body);
}

void WiFiManager::_handleDelete(AsyncWebServerRequest* req) {
  String ssid;
  if      (req->hasParam("ssid", true)) ssid = req->getParam("ssid", true)->value();
  else if (req->hasParam("ssid"))       ssid = req->getParam("ssid")->value();
  if (ssid.isEmpty()) { req->send(400, "text/plain", "SSID missing"); return; }
  deleteCredential(ssid.c_str());
  req->send(200, "text/plain", "Deleted");
}

// ══════════════════════════════════════════════════════════════════════════
//  WEB HANDLERS — Standard WebServer
// ══════════════════════════════════════════════════════════════════════════
#else

void WiFiManager::_handleRoot() {
  _server->sendHeader(F("Content-Encoding"), F("gzip"));
  _server->send_P(200, "text/html", (const char*)page_index, page_index_len);
}

void WiFiManager::_handleScan() {
  int n = WiFi.scanNetworks(/*async=*/false, /*showHidden=*/true);
  if (n <= 0) {
    WiFi.scanDelete();
    _server->send(200, "application/json", "[]");
    return;
  }
  JsonDocument doc;
  for (int i = 0; i < n; ++i) {
    JsonObject obj    = doc.add<JsonObject>();
    obj["ssid"]       = WiFi.SSID(i);
    obj["rssi"]       = WiFi.RSSI(i);
#if defined(ESP32)
    obj["encryption"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
#elif defined(ESP8266)
    obj["encryption"] = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
#endif
  }
  String json; serializeJson(doc, json);
  WiFi.scanDelete();
  _server->send(200, "application/json", json);
}

void WiFiManager::_handleSave() {
  String ssid     = _urlDecode(_server->arg("ssid"));
  String password = _urlDecode(_server->arg("password"));
  if (ssid.isEmpty()) { _server->send(400, "text/plain", "SSID cannot be empty"); return; }
  addCredential(ssid.c_str(), password.c_str());
  _server->send(200, "text/html", "<p>Saved! Rebooting...</p>");
  _restartPending = true;
  _restartAt      = millis() + RESTART_DELAY_MS;
}

void WiFiManager::_handleList() {
  String json = _loadData();
  JsonDocument doc, out;
  JsonArray arrOut = out.to<JsonArray>();
  if (!deserializeJson(doc, json))
    for (JsonObject obj : doc.as<JsonArray>()) {
      JsonObject o = arrOut.add<JsonObject>();
      o["ssid"] = obj["ssid"];
    }
  String body; serializeJson(arrOut, body);
  _server->send(200, "application/json", body);
}

void WiFiManager::_handleDelete() {
  if (!_server->hasArg("ssid")) { _server->send(400, "text/plain", "SSID missing"); return; }
  deleteCredential(_urlDecode(_server->arg("ssid")).c_str());
  _server->send(200, "text/plain", "Deleted");
}

#endif // WIFIMANAGER_USE_ASYNC_WEBSERVER

// ══════════════════════════════════════════════════════════════════════════
//  SERIAL COMMAND PARSER
// ══════════════════════════════════════════════════════════════════════════

int WiFiManager::_splitArgsQuoted(const String& line, String out[], int maxParts) {
  bool   inQuotes = false;
  String cur;
  int    count = 0;
  for (size_t i = 0; i < line.length(); ++i) {
    char c = line[i];
    if (c == '"') { inQuotes = !inQuotes; continue; }
    if (!inQuotes && isspace((unsigned char)c)) {
      if (cur.length() && count < maxParts) out[count++] = cur;
      cur = "";
    } else {
      cur += c;
    }
  }
  if (cur.length() && count < maxParts) out[count++] = cur;
  return count;
}

void WiFiManager::executeCommand(String cmdLine, Stream& io) {
  cmdLine.trim();
  if (!cmdLine.length()) return;

  const int MAXP = 4;
  String    p[MAXP];
  int n = _splitArgsQuoted(cmdLine, p, MAXP);
  if (n == 0) return;

  String cmd = p[0]; cmd.toUpperCase();

  if      (cmd == "ADD"    && n >= 2) addCredential(p[1].c_str(), n >= 3 ? p[2].c_str() : "");
  else if ((cmd == "DEL"   || cmd == "DELETE") && n >= 2) deleteCredential(p[1].c_str());
  else if (cmd == "CLEAR")            clearCredentials();
  else if (cmd == "LIST")             listCredentialsToSerial();
  else if (cmd == "STATUS") {
    io.printf("[WiFiManager] State: %d | IP: %s\n",
              _currentState, WiFi.localIP().toString().c_str());
  }
  else _printHelp();
}

void WiFiManager::_printHelp() const {
  Serial.println(F("[WiFiManager] Commands: ADD \"SSID\" \"PASS\"  |  DEL \"SSID\"  |  LIST  |  CLEAR  |  STATUS"));
}
