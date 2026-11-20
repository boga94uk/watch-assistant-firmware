#pragma once

#include <Arduino.h>
#include <WiFi.h>

// ============================================
// WIFI MANAGER - Connection & Auto-Reconnect
// ============================================

/**
 * Initialize WiFi manager and attempt connection using NVS credentials
 * @return true if WiFi connected or credentials loaded
 */
bool initWiFi();

/**
 * Connect to WiFi using saved credentials from NVS
 * Non-blocking - returns immediately, connection happens in background
 * @return true if connection attempt started
 */
bool connectWiFi();

/**
 * Disconnect from WiFi
 */
void disconnectWiFi();

/**
 * Update WiFi state - call in loop() to handle auto-reconnect
 * @return true if WiFi is connected
 */
bool updateWiFi();

/**
 * Check if WiFi is connected
 * @return true if connected to AP
 */
bool isWiFiConnected();

/**
 * Get WiFi signal strength in bars (0-4)
 * @return 0 (no signal) to 4 (excellent)
 */
int getWiFiSignalBars();

/**
 * Get WiFi RSSI (signal strength in dBm)
 * @return RSSI value (typically -30 to -90 dBm)
 */
int getWiFiRSSI();

/**
 * Get current WiFi SSID
 * @return SSID string or empty if not connected
 */
String getWiFiSSID();

/**
 * Get local IP address
 * @return IP address as string
 */
String getWiFiIP();
