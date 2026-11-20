#pragma once

#include <Arduino.h>

// ============================================
// NVS MANAGER - Non-Volatile Storage
// ============================================
// Clean interface for storing WiFi, server, and user settings
// Uses ESP32 Preferences library (wrapper around NVS)

namespace NVS {

/**
 * Initialize NVS manager
 * @return true if initialization successful
 */
bool init();

// ============================================
// WIFI CREDENTIALS
// ============================================

/**
 * Save WiFi credentials to NVS
 * @param ssid WiFi SSID (network name)
 * @param password WiFi password
 * @return true if saved successfully
 */
bool saveWiFiConfig(const char* ssid, const char* password);

/**
 * Load WiFi credentials from NVS
 * @param ssid Buffer to store SSID (min 33 bytes)
 * @param password Buffer to store password (min 64 bytes)
 * @param max_len Maximum length of buffers
 * @return true if credentials exist and loaded successfully
 */
bool loadWiFiConfig(char* ssid, char* password, size_t max_len);

/**
 * Check if WiFi credentials are configured
 * @return true if SSID exists in NVS
 */
bool hasWiFiConfig();

/**
 * Clear WiFi credentials from NVS
 */
void clearWiFiConfig();

// ============================================
// SERVER CONFIGURATION
// ============================================

/**
 * Save server URL to NVS
 * @param url Server URL (e.g. "https://api.example.com")
 * @return true if saved successfully
 */
bool saveServerURL(const char* url);

/**
 * Load server URL from NVS
 * @param url Buffer to store URL (min 256 bytes recommended)
 * @param max_len Maximum length of buffer
 * @return true if URL exists and loaded successfully
 */
bool loadServerURL(char* url, size_t max_len);

/**
 * Check if server URL is configured
 * @return true if URL exists in NVS
 */
bool hasServerURL();

/**
 * Clear server URL from NVS
 */
void clearServerURL();

// ============================================
// SSL CERTIFICATES
// ============================================

/**
 * Save Root CA certificate to NVS
 * @param cert Root CA certificate in PEM format (max ~4KB)
 * @return true if saved successfully
 */
bool saveRootCA(const char* cert);

/**
 * Load Root CA certificate from NVS
 * @param cert Buffer to store certificate (min 4096 bytes recommended)
 * @param max_len Maximum length of buffer
 * @return true if certificate exists and loaded successfully
 */
bool loadRootCA(char* cert, size_t max_len);

/**
 * Check if Root CA certificate is configured in NVS
 * @return true if certificate exists in NVS
 */
bool hasRootCA();

/**
 * Clear Root CA certificate from NVS (will revert to hardcoded default)
 */
void clearRootCA();

// ============================================
// USER SETTINGS
// ============================================

/**
 * Save user settings to NVS
 * @param brightness Display brightness (0-100)
 * @param volume Audio volume (0-100)
 * @param mic_gain Microphone gain (0-7)
 * @return true if saved successfully
 */
bool saveSettings(int brightness, int volume, int mic_gain);

/**
 * Load user settings from NVS
 * @param brightness Pointer to store brightness
 * @param volume Pointer to store volume
 * @param mic_gain Pointer to store mic gain
 * @return true if settings exist and loaded successfully
 */
bool loadSettings(int* brightness, int* volume, int* mic_gain);

/**
 * Check if user settings are configured
 * @return true if settings have been saved before
 */
bool hasSettings();

/**
 * Save device secret to NVS
 * @param secret Device secret (64 hex chars)
 * @return true if saved successfully
 */
bool saveDeviceSecret(const char* secret);

/**
 * Load device secret from NVS
 * @param secret Buffer to store secret (65 bytes for null terminator)
 * @return true if loaded successfully
 */
bool loadDeviceSecret(char* secret);

/**
 * Save timezone offset to NVS
 * @param timezone_hours Timezone offset in hours (-12 to +14)
 * @return true if saved successfully
 */
bool saveTimezone(int timezone_hours);

/**
 * Load timezone offset from NVS
 * @param timezone_hours Pointer to store timezone offset
 * @return true if loaded successfully
 */
bool loadTimezone(int* timezone_hours);

/**
 * Clear user settings from NVS
 */
void clearSettings();

// ============================================
// UTILITY
// ============================================

/**
 * Clear all stored data from NVS
 * Use with caution!
 */
void clearAll();

/**
 * Print all stored configuration to serial (for debugging)
 */
void printConfig();

} // namespace NVS
