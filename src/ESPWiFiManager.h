/**
 * @file ESPWiFiManager.h
 * @brief A modern, non-blocking, multi-credential WiFi Manager for ESP32 and ESP8266.
 *
 * Features:
 *  • Smart Connect — scans, sorts by RSSI, connects to the strongest known
 *    network first.
 *  • Multi-credential storage — up to 10 networks (configurable) using NVS
 *    Preferences on both ESP32 and ESP8266.
 *  • Auto AP Fallback — automatically starts captive portal when all saved
 *    networks are unreachable. No extra sketch code required.
 *  • Dual Mode (AP+STA) — portal runs while background reconnection scans
 *    for known networks. Automatically reconnects and closes the portal.
 *  • Exponential backoff reconnection — polite, battery-friendly retries.
 *  • Static IP support — stored in Preferences, applied before each connect.
 *  • Event callbacks — register lambdas for state changes, connect/disconnect
 *    events, and portal lifecycle events.
 *  • Dynamic logging — configurable log level and output stream.
 *  • Low-level controls — Tx power, WiFi sleep mode, AP channel/client limits.
 *  • Supports both standard WebServer and ESPAsyncWebServer backends.
 *  • Serial command interface for runtime credential management.
 *
 * ── Configuration ─────────────────────────────────────────────────────────
 *  Open  src/ESPWiFiManagerConfig.h  to configure build-time defaults.
 *  That is the ONLY file you need to edit — your sketch stays untouched.
 *
 * ── Basic usage ───────────────────────────────────────────────────────────
 *  @code
 *  #include <ESPWiFiManager.h>
 *
 *  AsyncWebServer server(80);  // or WebServer server(80)
 *  WiFiManager    wm("Portal", "pass");
 *
 *  void setup() {
 *    wm.begin();
 *    // onConnected / onAPStarted callbacks fire automatically — no manual
 *    // state-checking required in loop().
 *  }
 *  void loop() {
 *    wm.process();
 *  }
 *  @endcode
 * ─────────────────────────────────────────────────────────────────────────
 */

#ifndef ESP_WIFI_MANAGER_H
#define ESP_WIFI_MANAGER_H

// Config pulled in first so every #ifdef sees the same defines.
#include "ESPWiFiManagerConfig.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <functional>
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
  #include <Preferences.h>       // ESP8266 core ≥ 3.0 ships Preferences
  #ifndef WIFIMANAGER_USE_ASYNC_WEBSERVER
    #include <ESP8266WebServer.h>
    typedef ESP8266WebServer ESP_WebServer;
  #endif

#else
  #error "ESPWiFiManager: Only ESP32 and ESP8266 are supported."
#endif

// ═══════════════════════════════════════════════════════════════════════════
//  ENUMS & TYPES
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Operational states of the WiFiManager state machine.
 */
enum WiFiState {
  WIFI_STATE_IDLE,        ///< No active operation
  WIFI_STATE_SCANNING,    ///< Non-blocking scan in progress
  WIFI_STATE_CONNECTING,  ///< Attempting to connect to a station network
  WIFI_STATE_CONNECTED,   ///< Successfully connected to a station network
  WIFI_STATE_AP_MODE,     ///< Captive portal (soft-AP) is active
  WIFI_STATE_FAILED       ///< All connection attempts exhausted
};

/**
 * @brief Log verbosity levels.
 */
enum WiFiLogLevel {
  WIFI_LOG_NONE  = 0,  ///< Silent
  WIFI_LOG_ERROR = 1,  ///< Errors only
  WIFI_LOG_INFO  = 2,  ///< Normal operational messages  (default)
  WIFI_LOG_DEBUG = 3   ///< Verbose internal detail
};

/**
 * @brief Static IP configuration bundle.
 *        All fields are zero-initialised (DHCP) by default.
 */
struct StaticIPConfig {
  IPAddress ip;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;
  bool      enabled = false;
};

// ═══════════════════════════════════════════════════════════════════════════
//  CLASS DECLARATION
// ═══════════════════════════════════════════════════════════════════════════
class WiFiManager {
public:

  // ── Constructor / destructor ────────────────────────────────────────────
  /**
   * @param ap_ssid     SSID broadcast when falling back to AP mode.
   * @param ap_password Password for the fallback Access Point (min 8 chars
   *                    for WPA2; use "" for an open AP).
   */
  WiFiManager(const char* ap_ssid, const char* ap_password);
  ~WiFiManager() = default;

  // ── Initialisation ──────────────────────────────────────────────────────
  /**
   * Initialises internals, registers hardware event handlers, and
   * (if auto AP fallback is enabled) kicks off the first connection attempt.
   * Call once in setup().
   */
  void begin();

  // ── Core loop ───────────────────────────────────────────────────────────
  /**
   * Main state-machine pump — MUST be called on every iteration of loop().
   * Handles: scanning, connecting, reconnection backoff, background scans,
   * AP timeout, captive-portal DNS, and (standard WebServer only) client dispatch.
   */
  void process();

  // ── Station connection ───────────────────────────────────────────────────
  /**
   * Starts a non-blocking WiFi scan followed by a smart-connect attempt.
   * Transitions the state machine to WIFI_STATE_SCANNING.
   * Normally called automatically by begin() when autoAPFallback is enabled.
   */
  void connectToWiFi();

  // ── AP / Portal ──────────────────────────────────────────────────────────
  /**
   * Launches the WiFi Access Point in dual AP+STA mode and registers all
   * captive-portal routes on the provided server instance.
   *
   * @param server  A pre-created WebServer or AsyncWebServer instance.
   *                The manager keeps an internal pointer to it.
   */
  void startAPMode(ESP_WebServer& server);

  /**
   * Stops the captive portal, restores STA-only mode, and triggers a fresh
   * scan/connect cycle.
   */
  void stopAPMode();

  /**
   * Registers an externally-created server so process() will call
   * handleClient() on it (standard WebServer only).
   * Useful after a successful STA connection to keep your own routes alive.
   *
   * @param server  Pointer to your application's server instance.
   */
  void setServer(ESP_WebServer* server);

  // ── State query ──────────────────────────────────────────────────────────
  /** @return The current WiFiState value. */
  WiFiState getState() const;

  /** @return true if the device is connected to a station network. */
  bool isConnected() const;

  /** @return The IP address of the STA interface (0.0.0.0 if not connected). */
  IPAddress getLocalIP() const;

  /** @return The IP address of the soft-AP interface (0.0.0.0 if not active). */
  IPAddress getAPIP() const;

  // ── Credential management ────────────────────────────────────────────────
  /** Adds a new credential or updates the password if the SSID exists. */
  void addCredential(const char* ssid, const char* password);

  /** Removes a saved credential by SSID. No-op if not found. */
  void deleteCredential(const char* ssid);

  /** Erases all saved credentials from persistent storage. */
  void clearCredentials();

  /** Prints all saved SSIDs to the configured log stream. */
  void listCredentialsToSerial() const;

  /** @return JSON string of all saved credentials (SSID + password). */
  String getCredentialsJson() const;

  // ── Static IP ────────────────────────────────────────────────────────────
  /**
   * Configures a static IP address applied before every connection attempt.
   * Set dns1/dns2 to IPAddress() to skip custom DNS.
   */
  void setStaticIP(IPAddress ip, IPAddress gateway, IPAddress subnet,
                   IPAddress dns1 = IPAddress(),
                   IPAddress dns2 = IPAddress());

  /** Reverts to DHCP and removes stored static IP config. */
  void clearStaticIP();

  /** @return true if a static IP is currently configured. */
  bool hasStaticIP() const;

  // ── Behaviour tuning ────────────────────────────────────────────────────
  /**
   * Enable / disable automatic AP fallback.
   * When enabled (default: true), the manager starts the captive portal
   * automatically after all connection attempts fail — no sketch code needed.
   *
   * @param enable   true = auto AP fallback on.
   * @param server   Server instance to use for the portal.  Required when
   *                 enable=true. Pass nullptr to disable.
   */
  void setAutoAPFallback(bool enable, ESP_WebServer* server = nullptr);

  /**
   * Enable / disable background reconnection.
   * When enabled (default: true), after a disconnect the manager waits for
   * the backoff interval and then retries without any sketch code.
   */
  void setBackgroundReconnect(bool enable);

  /**
   * Set the AP timeout.  If the portal is active for longer than this value
   * with no STA client connected and no credentials submitted, the manager
   * will shut down the AP and resume scanning.
   * Set to 0 to disable (default).
   *
   * @param timeoutMs  Timeout in milliseconds.
   */
  void setAPTimeout(uint32_t timeoutMs);

  // ── Low-level WiFi controls ──────────────────────────────────────────────
  /**
   * Configure the soft-AP radio properties. Call before startAPMode().
   *
   * @param channel    WiFi channel 1–13 (default from config).
   * @param hidden     Broadcast SSID if false (default), hide if true.
   * @param maxClients Maximum simultaneous AP clients (default from config).
   */
  void setAPConfig(uint8_t channel, bool hidden = false,
                   uint8_t maxClients = WIFIMANAGER_AP_MAX_CLIENTS);

  /**
   * Set the WiFi transmission power.
   * Range: 0.0 dBm – 20.5 dBm (hardware dependent).
   */
  void setTxPower(float dbm);

  /**
   * Enable or disable the WiFi modem sleep mode.
   * Disabling sleep reduces latency at the cost of higher current draw.
   */
  void setWiFiSleep(bool enable);

  // ── Logging ──────────────────────────────────────────────────────────────
  /**
   * Set the minimum severity required to emit a log message.
   * Default is WIFI_LOG_INFO (or WIFIMANAGER_DEFAULT_LOG_LEVEL from config).
   */
  void setLogLevel(WiFiLogLevel level);

  /**
   * Redirect library log output to any Stream (e.g. Serial1, SoftwareSerial,
   * TelnetStream).  Defaults to Serial.
   */
  void setLogStream(Stream& stream);

  /**
   * Provide a custom log handler.  When set, the handler is called instead
   * of writing to the log stream.
   *
   * @param handler  Callable with signature void(WiFiLogLevel, const char* msg)
   */
  void setLogHandler(std::function<void(WiFiLogLevel, const char*)> handler);

  // ── Event callbacks ──────────────────────────────────────────────────────
  /**
   * Fires whenever the internal state changes.
   * @param cb  void(WiFiState oldState, WiFiState newState)
   */
  void onStateChange(std::function<void(WiFiState, WiFiState)> cb);

  /**
   * Fires once the ESP successfully joins a station network.
   * @param cb  void(const String& ssid, IPAddress ip)
   */
  void onStationConnected(std::function<void(const String&, IPAddress)> cb);

  /**
   * Fires when the ESP loses its station connection.
   * @param cb  void(int reason)   — reason code from the WiFi stack
   */
  void onStationDisconnected(std::function<void(int)> cb);

  /**
   * Fires when the captive portal (soft-AP) is started.
   * @param cb  void(const String& apSSID, IPAddress apIP)
   */
  void onAPModeStarted(std::function<void(const String&, IPAddress)> cb);

  /**
   * Fires when the captive portal is shut down (either by timeout, user save,
   * or a manual stopAPMode() call).
   */
  void onAPModeStopped(std::function<void()> cb);

  /**
   * Fires whenever a credential is added, updated, or deleted.
   */
  void onCredentialsChanged(std::function<void()> cb);

  // ── Serial command interface ─────────────────────────────────────────────
  /**
   * Parses and executes a text-based command.  Useful for Serial / BLE CLIs.
   *
   * Supported commands:
   *   ADD "SSID" "PASS"  — add or update credential
   *   DEL "SSID"         — delete credential
   *   LIST               — print all saved SSIDs
   *   CLEAR              — erase all credentials
   *   STATUS             — print current state and IP address
   *   RECONNECT          — force an immediate reconnect cycle
   *   APSTART            — force-start the captive portal (if server registered)
   *   APSTOP             — force-stop the captive portal
   *   LOGLEVEL <0-3>     — change log verbosity at runtime
   *
   * @param cmdLine  Full command string (trimming handled internally).
   * @param io       Stream to write responses to (default: Serial).
   */
  void executeCommand(String cmdLine, Stream& io = Serial);

private:
  // ── Build-time constants (from config, overridable per instance) ─────────
  static constexpr size_t        MAX_CREDS         = WIFIMANAGER_MAX_CREDENTIALS;
  static constexpr unsigned long CONNECTION_TIMEOUT = WIFIMANAGER_CONNECTION_TIMEOUT_MS;
  static constexpr unsigned long SCAN_TIMEOUT       = WIFIMANAGER_SCAN_TIMEOUT_MS;
  static constexpr unsigned long RECONNECT_BASE     = WIFIMANAGER_RECONNECT_BASE_MS;
  static constexpr unsigned long RECONNECT_MAX      = WIFIMANAGER_RECONNECT_MAX_MS;
  static constexpr unsigned long RESTART_DELAY      = WIFIMANAGER_RESTART_DELAY_MS;
  static constexpr unsigned long BG_SCAN_INTERVAL   = WIFIMANAGER_BG_SCAN_INTERVAL_MS;
  static constexpr const char*   NVS_NAMESPACE       = "wifi-cfg";
  static constexpr const char*   NVS_KEY_CREDS       = "creds";
  static constexpr const char*   NVS_KEY_STATIC_IP   = "static-ip";

  // ── AP identity ──────────────────────────────────────────────────────────
  const char* _ap_ssid;
  const char* _ap_password;

  // ── AP radio config ──────────────────────────────────────────────────────
  uint8_t _apChannel    = WIFIMANAGER_AP_CHANNEL;
  bool    _apHidden     = false;
  uint8_t _apMaxClients = WIFIMANAGER_AP_MAX_CLIENTS;

  // ── State ────────────────────────────────────────────────────────────────
  WiFiState     _currentState       = WIFI_STATE_IDLE;
  bool          _apModeActive       = false;
  bool          _disconnectTriggered = false;
  bool          _restartPending     = false;
  unsigned long _restartAt          = 0;
  unsigned long _startAttemptTime   = 0;
  unsigned long _scanStartTime      = 0;
  int           _lastDisconnectReason = 0;

  // ── Smart-connect bookkeeping ────────────────────────────────────────────
  std::vector<String> _matchedSSIDs;
  std::vector<String> _matchedPasses;
  int  _currentNetworkIndex = 0;
  bool _isConnecting        = false;

  // ── Reconnection backoff ─────────────────────────────────────────────────
  bool          _bgReconnectEnabled = true;
  unsigned long _reconnectInterval  = RECONNECT_BASE;  // current backoff value
  unsigned long _nextReconnectAt    = 0;
  bool          _reconnectScheduled = false;

  // ── Auto AP fallback ─────────────────────────────────────────────────────
  bool           _autoAPEnabled   = true;
  ESP_WebServer* _autoAPServer    = nullptr;

  // ── AP timeout ───────────────────────────────────────────────────────────
  uint32_t      _apTimeoutMs  = WIFIMANAGER_AP_TIMEOUT_MS;
  unsigned long _apStartedAt  = 0;

  // ── Background scan while AP is active ──────────────────────────────────
  bool          _bgScanPending  = false;
  unsigned long _bgScanAt       = 0;

  // ── Server / DNS ─────────────────────────────────────────────────────────
  ESP_WebServer* _server = nullptr;
  DNSServer      _dnsServer;

  // ── Static IP ────────────────────────────────────────────────────────────
  StaticIPConfig _staticIP;

  // ── Persistent storage ───────────────────────────────────────────────────
  mutable Preferences _prefs;

  // ── Logging ──────────────────────────────────────────────────────────────
  WiFiLogLevel  _logLevel  = static_cast<WiFiLogLevel>(WIFIMANAGER_DEFAULT_LOG_LEVEL);
  Stream*       _logStream = &Serial;
  std::function<void(WiFiLogLevel, const char*)> _logHandler;

  // ── Callbacks ────────────────────────────────────────────────────────────
  std::function<void(WiFiState, WiFiState)>   _cbStateChange;
  std::function<void(const String&, IPAddress)> _cbConnected;
  std::function<void(int)>                    _cbDisconnected;
  std::function<void(const String&, IPAddress)> _cbAPStarted;
  std::function<void()>                       _cbAPStopped;
  std::function<void()>                       _cbCredsChanged;

#if defined(ESP8266)
  WiFiEventHandler _disconnectHandler;
#endif

  // ── Internal helpers ─────────────────────────────────────────────────────
  String _loadData() const;
  void   _saveData(const String& data);
  void   _loadStaticIP();
  void   _saveStaticIP();
  void   _applyStaticIP();

  void   _setupEventHandlers();
  void   _checkScanStatus();
  void   _startNextConnection();
  void   _checkConnectionStatus();
  void   _handleAllFailed();

  void   _scheduleReconnect();
  void   _checkReconnect();
  void   _resetBackoff();

  void   _checkAPTimeout();
  void   _checkBGScan();
  void   _startBGScan();
  void   _processBGScanResults();

  void   _setState(WiFiState newState);
  void   _log(WiFiLogLevel level, const char* fmt, ...) const;

  void   _printHelp() const;
  static String _urlDecode(const String& str);
  static int    _splitArgsQuoted(const String& line, String out[], int maxParts);

  // ── Web handlers ─────────────────────────────────────────────────────────
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
