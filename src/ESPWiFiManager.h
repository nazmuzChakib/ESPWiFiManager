/**
 * @file ESPWiFiManager.h
 * @brief A non-blocking, multi-credential WiFi Manager for ESP32 and ESP8266.
 *
 * Features:
 *  • Smart Connect — scans the air, sorts by RSSI, connects to the strongest
 *    known network first.
 *  • Multi-credential storage — up to 10 networks (ESP32) / 5 (ESP8266).
 *  • Captive Portal fallback — serves a web UI for entering new credentials
 *    when all saved networks fail.
 *  • Supports both standard WebServer and ESPAsyncWebServer backends.
 *  • Serial command interface for runtime credential management.
 *
 * ── Configuration ─────────────────────────────────────────────────────────
 *  Open  src/ESPWiFiManagerConfig.h  and uncomment the relevant define(s).
 *  That is the ONLY file you need to edit — your sketch stays untouched.
 *
 * ── Basic usage ───────────────────────────────────────────────────────────
 *  @code
 *  #include <ESPWiFiManager.h>
 *
 *  WebServer      server(80);          // or AsyncWebServer for async mode
 *  WiFiManager    wm("Portal", "pass");
 *
 *  void setup() {
 *    wm.begin();
 *    wm.connectToWiFi();
 *  }
 *  void loop() {
 *    wm.process();
 *    if (wm.getState() == WIFI_STATE_FAILED) wm.startAPMode(server);
 *  }
 *  @endcode
 * ─────────────────────────────────────────────────────────────────────────
 */

#ifndef ESP_WIFI_MANAGER_H
#define ESP_WIFI_MANAGER_H

// Config is included first so every #ifdef in this header and in the .cpp
// sees the same set of defines. Edit ESPWiFiManagerConfig.h to configure.
#include "ESPWiFiManagerConfig.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <vector>
#include <algorithm>
#include "page_index.h"

// ── AsyncWebServer backend ────────────────────────────────────────────────
#ifdef WIFIMANAGER_USE_ASYNC_WEBSERVER
  #include <ESPAsyncWebServer.h>
  typedef AsyncWebServer ESP_WebServer;
#endif

// ── Platform headers & storage backend ───────────────────────────────────
#if defined(ESP32)
  #include <WiFi.h>
  #include <Preferences.h>
  #ifndef WIFIMANAGER_USE_ASYNC_WEBSERVER
    #include <WebServer.h>
    typedef WebServer ESP_WebServer;
  #endif

#elif defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <EEPROM.h>
  #ifndef WIFIMANAGER_USE_ASYNC_WEBSERVER
    #include <ESP8266WebServer.h>
    typedef ESP8266WebServer ESP_WebServer;
  #endif

#else
  #error "ESPWiFiManager: Only ESP32 and ESP8266 are supported."
#endif

// ── State machine ─────────────────────────────────────────────────────────
/**
 * @brief Operational states of the WiFiManager.
 */
enum WiFiState {
  WIFI_STATE_IDLE,        ///< No active operation
  WIFI_STATE_SCANNING,    ///< Scanning for available networks
  WIFI_STATE_CONNECTING,  ///< Attempting to connect to a matched network
  WIFI_STATE_CONNECTED,   ///< Successfully connected to a network
  WIFI_STATE_AP_MODE,     ///< Running as Access Point (captive portal active)
  WIFI_STATE_FAILED       ///< All connection attempts exhausted
};

// ═══════════════════════════════════════════════════════════════════════════
//  CLASS DECLARATION
// ═══════════════════════════════════════════════════════════════════════════
class WiFiManager {
public:
  /**
   * @param ap_ssid     SSID broadcast when falling back to AP mode.
   * @param ap_password Password for the fallback Access Point.
   */
  WiFiManager(const char* ap_ssid, const char* ap_password);

  /**
   * Initialises internals and registers hardware disconnect event handlers.
   * Call once in setup() before connectToWiFi().
   */
  void begin();

  /**
   * Starts a non-blocking WiFi scan followed by a smart-connect attempt.
   * Transitions state machine to WIFI_STATE_SCANNING.
   */
  void connectToWiFi();

  /**
   * Main processing loop — MUST be called on every iteration of loop().
   * Handles: state transitions, captive-portal DNS, and (standard WebServer
   * only) client request dispatch.
   */
  void process();

  /**
   * Launches the WiFi Access Point and registers all captive-portal routes
   * on the provided server instance.
   *
   * @param server  A pre-created WebServer or AsyncWebServer instance.
   *                Pass by reference; the manager keeps a pointer internally.
   */
  void startAPMode(ESP_WebServer& server);

  /**
   * Registers an externally-created server so process() will call
   * handleClient() on it (standard WebServer mode only).
   * Useful after a successful STA connection to keep your own routes alive.
   *
   * @param server  Pointer to your application's server instance.
   */
  void setServer(ESP_WebServer* server);

  /** @return The current WiFiState value. */
  WiFiState getState() const;

  // ── Credential management ──────────────────────────────────────────────

  /** Adds a new credential or updates the password if the SSID exists. */
  void addCredential(const char* ssid, const char* password);

  /** Removes a saved credential by SSID. No-op if not found. */
  void deleteCredential(const char* ssid);

  /** Erases all saved credentials from persistent storage. */
  void clearCredentials();

  /** Prints all saved SSIDs to the Serial monitor. */
  void listCredentialsToSerial() const;

  /** @return JSON string of all saved credentials (SSID + password). */
  String getCredentialsJson() const;

  /**
   * Parses and executes a text-based command.  Useful for Serial / BLE CLIs.
   *
   * Supported commands:
   *   ADD "SSID" "PASS"  — add or update credential
   *   DEL "SSID"         — delete credential
   *   LIST               — print all saved SSIDs
   *   CLEAR              — erase all credentials
   *   STATUS             — print current state and IP address
   *
   * @param cmdLine  Full command string (trimming handled internally).
   * @param io       Stream to write responses to (default: Serial).
   */
  void executeCommand(String cmdLine, Stream& io = Serial);

private:
  // ── Configuration constants ───────────────────────────────────────────
  static constexpr unsigned long CONNECTION_TIMEOUT_MS = 12000UL;
  static constexpr unsigned long SCAN_TIMEOUT_MS       = 15000UL;
  static constexpr unsigned long RESTART_DELAY_MS      =   600UL;

#if defined(ESP32)
  static constexpr size_t MAX_CREDS = 10;
#elif defined(ESP8266)
  static constexpr size_t MAX_CREDS = 5;
#endif

  // ── Identity ──────────────────────────────────────────────────────────
  const char* _ap_ssid;
  const char* _ap_password;

  // ── State ─────────────────────────────────────────────────────────────
  WiFiState     _currentState       = WIFI_STATE_IDLE;
  bool          _apModeActive       = false;
  bool          _disconnectTriggered = false;
  bool          _restartPending     = false;
  unsigned long _restartAt          = 0;
  unsigned long _startAttemptTime   = 0;
  unsigned long _scanStartTime      = 0;

  // ── Smart-connect bookkeeping ─────────────────────────────────────────
  std::vector<String> _matchedSSIDs;
  std::vector<String> _matchedPasses;
  int  _currentNetworkIndex = 0;
  bool _isConnecting        = false;

  // ── Server / DNS ──────────────────────────────────────────────────────
  ESP_WebServer* _server = nullptr;
  DNSServer      _dnsServer;

#if defined(ESP8266)
  WiFiEventHandler _disconnectHandler; ///< Must live in class scope on ESP8266
#endif

  // ── Persistent storage ────────────────────────────────────────────────
#if defined(ESP32)
  mutable Preferences _prefs;
  static constexpr const char* NVS_NAMESPACE = "wifi-config";
  static constexpr const char* NVS_KEY_MULTI = "multi";
#elif defined(ESP8266)
  static constexpr int EEPROM_SIZE = 1024;
#endif

  // ── Private methods ───────────────────────────────────────────────────
  String _loadData() const;
  void   _saveData(const String& data);
  void   _setupEventHandlers();
  void   _checkScanStatus();
  void   _startNextConnection();
  void   _checkConnectionStatus();
  void   _printHelp() const;
  static String _urlDecode(const String& str);
  static int    _splitArgsQuoted(const String& line, String out[], int maxParts);

  // ── Web handlers (signature depends on backend) ───────────────────────
#ifdef WIFIMANAGER_USE_ASYNC_WEBSERVER
  void _handleRoot  (AsyncWebServerRequest* req);
  void _handleScan  (AsyncWebServerRequest* req);
  void _handleSave  (AsyncWebServerRequest* req);
  void _handleList  (AsyncWebServerRequest* req);
  void _handleDelete(AsyncWebServerRequest* req);
#else
  void _handleRoot();
  void _handleScan();
  void _handleSave();
  void _handleList();
  void _handleDelete();
#endif
};

#endif // ESP_WIFI_MANAGER_H
