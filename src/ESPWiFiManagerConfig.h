/**
 * @file ESPWiFiManagerConfig.h
 * @brief One-time configuration file for ESPWiFiManager.
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  Edit THIS file to configure the library.                           │
 * │  You never need to touch your sketch or any other library file.     │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * ── Web server backend ────────────────────────────────────────────────────
 *  By default the library uses the standard blocking WebServer that ships
 *  with the ESP32 / ESP8266 Arduino core — no extra libraries needed.
 *
 *  To switch to ESPAsyncWebServer (non-blocking, event-driven), uncomment
 *  the line below and install the required dependencies:
 *
 *    Dependencies (install via Arduino Library Manager or PlatformIO):
 *      • ESPAsyncWebServer
 *      • AsyncTCP       ← ESP32 only
 *      • ESPAsyncTCP    ← ESP8266 only
 *
 * ─────────────────────────────────────────────────────────────────────────
 */

#ifndef ESP_WIFI_MANAGER_CONFIG_H
#define ESP_WIFI_MANAGER_CONFIG_H

// ── Web server backend ────────────────────────────────────────────────────
// Uncomment to use ESPAsyncWebServer instead of the standard WebServer:
// #define WIFIMANAGER_USE_ASYNC_WEBSERVER

// ── Logging ───────────────────────────────────────────────────────────────
// Default log level. Override at runtime with wm.setLogLevel(...).
// Values: WIFI_LOG_NONE (0), WIFI_LOG_ERROR (1), WIFI_LOG_INFO (2), WIFI_LOG_DEBUG (3)
#ifndef WIFIMANAGER_DEFAULT_LOG_LEVEL
  #define WIFIMANAGER_DEFAULT_LOG_LEVEL 2  // INFO
#endif

// ── Credential storage limits ─────────────────────────────────────────────
// Maximum number of networks to store.
// Both ESP32 and ESP8266 now use Preferences (NVS) so limits can be equal.
#ifndef WIFIMANAGER_MAX_CREDENTIALS
  #define WIFIMANAGER_MAX_CREDENTIALS 10
#endif

// ── Connection behaviour ──────────────────────────────────────────────────
// Timeout per network connection attempt (ms)
#ifndef WIFIMANAGER_CONNECTION_TIMEOUT_MS
  #define WIFIMANAGER_CONNECTION_TIMEOUT_MS 12000UL
#endif

// Timeout for the WiFi scan (ms)
#ifndef WIFIMANAGER_SCAN_TIMEOUT_MS
  #define WIFIMANAGER_SCAN_TIMEOUT_MS 15000UL
#endif

// ── Reconnection backoff ──────────────────────────────────────────────────
// Initial delay before the first reconnect attempt after a drop (ms)
#ifndef WIFIMANAGER_RECONNECT_BASE_MS
  #define WIFIMANAGER_RECONNECT_BASE_MS 5000UL
#endif

// Maximum reconnect interval after repeated failures (ms). Default: 5 min.
#ifndef WIFIMANAGER_RECONNECT_MAX_MS
  #define WIFIMANAGER_RECONNECT_MAX_MS 300000UL
#endif

// ── AP / Portal settings ──────────────────────────────────────────────────
// AP channel (1–13, 0 = auto)
#ifndef WIFIMANAGER_AP_CHANNEL
  #define WIFIMANAGER_AP_CHANNEL 1
#endif

// Maximum simultaneous clients on the soft-AP
#ifndef WIFIMANAGER_AP_MAX_CLIENTS
  #define WIFIMANAGER_AP_MAX_CLIENTS 4
#endif

// Soft-AP timeout in milliseconds (0 = never timeout)
#ifndef WIFIMANAGER_AP_TIMEOUT_MS
  #define WIFIMANAGER_AP_TIMEOUT_MS 0UL
#endif

// Non-blocking restart delay after saving credentials via portal (ms)
#ifndef WIFIMANAGER_RESTART_DELAY_MS
  #define WIFIMANAGER_RESTART_DELAY_MS 600UL
#endif

// Background scan interval while captive portal is active (ms)
#ifndef WIFIMANAGER_BG_SCAN_INTERVAL_MS
  #define WIFIMANAGER_BG_SCAN_INTERVAL_MS 30000UL
#endif

#endif // ESP_WIFI_MANAGER_CONFIG_H
