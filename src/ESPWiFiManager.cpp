/**
 * @file ESPWiFiManager.cpp
 * @brief Implementation of WiFiManager v5.
 *
 * Key improvements over v4:
 *  - Unified Preferences (NVS) storage on both ESP32 and ESP8266
 *  - Auto AP Fallback with dual-mode AP+STA
 *  - Background scan while portal is active — auto-reconnect to STA
 *  - Exponential backoff reconnection
 *  - Static IP configuration (persisted in NVS)
 *  - Event callbacks (onStateChange, onConnected, onDisconnected, etc.)
 *  - Dynamic logging engine (level, stream, custom handler)
 *  - Low-level tuning: Tx power, sleep mode, AP channel/hidden/clients
 */

#include "ESPWiFiManager.h"
#include <stdarg.h>   // for va_list in _log()

// ── Constructor ───────────────────────────────────────────────────────────
WiFiManager::WiFiManager(const char* ap_ssid, const char* ap_password)
  : _ap_ssid(ap_ssid), _ap_password(ap_password) {}

// ══════════════════════════════════════════════════════════════════════════
//  LOGGING ENGINE
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::_log(WiFiLogLevel level, const char* fmt, ...) const {
  if (level > _logLevel) return;

  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (_logHandler) {
    _logHandler(level, buf);
    return;
  }
  if (_logStream) {
    _logStream->println(buf);
  }
}

void WiFiManager::setLogLevel(WiFiLogLevel level) { _logLevel = level; }

void WiFiManager::setLogStream(Stream& stream) { _logStream = &stream; }

void WiFiManager::setLogHandler(std::function<void(WiFiLogLevel, const char*)> handler) {
  _logHandler = handler;
}

// ══════════════════════════════════════════════════════════════════════════
//  PERSISTENT STORAGE  (unified Preferences / NVS on both platforms)
// ══════════════════════════════════════════════════════════════════════════

String WiFiManager::_loadData() const {
  _prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  String json = _prefs.getString(NVS_KEY_CREDS, "[]");
  _prefs.end();
  return json;
}

void WiFiManager::_saveData(const String& js) {
  _prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  _prefs.putString(NVS_KEY_CREDS, js);
  _prefs.end();
}

void WiFiManager::_loadStaticIP() {
  _prefs.begin(NVS_NAMESPACE, true);
  String json = _prefs.getString(NVS_KEY_STATIC_IP, "");
  _prefs.end();
  if (json.isEmpty()) return;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return;

  if (doc["enabled"] | false) {
    _staticIP.enabled = true;
    _staticIP.ip.fromString(doc["ip"] | "0.0.0.0");
    _staticIP.gateway.fromString(doc["gw"] | "0.0.0.0");
    _staticIP.subnet.fromString(doc["sn"] | "255.255.255.0");
    _staticIP.dns1.fromString(doc["d1"] | "0.0.0.0");
    _staticIP.dns2.fromString(doc["d2"] | "0.0.0.0");
  }
}

void WiFiManager::_saveStaticIP() {
  JsonDocument doc;
  doc["enabled"] = _staticIP.enabled;
  doc["ip"]      = _staticIP.ip.toString();
  doc["gw"]      = _staticIP.gateway.toString();
  doc["sn"]      = _staticIP.subnet.toString();
  doc["d1"]      = _staticIP.dns1.toString();
  doc["d2"]      = _staticIP.dns2.toString();
  String json; serializeJson(doc, json);
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString(NVS_KEY_STATIC_IP, json);
  _prefs.end();
}

void WiFiManager::_applyStaticIP() {
  if (!_staticIP.enabled) return;
  WiFi.config(_staticIP.ip, _staticIP.gateway, _staticIP.subnet,
               _staticIP.dns1, _staticIP.dns2);
  _log(WIFI_LOG_INFO, "[WiFiManager] Static IP applied: %s", _staticIP.ip.toString().c_str());
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
      _log(WIFI_LOG_INFO, "[WiFiManager] Updated credential: %s", ssid);
      if (_cbCredsChanged) _cbCredsChanged();
      return;
    }
  }

  // Enforce FIFO capacity limit
  if (arr.size() >= MAX_CREDS) {
    _log(WIFI_LOG_INFO, "[WiFiManager] Limit (%u) reached, removing oldest: %s",
         (unsigned)MAX_CREDS, arr[0]["ssid"].as<const char*>());
    arr.remove(0);
  }

  JsonObject o  = arr.add<JsonObject>();
  o["ssid"]     = ssid;
  o["password"] = password;
  String out; serializeJson(arr, out); _saveData(out);
  _log(WIFI_LOG_INFO, "[WiFiManager] Added credential: %s", ssid);
  if (_cbCredsChanged) _cbCredsChanged();
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
      _log(WIFI_LOG_INFO, "[WiFiManager] Deleted credential: %s", ssid);
      if (_cbCredsChanged) _cbCredsChanged();
      return;
    }
  }
  _log(WIFI_LOG_INFO, "[WiFiManager] Credential not found: %s", ssid);
}

void WiFiManager::clearCredentials() {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.remove(NVS_KEY_CREDS);
  _prefs.end();
  _log(WIFI_LOG_INFO, "[WiFiManager] All credentials cleared.");
  if (_cbCredsChanged) _cbCredsChanged();
}

void WiFiManager::listCredentialsToSerial() const {
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json) || doc.as<JsonArray>().size() == 0) {
    _log(WIFI_LOG_INFO, "[WiFiManager] No credentials stored.");
    return;
  }
  _log(WIFI_LOG_INFO, "[WiFiManager] Stored networks:");
  for (JsonObject obj : doc.as<JsonArray>()) {
    // Print each SSID individually since _log uses a fixed buffer
    char buf[80];
    snprintf(buf, sizeof(buf), "  - %s", obj["ssid"].as<const char*>());
    _log(WIFI_LOG_INFO, buf);
  }
}

// ══════════════════════════════════════════════════════════════════════════
//  STATIC IP
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::setStaticIP(IPAddress ip, IPAddress gateway, IPAddress subnet,
                               IPAddress dns1, IPAddress dns2) {
  _staticIP = { ip, gateway, subnet, dns1, dns2, true };
  _saveStaticIP();
  _log(WIFI_LOG_INFO, "[WiFiManager] Static IP set: %s", ip.toString().c_str());
}

void WiFiManager::clearStaticIP() {
  _staticIP = {};
  _saveStaticIP();
  _log(WIFI_LOG_INFO, "[WiFiManager] Static IP cleared (DHCP).");
}

bool WiFiManager::hasStaticIP() const { return _staticIP.enabled; }

// ══════════════════════════════════════════════════════════════════════════
//  STATE HELPERS & QUERY
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::_setState(WiFiState newState) {
  if (newState == _currentState) return;
  WiFiState old = _currentState;
  _currentState  = newState;
  if (_cbStateChange) _cbStateChange(old, newState);
}

WiFiState WiFiManager::getState()    const { return _currentState; }
bool      WiFiManager::isConnected() const { return _currentState == WIFI_STATE_CONNECTED; }
IPAddress WiFiManager::getLocalIP()  const { return WiFi.localIP(); }
IPAddress WiFiManager::getAPIP()     const { return WiFi.softAPIP(); }

// ══════════════════════════════════════════════════════════════════════════
//  CALLBACK REGISTRATION
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::onStateChange(std::function<void(WiFiState, WiFiState)> cb)     { _cbStateChange   = cb; }
void WiFiManager::onStationConnected(std::function<void(const String&, IPAddress)> cb) { _cbConnected = cb; }
void WiFiManager::onStationDisconnected(std::function<void(int)> cb)               { _cbDisconnected  = cb; }
void WiFiManager::onAPModeStarted(std::function<void(const String&, IPAddress)> cb){ _cbAPStarted     = cb; }
void WiFiManager::onAPModeStopped(std::function<void()> cb)                        { _cbAPStopped     = cb; }
void WiFiManager::onCredentialsChanged(std::function<void()> cb)                   { _cbCredsChanged  = cb; }

// ══════════════════════════════════════════════════════════════════════════
//  BEHAVIOUR TUNING
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::setAutoAPFallback(bool enable, ESP_WebServer* server) {
  _autoAPEnabled = enable;
  _autoAPServer  = server;
}

void WiFiManager::setBackgroundReconnect(bool enable) {
  _bgReconnectEnabled = enable;
}

void WiFiManager::setAPTimeout(uint32_t timeoutMs) {
  _apTimeoutMs = timeoutMs;
}

void WiFiManager::setAPConfig(uint8_t channel, bool hidden, uint8_t maxClients) {
  _apChannel    = channel;
  _apHidden     = hidden;
  _apMaxClients = maxClients;
}

void WiFiManager::setTxPower(float dbm) {
#if defined(ESP32)
  WiFi.setTxPower(static_cast<wifi_power_t>((int)(dbm * 4)));
#elif defined(ESP8266)
  WiFi.setOutputPower(dbm);
#endif
  _log(WIFI_LOG_DEBUG, "[WiFiManager] Tx power set to %.1f dBm", dbm);
}

void WiFiManager::setWiFiSleep(bool enable) {
#if defined(ESP32)
  WiFi.setSleep(enable);
#elif defined(ESP8266)
  WiFi.setSleepMode(enable ? WIFI_MODEM_SLEEP : WIFI_NONE_SLEEP);
#endif
  _log(WIFI_LOG_DEBUG, "[WiFiManager] WiFi sleep: %s", enable ? "enabled" : "disabled");
}

// ══════════════════════════════════════════════════════════════════════════
//  INITIALISATION & EVENT HANDLERS
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::begin() {
  _loadStaticIP();   // restore any persisted static IP from NVS
  _setupEventHandlers();
  _printHelp();
  _log(WIFI_LOG_INFO, "[WiFiManager] v5 ready. AutoAP=%s BackgroundReconnect=%s",
       _autoAPEnabled ? "on" : "off", _bgReconnectEnabled ? "on" : "off");

  // Auto-kick the first connection attempt
  connectToWiFi();
}

void WiFiManager::_setupEventHandlers() {
#if defined(ESP32)
  WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (_apModeActive) return;
    _log(WIFI_LOG_INFO, "[WiFiManager] Disconnected (event). Reason: %d",
         (int)info.wifi_sta_disconnected.reason);
    _lastDisconnectReason = (int)info.wifi_sta_disconnected.reason;
    if (_cbDisconnected) _cbDisconnected(_lastDisconnectReason);
    _disconnectTriggered = true;
    _setState(WIFI_STATE_FAILED);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

#elif defined(ESP8266)
  _disconnectHandler = WiFi.onStationModeDisconnected(
    [this](const WiFiEventStationModeDisconnected& e) {
      if (_apModeActive) return;
      _log(WIFI_LOG_INFO, "[WiFiManager] Disconnected (event). Reason: %d", (int)e.reason);
      _lastDisconnectReason = (int)e.reason;
      if (_cbDisconnected) _cbDisconnected(_lastDisconnectReason);
      _disconnectTriggered = true;
      _setState(WIFI_STATE_FAILED);
    });
#endif
}

// ══════════════════════════════════════════════════════════════════════════
//  SMART CONNECT
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::connectToWiFi() {
  _log(WIFI_LOG_INFO, "[WiFiManager] Starting async WiFi scan...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  delay(50);
  WiFi.scanNetworks(/*async=*/true, /*showHidden=*/true);
  _scanStartTime = millis();
  _setState(WIFI_STATE_SCANNING);
}

void WiFiManager::_checkScanStatus() {
  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - _scanStartTime >= SCAN_TIMEOUT) {
      _log(WIFI_LOG_ERROR, "[WiFiManager] Scan timed out. Falling back.");
      WiFi.scanDelete();
      _handleAllFailed();
    }
    return;
  }

  if (n == WIFI_SCAN_FAILED) {
    _log(WIFI_LOG_ERROR, "[WiFiManager] Scan failed. Falling back.");
    _handleAllFailed();
    return;
  }

  // n >= 0 — scan complete, match against saved credentials
  _matchedSSIDs.clear();
  _matchedPasses.clear();

  String json = _loadData();
  JsonDocument doc;

  if (!deserializeJson(doc, json)) {
    JsonArray arr = doc.as<JsonArray>();

    if (n > 0) {
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
                if (rssi > f.rssi) f.rssi = rssi;
                break;
              }
            }
            if (!exists) found.push_back({scanned, cred["password"].as<String>(), rssi});
            break;
          }
        }
      }

      std::sort(found.begin(), found.end(),
                [](const Match& a, const Match& b){ return a.rssi > b.rssi; });

      for (const auto& f : found) {
        _matchedSSIDs.push_back(f.ssid);
        _matchedPasses.push_back(f.pass);
        _log(WIFI_LOG_INFO, "[WiFiManager] Matched: %s (RSSI: %d dBm)",
             f.ssid.c_str(), f.rssi);
      }
    } else {
      // No visible networks — try all saved credentials as fallback
      for (JsonObject cred : arr) {
        _matchedSSIDs.push_back(cred["ssid"].as<String>());
        _matchedPasses.push_back(cred["password"].as<String>());
      }
    }
  }

  WiFi.scanDelete();

  if (_matchedSSIDs.empty()) {
    _log(WIFI_LOG_INFO, "[WiFiManager] No known networks found.");
    _handleAllFailed();
    return;
  }

  _currentNetworkIndex = 0;
  _isConnecting        = true;
  _startNextConnection();
}

void WiFiManager::_startNextConnection() {
  if (_currentNetworkIndex >= (int)_matchedSSIDs.size()) {
    _log(WIFI_LOG_INFO, "[WiFiManager] All networks exhausted.");
    _isConnecting = false;
    _handleAllFailed();
    return;
  }
  const String& ssid = _matchedSSIDs[_currentNetworkIndex];
  const String& pass = _matchedPasses[_currentNetworkIndex];
  _log(WIFI_LOG_INFO, "[WiFiManager] Trying: %s (%d/%d)",
       ssid.c_str(), _currentNetworkIndex + 1, (int)_matchedSSIDs.size());
  WiFi.mode(WIFI_STA);
  _applyStaticIP();
  WiFi.begin(ssid.c_str(), pass.c_str());
  _setState(WIFI_STATE_CONNECTING);
  _startAttemptTime = millis();
}

void WiFiManager::_checkConnectionStatus() {
  if (!_isConnecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    _setState(WIFI_STATE_CONNECTED);
    _isConnecting = false;
    _resetBackoff();
    String ssid = WiFi.SSID();
    IPAddress ip = WiFi.localIP();
    _log(WIFI_LOG_INFO, "[WiFiManager] Connected! SSID: %s  IP: %s",
         ssid.c_str(), ip.toString().c_str());
    if (_cbConnected) _cbConnected(ssid, ip);
    return;
  }

  if (millis() - _startAttemptTime >= CONNECTION_TIMEOUT) {
    _log(WIFI_LOG_INFO, "[WiFiManager] Timeout for '%s'. Trying next...",
         _matchedSSIDs[_currentNetworkIndex].c_str());
    _currentNetworkIndex++;
    _startNextConnection();
  }
}

// ── Called whenever we have exhausted all networks ───────────────────────
void WiFiManager::_handleAllFailed() {
  _setState(WIFI_STATE_FAILED);

  // Auto AP fallback if enabled and a server is registered
  if (_autoAPEnabled && _autoAPServer) {
    _log(WIFI_LOG_INFO, "[WiFiManager] Auto AP Fallback: starting captive portal.");
    startAPMode(*_autoAPServer);
    return;
  }

  // Otherwise schedule a backoff reconnect
  if (_bgReconnectEnabled) {
    _scheduleReconnect();
  }
}

// ══════════════════════════════════════════════════════════════════════════
//  EXPONENTIAL BACKOFF RECONNECTION
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::_resetBackoff() {
  _reconnectInterval  = RECONNECT_BASE;
  _reconnectScheduled = false;
}

void WiFiManager::_scheduleReconnect() {
  if (_reconnectScheduled) return;
  _reconnectScheduled = true;
  _nextReconnectAt    = millis() + _reconnectInterval;
  _log(WIFI_LOG_INFO, "[WiFiManager] Reconnect scheduled in %lu ms (backoff: %lu ms).",
       _reconnectInterval, _reconnectInterval);

  // Double the interval for next time, clamped to max
  _reconnectInterval = min(_reconnectInterval * 2, (unsigned long)RECONNECT_MAX);
}

void WiFiManager::_checkReconnect() {
  if (!_reconnectScheduled || _apModeActive) return;
  if (millis() >= _nextReconnectAt) {
    _reconnectScheduled = false;
    _disconnectTriggered = false;
    _log(WIFI_LOG_INFO, "[WiFiManager] Backoff elapsed. Reconnecting...");
    connectToWiFi();
  }
}

// ══════════════════════════════════════════════════════════════════════════
//  AP TIMEOUT & BACKGROUND SCAN
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::_checkAPTimeout() {
  if (!_apModeActive || _apTimeoutMs == 0) return;
  if (millis() - _apStartedAt >= _apTimeoutMs) {
    _log(WIFI_LOG_INFO, "[WiFiManager] AP timeout reached. Reverting to STA scan.");
    stopAPMode();
    connectToWiFi();
  }
}

void WiFiManager::_startBGScan() {
  _log(WIFI_LOG_DEBUG, "[WiFiManager] Background scan started (AP+STA dual mode).");
  WiFi.mode(WIFI_AP_STA);
  WiFi.scanNetworks(/*async=*/true, /*showHidden=*/true);
  _bgScanPending = true;
}

void WiFiManager::_checkBGScan() {
  if (!_apModeActive) return;

  // Schedule next background scan
  if (!_bgScanPending && millis() >= _bgScanAt) {
    _startBGScan();
    return;
  }

  if (!_bgScanPending) return;

  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;

  _bgScanPending = false;
  _bgScanAt      = millis() + BG_SCAN_INTERVAL;

  if (n <= 0) {
    _log(WIFI_LOG_DEBUG, "[WiFiManager] Background scan: no networks found.");
    return;
  }

  _log(WIFI_LOG_DEBUG, "[WiFiManager] Background scan complete: %d network(s).", n);

  // Check if any saved credentials match
  String json = _loadData();
  JsonDocument doc;
  if (deserializeJson(doc, json)) { WiFi.scanDelete(); return; }

  JsonArray arr = doc.as<JsonArray>();
  for (int i = 0; i < n; ++i) {
    String scanned = WiFi.SSID(i);
    for (JsonObject cred : arr) {
      if (scanned == cred["ssid"].as<String>()) {
        _log(WIFI_LOG_INFO, "[WiFiManager] Known network '%s' detected. Auto-recovering!",
             scanned.c_str());
        WiFi.scanDelete();
        // Tear down portal, connect to STA
        stopAPMode();
        connectToWiFi();
        return;
      }
    }
  }
  WiFi.scanDelete();
}

// ══════════════════════════════════════════════════════════════════════════
//  MAIN PROCESS LOOP
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::process() {
  // 1. Pending restart (non-blocking, set by /save handler)
  if (_restartPending && millis() >= _restartAt) {
    ESP.restart();
  }

  // 2. Disconnect event from hardware ISR — schedule a reconnect
  if (_disconnectTriggered && !_apModeActive) {
    _disconnectTriggered = false;
    if (_bgReconnectEnabled) {
      _scheduleReconnect();
    }
  }

  // 3. State machine tick
  switch (_currentState) {
    case WIFI_STATE_SCANNING:   _checkScanStatus();       break;
    case WIFI_STATE_CONNECTING: _checkConnectionStatus(); break;
    default:                                               break;
  }

  // 4. Background reconnect timer
  if (_currentState == WIFI_STATE_FAILED || _reconnectScheduled) {
    _checkReconnect();
  }

  // 5. Captive portal DNS + AP lifecycle
  if (_apModeActive) {
    _dnsServer.processNextRequest();
    _checkAPTimeout();
    _checkBGScan();
  }

  // 6. Standard WebServer client pump (AsyncWebServer is self-driven)
#ifndef WIFIMANAGER_USE_ASYNC_WEBSERVER
  if (_server) _server->handleClient();
#endif
}

// ══════════════════════════════════════════════════════════════════════════
//  SERVER SETUP
// ══════════════════════════════════════════════════════════════════════════

void WiFiManager::setServer(ESP_WebServer* server) { _server = server; }

void WiFiManager::startAPMode(ESP_WebServer& server) {
  if (_apModeActive) return;  // Guard against duplicate calls

  _server       = &server;
  _apModeActive = true;
  _apStartedAt  = millis();
  _bgScanAt     = millis() + BG_SCAN_INTERVAL;  // first bg scan after interval
  _bgScanPending = false;

  WiFi.mode(WIFI_AP_STA);  // dual mode — keeps STA radio alive for bg scans
  WiFi.softAP(_ap_ssid, _ap_password, _apChannel, _apHidden, _apMaxClients);
  IPAddress apIP = WiFi.softAPIP();
  _log(WIFI_LOG_INFO, "[WiFiManager] AP started  SSID: %-20s  IP: %s",
       _ap_ssid, apIP.toString().c_str());

  _dnsServer.start(53, "*", apIP);

  _setState(WIFI_STATE_AP_MODE);

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
  if (_cbAPStarted) _cbAPStarted(String(_ap_ssid), apIP);
}

void WiFiManager::stopAPMode() {
  if (!_apModeActive) return;
  _dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  _apModeActive  = false;
  _bgScanPending = false;
  _log(WIFI_LOG_INFO, "[WiFiManager] AP stopped.");
  _setState(WIFI_STATE_IDLE);
  if (_cbAPStopped) _cbAPStopped();
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
  if (n <= 0) { WiFi.scanDelete(); req->send(200, "application/json", "[]"); return; }
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
  auto param = [&](const char* name) -> String {
    if (req->hasParam(name, true)) return req->getParam(name, true)->value();
    if (req->hasParam(name))       return req->getParam(name)->value();
    return "";
  };
  String ssid = param("ssid");
  if (ssid.isEmpty()) { req->send(400, "text/plain", "SSID cannot be empty"); return; }
  addCredential(ssid.c_str(), param("password").c_str());
  req->send(200, "text/html", "<p>Saved! Rebooting...</p>");
  _restartPending = true;
  _restartAt      = millis() + RESTART_DELAY;
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
  if (n <= 0) { WiFi.scanDelete(); _server->send(200, "application/json", "[]"); return; }
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
  _restartAt      = millis() + RESTART_DELAY;
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

  if (cmd == "ADD" && n >= 2) {
    addCredential(p[1].c_str(), n >= 3 ? p[2].c_str() : "");
  }
  else if ((cmd == "DEL" || cmd == "DELETE") && n >= 2) {
    deleteCredential(p[1].c_str());
  }
  else if (cmd == "CLEAR") {
    clearCredentials();
  }
  else if (cmd == "LIST") {
    listCredentialsToSerial();
  }
  else if (cmd == "STATUS") {
    io.printf("[WiFiManager] State: %d | STA IP: %s | AP IP: %s\n",
              (int)_currentState,
              WiFi.localIP().toString().c_str(),
              WiFi.softAPIP().toString().c_str());
  }
  else if (cmd == "RECONNECT") {
    _log(WIFI_LOG_INFO, "[WiFiManager] Manual reconnect triggered.");
    stopAPMode();
    connectToWiFi();
  }
  else if (cmd == "APSTART") {
    if (_autoAPServer) startAPMode(*_autoAPServer);
    else io.println("[WiFiManager] No server registered. Use setAutoAPFallback(true, &server) first.");
  }
  else if (cmd == "APSTOP") {
    stopAPMode();
  }
  else if (cmd == "LOGLEVEL" && n >= 2) {
    int lvl = p[1].toInt();
    if (lvl >= 0 && lvl <= 3) {
      setLogLevel(static_cast<WiFiLogLevel>(lvl));
      io.printf("[WiFiManager] Log level set to %d\n", lvl);
    } else {
      io.println("[WiFiManager] Log level must be 0-3.");
    }
  }
  else {
    _printHelp();
  }
}

void WiFiManager::_printHelp() const {
  _log(WIFI_LOG_INFO,
    "[WiFiManager] Commands: "
    "ADD \"SSID\" \"PASS\" | DEL \"SSID\" | LIST | CLEAR | STATUS | "
    "RECONNECT | APSTART | APSTOP | LOGLEVEL <0-3>");
}
