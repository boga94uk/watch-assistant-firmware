// ============================================
// TEST/AP CONFIGURATION MODE
// ============================================
// Triggered by holding top button during power-on
// Starts WiFi AP with web server for testing and configuration

#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// Test mode entry timing
#define TEST_MODE_HOLD_MS 3000  // Hold bottom button for 3 seconds

// AP Configuration
#define AP_SSID "ESP Assistant"
#define AP_PASSWORD ""  // Open network
#define AP_CHANNEL 1
#define AP_MAX_CONNECTIONS 4

// DNS Server for captive portal
#define DNS_PORT 53

// Web Server
#define WEB_SERVER_PORT 80

// Global objects (defined in test_mode.cpp)
extern WebServer webServer;
extern DNSServer dnsServer;

// Test mode state
extern bool testModeActive;
extern unsigned long testModeStartTime;
extern bool deviceConfirmed;  // Track if device unlocked in test mode

// Button test tracking
extern int topButtonPressCount;
extern int bottomButtonPressCount;
extern unsigned long lastTopButtonTest;
extern unsigned long lastBottomButtonTest;

// SD card test results
extern bool sdCardTestPassed;
extern String sdCardInfo;

// Forward declaration
class ESP_IOExpander;

// Function declarations
void startTestMode(ESP_IOExpander *io_expander = nullptr);
void handleTestMode();
void stopTestMode();

// Web server handlers
void handleRoot();
void handleStatus();
void handleWiFiConfig();
void handleSaveWiFi();
void handleServerConfig();
void handleSaveServer();
void handleButtonTest();
void handleSDTest();
void handleFileBrowser();
void handleAudioTest();
void handleFileDownload();
void handleFileUpload();
void handleFileDelete();
void handleNotFound();

// Utility functions
String getDeviceStatus();
String getSystemInfo();
String getWiFiConfigHTML();
String getButtonTestHTML();
String getSDTestHTML();

#endif // TEST_MODE_H
