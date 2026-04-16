/**
 * @file ESPWiFiManagerConfig.h
 * @brief One-time configuration file for the ESPWiFiManager library.
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

// Uncomment to use ESPAsyncWebServer instead of the standard WebServer:
// #define WIFIMANAGER_USE_ASYNC_WEBSERVER

#endif // ESP_WIFI_MANAGER_CONFIG_H
