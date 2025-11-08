#ifndef ESP_WIFI_MANAGER_H
#define ESP_WIFI_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <DNSServer.h>

extern const char wifiSetupPage[]; // from wifi_manager_web.h (PROGMEM HTML)

class WiFiManager {
public:
  // Constructor (AP SSID/Password). No static IP involved.
  WiFiManager(const char* ap_ssid, const char* ap_password);

  // Try connecting with saved (multiple) credentials; returns true if connected.
  bool connectToWiFi();

  // Optional quick validator (used by /save flow)
  bool validateWiFiCredentials(const char* ssid, const char* password);

  // Start AP + Web UI/REST endpoints (uses provided WebServer instance)
  void startAPMode(WebServer& server);

  // ---- Managing credentials ----
  void addCredential(const char* ssid, const char* password); // add or update
  void deleteCredential(const char* ssid);
  void clearCredentials();                                     // public as requested
  void listCredentialsToSerial() const;                        // pretty print to Serial
  String getCredentialsJson() const;                           // returns full JSON array [{"ssid":"...","password":"..."}]
  void setServer(WebServer* server);                           // set server if not using startAPMode
  void process();                                              // call this in loop() to handle DNS and web requests

  // ---- Serial commands handler (quotes-aware) ----
  void handleSerialCommands(Stream& io = Serial);

private:
  // AP config
  const char* _ap_ssid;
  const char* _ap_password;

  // add dns 
  DNSServer _dnsServer; // dns for captive portal

  // Storage
  mutable Preferences _prefs; // mutable to allow const getters to open NVS
  static constexpr const char* NVS_NAMESPACE = "wifi-config";
  static constexpr const char* NVS_KEY_MULTI = "multi";

  // Helpers for JSON storage
  String _loadCredentialsJson() const;           // returns JSON array (string)
  void   _saveCredentialsJson(const String& js); // stores JSON array

  // URL decode (for /save form handling safety)
  static String _urlDecode(const String& str);

  // Tokenizer for serial commands supporting "quoted strings"
  static int _splitArgsQuoted(const String& line, String outParts[], int maxParts);

  // Web handlers (bound with std::bind)
  void _handleRoot();
  void _handleScan();
  void _handleSave();
  void _handleList();     // list saved SSIDs (no passwords)
  void _handleDelete();   // delete by ssid

  // the server reference (only valid after startAPMode)
  WebServer* _server = nullptr;
};

#endif // ESP_WIFI_MANAGER_H