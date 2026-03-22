#ifndef ESP_WIFI_MANAGER_H
#define ESP_WIFI_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>

// ---------------- Cross-Platform Support ----------------
#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <Preferences.h>
  typedef WebServer ESP_WebServer;
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <EEPROM.h>
  typedef ESP8266WebServer ESP_WebServer;
#else
  #error "This library only supports ESP32 and ESP8266"
#endif

extern const unsigned char page_index[]; // From page_index.h
extern unsigned int page_index_len;

// ---------------- State Machine Enum ----------------
enum WiFiState {
  WIFI_STATE_IDLE,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_AP_MODE,
  WIFI_STATE_FAILED
};

class WiFiManager {
public:
  WiFiManager(const char* ap_ssid, const char* ap_password);

  void begin();
  
  // Non-blocking connect initiation
  void connectToWiFi(); 
  
  // Call this frequently in your main loop()
  void process();

  void startAPMode(ESP_WebServer& server);
  void setServer(ESP_WebServer* server);

  // Get current state of the WiFi manager
  WiFiState getState() const;

  // Credential Management
  void addCredential(const char* ssid, const char* password);
  void deleteCredential(const char* ssid);
  void clearCredentials();
  void listCredentialsToSerial() const;
  String getCredentialsJson() const;

  void handleSerialCommands(Stream& io = Serial);

private:
  const char* _ap_ssid;
  const char* _ap_password;
  
  ESP_WebServer* _server = nullptr;
  DNSServer _dnsServer;

  // State Tracking
  WiFiState _currentState = WIFI_STATE_IDLE;
  unsigned long _startAttemptTime = 0;
  const unsigned long _connectionTimeout = 12000; // 12 seconds per network
  
  // Credential iteration for connection
  int _currentNetworkIndex = 0;
  bool _isConnecting = false;

  // Platform specific storage details
#if defined(ESP32)
  mutable Preferences _prefs;
  static constexpr const char* NVS_NAMESPACE = "wifi-config";
  static constexpr const char* NVS_KEY_MULTI = "multi";
#elif defined(ESP8266)
  static constexpr int EEPROM_SIZE = 1024;
#endif

  // Storage Helpers
  String _loadData() const;
  void _saveData(const String& data);

  // Core Connection Logic
  void _startNextConnection();
  void _checkConnectionStatus();

  // Utilities
  static String _urlDecode(const String& str);
  static int _splitArgsQuoted(const String& line, String outParts[], int maxParts);

  // Web Handlers
  void _handleRoot();
  void _handleScan();
  void _handleSave();
  void _handleList();
  void _handleDelete();

  void _printHelp();
};

#endif // ESP_WIFI_MANAGER_H