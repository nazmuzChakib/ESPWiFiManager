#include "ESPWiFiManager.h"
#include "page_index.h"

// -------------------- Constructor --------------------
WiFiManager::WiFiManager(const char* ap_ssid, const char* ap_password)
: _ap_ssid(ap_ssid), _ap_password(ap_password) {}

// -------------------- Storage helpers --------------------
String WiFiManager::_loadCredentialsJson() const {
  _prefs.begin(NVS_NAMESPACE, true);
  String json = _prefs.getString(NVS_KEY_MULTI, "[]");
  _prefs.end();
  return json;
}

void WiFiManager::_saveCredentialsJson(const String& js) {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.putString(NVS_KEY_MULTI, js);
  _prefs.end();
}

String WiFiManager::getCredentialsJson() const {
  return _loadCredentialsJson();
}

// -------------------- Manage credentials --------------------
void WiFiManager::addCredential(const char* ssid, const char* password) {
  String json = _loadCredentialsJson();
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, json);
  JsonArray arr;
  if (err) {
    arr = doc.to<JsonArray>();
  } else {
    arr = doc.as<JsonArray>();
  }

  bool updated = false;
  for (JsonObject obj : arr) {
    const char* s = obj["ssid"] | "";
    if (String(s) == String(ssid)) {
      obj["password"] = password;
      updated = true;
      break;
    }
  }
  if (!updated) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = ssid;
    o["password"] = password;
  }
  String out;
  serializeJson(arr, out);
  _saveCredentialsJson(out);

  Serial.printf("[WiFiManager] %s credential: %s\n", updated ? "Updated" : "Added", ssid);
}

void WiFiManager::deleteCredential(const char* ssid) {
  String json = _loadCredentialsJson();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, json)) {
    Serial.println("[WiFiManager] No credentials to delete.");
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (size_t i = 0; i < arr.size(); ++i) {
    if (String((const char*)arr[i]["ssid"]) == String(ssid)) {
      arr.remove(i);
      String out;
      serializeJson(arr, out);
      _saveCredentialsJson(out);
      Serial.printf("[WiFiManager] Deleted credential: %s\n", ssid);
      delay(100);
      Serial.println("[WiFiManager] Saving settings...");
      return;
    }
  }
  Serial.printf("[WiFiManager] SSID not found: %s\n", ssid);
}

void WiFiManager::clearCredentials() {
  _prefs.begin(NVS_NAMESPACE, false);
  _prefs.clear();
  _prefs.end();
  Serial.println("[WiFiManager] All credentials cleared!");
  delay(200);
  Serial.println("[WiFiManager] Saving settings...");
  delay(1000);
  ESP.restart();
}

void WiFiManager::listCredentialsToSerial() const {
  String json = _loadCredentialsJson();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, json)) {
    Serial.println("[WiFiManager] []");
    return;
  }
  Serial.println("[WiFiManager] Stored WiFi:");
  for (JsonObject obj : doc.as<JsonArray>()) {
    String s = obj["ssid"].as<String>();
    Serial.printf(" - %s\n", s.c_str());
  }
}

// -------------------- Connect (multiple) --------------------
bool WiFiManager::connectToWiFi() {
  String json = _loadCredentialsJson();
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, json)) {
    Serial.println("[WiFiManager] No saved credentials.");
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject cred : arr) {
    String ssid = cred["ssid"].as<String>();
    String pass = cred["password"].as<String>();

    Serial.print("Trying SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long startAttemptTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 12000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFiManager] Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return true;
    } else {
      Serial.println("[WiFiManager] Failed. Trying next...");
    }
  }
  Serial.println("[WiFiManager] Could not connect to any saved network.");
  return false;
}

bool WiFiManager::validateWiFiCredentials(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 8000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  bool ok = (WiFi.status() == WL_CONNECTED);
  WiFi.disconnect(true, true);
  return ok;
}

// -------------------- URL decode --------------------
String WiFiManager::_urlDecode(const String& str) {
  String out;
  out.reserve(str.length());
  for (size_t i = 0; i < str.length(); ++i) {
    char c = str[i];
    if (c == '+') out += ' ';
    else if (c == '%' && i + 2 < str.length()) {
      char h1 = str[i + 1];
      char h2 = str[i + 2];
      char hex[3] = {h1, h2, 0};
      out += (char) strtol(hex, nullptr, 16);
      i += 2;
    } else out += c;
  }
  return out;
}

// -------------------- Web Handlers --------------------
void WiFiManager::_handleRoot() {
  _server->sendHeader(F("Content-Encoding"), F("gzip"));
  _server->send_P(200, "text/html", (const char*)page_index, page_index_len);
}

void WiFiManager::_handleScan() {
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true); // async=false, show_hidden=true
  if (n <= 0) {
    _server->send(200, "application/json", "[]");
    Serial.println("[WiFiManager] No networks found.");
    return;
  }
  String json = "[";
  for (int i = 0; i < n; ++i) {
    if (i) json += ",";
    json += "{";
    json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"encryption\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    json += "}";
  }
  json += "]";
  _server->send(200, "application/json", json);
}

void WiFiManager::_handleSave() {
  // Accept from form POST; SSID/password can contain spaces; decode first
  String ssid = _urlDecode(_server->arg("ssid"));
  String password = _urlDecode(_server->arg("password"));

  if (ssid.length() == 0) {
    _server->send(400, "text/html", "<p>SSID cannot be empty!</p>");
    return;
  }

  // Optional: validate before saving
  if (validateWiFiCredentials(ssid.c_str(), password.c_str())) {
    addCredential(ssid.c_str(), password.c_str());
    _server->send(200, "text/html", "<p>Credentials validated & saved. Rebooting...</p>");
    delay(1000);
    WiFi.softAPdisconnect(true); // disconnect AP if running
    ESP.restart();
  } else {
    _server->send(200, "text/html", "<p>Invalid credentials. Please try again.</p>");
  }
}

void WiFiManager::_handleList() {
  // Return only SSIDs, not passwords
  DynamicJsonDocument out(2048);
  JsonArray arrOut = out.to<JsonArray>();

  String json = _loadCredentialsJson();
  DynamicJsonDocument doc(4096);
  if (!deserializeJson(doc, json)) {
    for (JsonObject obj : doc.as<JsonArray>()) {
      JsonObject o = arrOut.createNestedObject();
      o["ssid"] = obj["ssid"].as<const char*>();
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
  String ssid = _urlDecode(_server->arg("ssid"));
  deleteCredential(ssid.c_str());
  _server->send(200, "text/plain", "Deleted");
}

// -------------------- Set server if not using startAPMode --------------------
void WiFiManager::setServer(WebServer* server) {
  _server = server;
}

// -------------------- Start AP + bind routes --------------------
void WiFiManager::startAPMode(WebServer& server) {
  _server = &server;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_ap_ssid, _ap_password);
  Serial.println("[WiFiManager] AP Mode started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Start DNS server for captive portal
  _dnsServer.start(53, "*", WiFi.softAPIP());


  // Routes
  _server->on("/",        std::bind(&WiFiManager::_handleRoot,   this));
  _server->on("/scan",    std::bind(&WiFiManager::_handleScan,   this));
  _server->on("/list",    std::bind(&WiFiManager::_handleList,   this));
  _server->on("/delete",  std::bind(&WiFiManager::_handleDelete, this));
  _server->on("/save", HTTP_POST, std::bind(&WiFiManager::_handleSave, this));

  // Unknown route redirect to root page (captive portal)
  _server->onNotFound([this]() {
    _handleRoot(); // redirect to root/setup page (captive portal)
  });

  _server->begin();
}

// -------------------- Serial Commands --------------------
int WiFiManager::_splitArgsQuoted(const String& line, String outParts[], int maxParts) {
  // Tokenize preserving quoted segments "..."
  bool inQuotes = false;
  String cur;
  int count = 0;
  for (size_t i = 0; i < line.length(); ++i) {
    char c = line[i];
    if (c == '"') { inQuotes = !inQuotes; continue; }
    if (!inQuotes && isspace(c)) {
      if (cur.length()) {
        if (count < maxParts) outParts[count++] = cur;
        cur = "";
      }
    } else {
      cur += c;
    }
  }
  if (cur.length() && count < maxParts) outParts[count++] = cur;
  return count;
}

void WiFiManager::handleSerialCommands(Stream& io) {
  if (!io.available()) return;
  String line = io.readStringUntil('\n');
  line.trim();
  if (!line.length()) return;

  // Split into tokens
  const int MAXP = 8;
  String p[MAXP];
  int n = _splitArgsQuoted(line, p, MAXP);
  if (n == 0) return;

  String cmd = p[0];
  cmd.toUpperCase();

  if (cmd == "ADD") {
    if (n >= 2) {
      String ssid = (n >= 2) ? p[1] : "";
      String pass = (n >= 3) ? p[2] : "";
      addCredential(ssid.c_str(), pass.c_str());
      io.println("[WiFiManager] ADD done.");
      // go to check credentials and connect if valid
      if (validateWiFiCredentials(ssid.c_str(), pass.c_str())) {
        io.println("[WiFiManager] Valid credentials. Rebooting...");
        delay(1000);
        ESP.restart();
      } else {
        io.println("[WiFiManager] Invalid credentials.");
      }
    } else {
      io.println("Usage: ADD \"SSID with spaces\" \"password with spaces\"");
    }
  } else if (cmd == "DEL" || cmd == "DELETE" || cmd == "REMOVE") {
    if (n >= 2) {
      deleteCredential(p[1].c_str());
    } else {
      io.println("Usage: DEL \"SSID with spaces\"");
    }
  } else if (cmd == "CLEAR") {
    clearCredentials();
  } else if (cmd == "LIST") {
    listCredentialsToSerial();
  } else if (cmd == "HELP") {
    io.println("Commands:");
    io.println("  ADD \"SSID with spaces\" \"password with spaces\"");
    io.println("  DEL \"SSID with spaces\"");
    io.println("  LIST");
    io.println("  CLEAR");
    io.println("  CONNECT \"SSID with spaces\" [\"password with spaces\"]");
    io.println("  JSON - get credentials in JSON format");
    io.println("  REBOOT - reboot the device");
  } else if (cmd == "connect" || cmd == "CONNECT") {
    if (n >= 2) {
      String ssid = p[1];
      String pass = (n >= 3) ? p[2] : "";
      if (validateWiFiCredentials(ssid.c_str(), pass.c_str())) {
        addCredential(ssid.c_str(), pass.c_str());
        io.println("[WiFiManager] CONNECT done. Rebooting...");
        delay(1000);
        ESP.restart();
      } else {
        io.println("[WiFiManager] Invalid credentials.");
      }
    } else {
      io.println("Usage: CONNECT \"SSID\" [\"password\"]");
    }
  } else if (cmd == "JSON") {
    io.println(getCredentialsJson());
  } else if (cmd == "REBOOT" || cmd == "reboot") {
    io.println("[WiFiManager] Rebooting...");
    delay(1000);
    ESP.restart();
  } else if (cmd == "STATUS" || cmd == "status") {
    io.printf("AP SSID: %s\n", _ap_ssid);
    io.printf("AP Password: %s\n", _ap_password);
    io.printf("Connected to: %s\n", WiFi.SSID().c_str());
    io.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    listCredentialsToSerial();
  } else {
    io.println("Unknown. Try HELP");
  }
}

void WiFiManager::process() {
  _dnsServer.processNextRequest();
  if (_server) {
    _server->handleClient(); // Handle web requests
  }
}