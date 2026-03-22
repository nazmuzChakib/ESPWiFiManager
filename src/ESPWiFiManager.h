#ifndef ESP_WIFI_MANAGER_H
#define ESP_WIFI_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <vector>
#include <algorithm>

// ---------------- Cross-Platform Support & Memory Limit ----------------
#if defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
  #include <Preferences.h>
  typedef WebServer ESP_WebServer;
  static constexpr size_t MAX_CREDENTIALS = 10; // ESP32 এর জন্য সর্বোচ্চ ১০টি
#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  #include <EEPROM.h>
  typedef ESP8266WebServer ESP_WebServer;
  static constexpr size_t MAX_CREDENTIALS = 5;  // ESP8266 এর জন্য সর্বোচ্চ ৫টি
#else
  #error "This library only supports ESP32 and ESP8266"
#endif

extern const unsigned char page_index[]; // From page_index.h
extern unsigned int page_index_len;

// ---------------- State Machine Enum ----------------
/**
 * @brief Represents the current state of the WiFi connection process.
 */
enum WiFiState {
  WIFI_STATE_IDLE,         /**< No active operation */
  WIFI_STATE_SCANNING,     /**< Scanning for available networks */
  WIFI_STATE_CONNECTING,   /**< Attempting to connect to a matched network */
  WIFI_STATE_CONNECTED,    /**< Successfully connected to a network */
  WIFI_STATE_AP_MODE,      /**< Running in Access Point (AP) mode */
  WIFI_STATE_FAILED        /**< Connection failed after all attempts */
};

/**
 * @brief Manages WiFi connections, credentials, and Access Point behavior.
 */
class WiFiManager {
public:
  /**
   * @brief Constructs a new WiFiManager object.
   * 
   * @param ap_ssid The SSID for the Access Point.
   * @param ap_password The password for the Access Point.
   */
  WiFiManager(const char* ap_ssid, const char* ap_password);

  /**
   * @brief Initializes the WiFiManager, loading saved credentials.
   */
  void begin();
  
  /**
   * @brief Initiates a non-blocking WiFi connection using Smart Scan.
   */
  void connectToWiFi(); 
  
  /**
   * @brief Main processing loop. Must be called frequently in `loop()`.
   */
  void process();

  /**
   * @brief Starts the Access Point mode and configures the web server routes.
   * 
   * @param server Reference to the web server instance.
   */
  void startAPMode(ESP_WebServer& server);

  /**
   * @brief Sets the web server instance for routing.
   * 
   * @param server Pointer to the web server instance.
   */
  void setServer(ESP_WebServer* server);

  /**
   * @brief Gets the current state of the WiFi manager.
   * 
   * @return WiFiState The current WiFi connection state.
   */
  WiFiState getState() const;

  // Credential Management

  /**
   * @brief Adds or updates a WiFi credential.
   * 
   * @param ssid The SSID of the network.
   * @param password The password for the network.
   */
  void addCredential(const char* ssid, const char* password);

  /**
   * @brief Deletes a saved WiFi credential.
   * 
   * @param ssid The SSID of the credential to delete.
   */
  void deleteCredential(const char* ssid);

  /**
   * @brief Clears all saved WiFi credentials.
   */
  void clearCredentials();

  /**
   * @brief Prints the list of saved credentials to the Serial monitor.
   */
  void listCredentialsToSerial() const;

  /**
   * @brief Returns a JSON string containing the saved credentials.
   * 
   * @return String JSON representation of the credentials.
   */
  String getCredentialsJson() const;

  /**
   * @brief Processes serial commands for managing the WiFiManager.
   * 
   * @param io Stream object to read from and write to (default is Serial).
   */
  void executeCommand(String cmdLine, Stream& io = Serial);

private:
  const char* _ap_ssid;
  const char* _ap_password;
  
  ESP_WebServer* _server = nullptr;
  DNSServer _dnsServer;

  // State Tracking
  WiFiState _currentState = WIFI_STATE_IDLE;
  unsigned long _startAttemptTime = 0;
  const unsigned long _connectionTimeout = 12000; // 12 seconds max per network
  
  // Smart Connect Variables
  std::vector<String> _matchedSSIDs;
  std::vector<String> _matchedPasses;
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

  /**
   * @brief Loads the credentials data from persistent storage.
   * 
   * @return String JSON data containing the credentials.
   */
  String _loadData() const;

  /**
   * @brief Saves the credentials data to persistent storage.
   * 
   * @param data JSON string to save.
   */
  void _saveData(const String& data);

  // Core Connection Logic

  /**
   * @brief Checks the WiFi scan status and matches against saved credentials.
   */
  void _checkScanStatus();

  /**
   * @brief Attempts connection to the next matched network.
   */
  void _startNextConnection();

  /**
   * @brief Monitors the ongoing connection attempt.
   */
  void _checkConnectionStatus();

  // Utilities

  /**
   * @brief Decodes a URL-encoded string.
   * 
   * @param str The URL-encoded string.
   * @return String The decoded string.
   */
  static String _urlDecode(const String& str);

  /**
   * @brief Splits a command line string into parts, respecting quotes.
   * 
   * @param line The command line string.
   * @param outParts Array to hold the resulting parts.
   * @param maxParts Maximum number of parts to split into.
   * @return int The number of parts successfully separated.
   */
  static int _splitArgsQuoted(const String& line, String outParts[], int maxParts);

  // Web Handlers

  /**
   * @brief Handles the root web server route.
   */
  void _handleRoot();

  /**
   * @brief Handles the WiFi scan request route.
   */
  void _handleScan();

  /**
   * @brief Handles the credential save request route.
   */
  void _handleSave();

  /**
   * @brief Handles the credential list request route.
   */
  void _handleList();

  /**
   * @brief Handles the credential delete request route.
   */
  void _handleDelete();

  /**
   * @brief Prints the available serial commands.
   */
  void _printHelp();
};

#endif // ESP_WIFI_MANAGER_H