// ============================================
// TEST/AP CONFIGURATION MODE - Implementation
// ============================================

#include "test_mode.h"
#include "nvs_manager.h"
#include "battery_manager.h"
#include "rtc_manager.h"
#include "imu_manager.h"
#include "sd_manager.h"
#include "audio/audio_manager.h"
#include "display/display_screens.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_mac.h>
#include <ESP_IOExpander_Library.h>
#include <lvgl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <Update.h>
#include <esp_ota_ops.h>

// Button pin (matches main.cpp)
#define BUTTON_TOP_PIN 0

// Global objects
DNSServer dnsServer;
WebServer webServer(80);
extern ESP_IOExpander *expander;  // Defined in main.cpp, passed to startTestMode()

// Stubs for modules not yet integrated (SD card and Audio only)
namespace TestMode {
    bool sdCardTestPassed = false;  // Will be set in startTestMode()
    String sdCardInfo = "Not tested";
    
    // Audio stubs (TODO: Replace when audio_manager is integrated)
    bool startRecording() { return false; }
    bool saveRecordingToSD(const char*) { return false; }
    bool playAudioFile(const char*) { return false; }
    bool stopPlayback() { return false; }
    void setVolume(int) {}
    void setMicGain(int) {}
    
    // SD card stubs (TODO: Replace when sd_manager is integrated)
    class FileStub {
    public:
        operator bool() const { return false; }
        bool isDirectory() { return false; }
        FileStub openNextFile() { return FileStub(); }
        String name() { return ""; }
        size_t size() { return 0; }
        void close() {}
        size_t write(const uint8_t*, size_t) { return 0; }
    };
    
    class SD_MMC_Stub {
    public:
        bool exists(const char*) { return false; }
        FileStub open(const char*, const char* mode = "r") { return FileStub(); }
        bool remove(const char*) { return false; }
    };
    
    SD_MMC_Stub SD_MMC;
}

// Make SD_MMC and File available globally
using TestMode::SD_MMC;
using File = TestMode::FileStub;

// FILE_READ and FILE_WRITE constants
#define FILE_READ "r"
#define FILE_WRITE "w"

// Global test mode state
bool testModeActive = false;
bool deviceConfirmed = false;  // true after user confirms device ownership

// Button test tracking
int topButtonPressCount = 0;
int bottomButtonPressCount = 0;
unsigned long lastTopButtonTest = 0;
unsigned long lastBottomButtonTest = 0;

// SD card test results (set in startTestMode)
bool sdCardTestPassed = false;
String sdCardInfo = "";

// Test mode display
lv_obj_t *testmode_screen = nullptr;
test_mode_state_t testmode_display_state = TEST_MODE_WAIT;

// ============================================
// FORWARD DECLARATIONS
// ============================================
void handleRoot();
void handleStatus();
void handleWiFiConfig();
void handleSaveWiFi();
void handleServerConfig();
void handleSaveServer();
void handleButtonTest();
void handleSDTest();
void handleAudioTest();
void handleDeviceSettings();
void handleFileBrowser();
void handleFileDownload();
void handleFileUpload();
void handleFileDelete();
void handleFirmwareUpdate();
void handleFirmwareUploadPage();
void handleNotFound();

// ============================================
// START TEST MODE
// ============================================

// Store MAC address before WiFi mode changes
String deviceMAC = "";

void startTestMode(ESP_IOExpander *io_expander) {
  testModeActive = true;
  expander = io_expander;  // Store expander pointer
  
  Serial.println("\n========================================");
  Serial.println("🧪 ENTERING TEST/CONFIGURATION MODE");
  Serial.println("========================================\n");
  
  // Run SD card test using SD manager API
  if (isSDCardAvailable()) {
    TestMode::sdCardTestPassed = true;
    uint64_t sizeMB = getSDCardSize();
    TestMode::sdCardInfo = String("SD Card OK - ") + String((unsigned long)sizeMB) + "MB";
    Serial.printf("✅ SD Card: %s\n", TestMode::sdCardInfo.c_str());
  } else {
    TestMode::sdCardTestPassed = false;
    TestMode::sdCardInfo = "SD Card Failed";
    Serial.println("❌ SD Card: Not detected");
  }
  
  // Create test mode display screen
  testmode_screen = screen_testmode_create(lv_scr_act());
  screen_testmode_set_visible(testmode_screen, true);
  testmode_display_state = TEST_MODE_WAIT;
  screen_testmode_update_state(testmode_screen, TEST_MODE_WAIT);
  Serial.println("🖥️  Test mode display: WAIT screen shown");
  
  Serial.println("\n🧪 Starting Test Mode Web Server...");
  
  // Get MAC address BEFORE changing WiFi mode
  // Use WiFi.macAddress() which works in any mode
  deviceMAC = WiFi.macAddress();
  if (deviceMAC == "00:00:00:00:00:00" || deviceMAC.length() == 0) {
    // If WiFi not initialized, get from base MAC
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
    deviceMAC = String(macStr);
  }
  
  // Start AP mode with max 1 connection
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, 1);  // channel=1, hidden=0, max_connections=1, AP_CHANNEL, 0, AP_MAX_CONNECTIONS);
  
  IPAddress IP = WiFi.softAPIP();
  Serial.printf("✅ AP Started: %s\n", AP_SSID);
  Serial.printf("✅ IP Address: %s\n", IP.toString().c_str());
  Serial.println("✅ No password required (open network)");
  
  // Start DNS server for captive portal
  dnsServer.start(DNS_PORT, "*", IP);
  Serial.println("✅ DNS server started (captive portal)");
  
  // Setup web server routes
  webServer.on("/", handleRoot);
  webServer.on("/status", handleStatus);
  webServer.on("/wifi", handleWiFiConfig);
  webServer.on("/savewifi", HTTP_POST, handleSaveWiFi);
  webServer.on("/serverconfig", handleServerConfig);
  webServer.on("/saveserver", HTTP_POST, handleSaveServer);
  webServer.on("/buttontest", handleButtonTest);
  webServer.on("/sdtest", handleSDTest);
  webServer.on("/audiotest", handleAudioTest);
  webServer.on("/devicesettings", handleDeviceSettings);
  webServer.on("/audiosettings", handleDeviceSettings);  // Backward compatibility
  webServer.on("/filebrowser", handleFileBrowser);
  webServer.on("/download", handleFileDownload);
  webServer.on("/upload", HTTP_POST, []() {
    webServer.send(200);
  }, handleFileUpload);
  webServer.on("/delete", handleFileDelete);
  webServer.on("/firmwareupdate", handleFirmwareUpdate);
  webServer.on("/uploadfirmware", HTTP_POST, 
    []() { webServer.send(200); },
    handleFirmwareUploadPage
  );
  webServer.on("/restart", []() {
    webServer.send(200, "text/html", "<html><body><h1>Restarting...</h1><p>Device will restart in normal mode.</p></body></html>");
    delay(1000);
    ESP.restart();
  });
  webServer.on("/reboot", []() {
    webServer.send(200, "text/html", "<html><body><h1>Rebooting...</h1><p>Device will reboot to normal mode in 2 seconds.</p></body></html>");
    delay(2000);
    ESP.restart();
  });
  webServer.onNotFound(handleNotFound);
  
  webServer.begin();
  Serial.println("✅ Web server started on port 80");
  
  Serial.println("\n========================================");
  Serial.println("📱 Connect to WiFi: ESP Assistant");
  Serial.println("🌐 Open browser (captive portal will redirect)");
  Serial.println("🔧 Configure device and run tests");
  Serial.println("========================================\n");
}

// ============================================
// HANDLE TEST MODE (call in loop)
// ============================================

void handleTestMode() {
  if (!testModeActive) return;
  
  // Update LVGL timers (keep display responsive)
  lv_timer_handler();
  
  // Update audio playback if playing
  if (audio_is_playing()) {
    audio_update_playback();
  }
  
  // Update test mode screen state based on WiFi client connection
  static uint8_t lastClientCount = 0;
  uint8_t clientCount = WiFi.softAPgetStationNum();
  
  if (clientCount != lastClientCount) {
    lastClientCount = clientCount;
    
    if (clientCount > 0 && testmode_display_state == TEST_MODE_WAIT) {
      // Client connected - show LOCKED screen
      testmode_display_state = TEST_MODE_LOCKED;
      screen_testmode_update_state(testmode_screen, TEST_MODE_LOCKED);
      Serial.println("🖥️  Test mode display: LOCKED screen shown (client connected)");
    } else if (clientCount == 0 && testmode_display_state != TEST_MODE_WAIT) {
      // Client disconnected - back to WAIT screen
      testmode_display_state = TEST_MODE_WAIT;
      deviceConfirmed = false;
      screen_testmode_update_state(testmode_screen, TEST_MODE_WAIT);
      Serial.println("🖥️  Test mode display: WAIT screen shown (client disconnected)");
    }
  }
  
  static bool lastButtonState = HIGH;
  static unsigned long buttonPressTime = 0;
  static bool buttonHandled = false;
  
  bool currentButtonState = digitalRead(BUTTON_TOP_PIN);
  unsigned long now = millis();
  
  // Detect falling edge (button pressed)
  if (currentButtonState == LOW && lastButtonState == HIGH) {
    // Button just pressed
    buttonPressTime = now;
    buttonHandled = false;
    Serial.println("🔘 Button pressed");
  }
  
  // After 50ms debounce, process the press if still held
  if (currentButtonState == LOW && !buttonHandled && (now - buttonPressTime) >= 50) {
    buttonHandled = true;
    
    // Track button press for button test page
    topButtonPressCount++;
    Serial.printf("🔘 Top button pressed (count: %d)\n", topButtonPressCount);
    
    if (!deviceConfirmed) {
      // On LOCKED screen: unlock and show GEAR screen
      deviceConfirmed = true;
      testmode_display_state = TEST_MODE_UNLOCKED;
      screen_testmode_update_state(testmode_screen, TEST_MODE_UNLOCKED);
      Serial.println("🔓 Device UNLOCKED - Web interface accessible");
      Serial.println("🖥️  Test mode display: UNLOCKED screen shown (rotating gear)");
    }
  }
  
  // Check for LONG HOLD (5+ seconds) to exit test mode
  static bool exitWarningShown = false;
  if (currentButtonState == LOW && deviceConfirmed && (now - buttonPressTime) >= 5000) {
    if (!exitWarningShown) {
      Serial.println("🚪 LONG HOLD DETECTED - Exiting test mode...");
      exitWarningShown = true;
      stopTestMode();
      return;
    }
  }
  
  // Reset exit warning when button released
  if (currentButtonState == HIGH) {
    exitWarningShown = false;
  }
  
  // Check bottom button (IO Expander Pin 4) for test
  if (expander) {
    static bool lastBottomState = LOW;
    int bottomState = expander->digitalRead(4);
    
    // Detect button press (HIGH = pressed for this button - reversed logic)
    if (bottomState == HIGH && lastBottomState == LOW) {
      bottomButtonPressCount++;
      Serial.printf("🔘 Bottom button pressed (count: %d)\n", bottomButtonPressCount);
    }
    lastBottomState = bottomState;
  }
  
  lastButtonState = currentButtonState;
  
  dnsServer.processNextRequest();
  webServer.handleClient();
  
  // Heartbeat
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat > 5000) {
    lastHeartbeat = millis();
    Serial.printf("💓 Test mode active (%lu clients connected) %s\n", 
                     WiFi.softAPgetStationNum(),
                     deviceConfirmed ? "🔓 UNLOCKED" : "🔒 LOCKED");
  }
}

// ============================================
// STOP TEST MODE
// ============================================

void stopTestMode() {
  if (!testModeActive) return;
  
  Serial.println("\n🛑 Exiting test mode...");
  
  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  
  testModeActive = false;
  
  Serial.println("✅ Test mode stopped");
  Serial.println("🔄 Restarting in normal mode...\n");
  
  delay(1000);
  ESP.restart();
}

// ============================================
// WEB SERVER HANDLERS
// ============================================

void handleRoot() {
  // Check if device is confirmed (button pressed)
  if (!deviceConfirmed) {
    String lockHtml = R"rawliteral(<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Device Locked</title>
  <style>
    body { font-family: Arial; text-align: center; padding: 50px; background: #f0f0f0; }
    .lock-box { background: white; padding: 40px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); max-width: 400px; margin: 0 auto; }
    h1 { color: #ff6b6b; }
    .icon { font-size: 64px; margin: 20px 0; }
    p { color: #666; line-height: 1.6; }
    .refresh-btn { background: #4CAF50; color: white; padding: 12px 24px; border: none; border-radius: 5px; cursor: pointer; font-size: 16px; margin-top: 20px; }
    .refresh-btn:hover { background: #45a049; }
  </style>
  <script>
    setTimeout(() => location.reload(), 2000);
  </script>
</head>
<body>
  <div class="lock-box">
    <div class="icon">🔒</div>
    <h1>Device Locked</h1>
    <p><strong>Please press the TOP button on the device to unlock access.</strong></p>
    <p>This security measure ensures only authorized physical access to the device configuration.</p>
    <button class="refresh-btn" onclick="location.reload()">Check Again</button>
  </div>
</body>
</html>)rawliteral";
    webServer.send(200, "text/html", lockHtml);
    return;
  }
  
  // Use stored MAC address
  String macAddr = deviceMAC;
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP Assistant - Test Mode</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  min-height: 100vh;
  padding: 20px;
}
.container {
  max-width: 900px;
  margin: 0 auto;
  background: white;
  border-radius: 20px;
  box-shadow: 0 20px 60px rgba(0,0,0,0.3);
  overflow: hidden;
}
.header {
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  padding: 30px;
  text-align: center;
}
.header h1 { font-size: 28px; margin-bottom: 5px; }
.header .device-id { opacity: 0.8; font-size: 12px; font-family: monospace; }
.tabs {
  display: flex;
  background: #f8f9fa;
  border-bottom: 2px solid #dee2e6;
}
.tab {
  flex: 1;
  padding: 15px;
  text-align: center;
  cursor: pointer;
  border: none;
  background: none;
  font-size: 16px;
  color: #666;
  transition: all 0.3s;
}
.tab:hover { background: #e9ecef; }
.tab.active {
  color: #667eea;
  font-weight: bold;
  border-bottom: 3px solid #667eea;
  background: white;
}
.tab-content {
  display: none;
  padding: 30px;
}
.tab-content.active { display: block; }
.card {
  background: #f8f9fa;
  border-radius: 12px;
  padding: 20px;
  margin-bottom: 20px;
  border-left: 4px solid #667eea;
}
.card h2 { font-size: 18px; margin-bottom: 15px; color: #333; }
.btn {
  display: inline-block;
  padding: 12px 24px;
  background: #667eea;
  color: white;
  text-decoration: none;
  border-radius: 8px;
  border: none;
  cursor: pointer;
  font-size: 16px;
  transition: all 0.3s;
  margin-right: 10px;
  margin-bottom: 10px;
}
.btn:hover { background: #5568d3; transform: translateY(-2px); }
.btn-success { background: #28a745; }
.btn-success:hover { background: #218838; }
.btn-danger { background: #dc3545; }
.btn-danger:hover { background: #c82333; }
.status { padding: 10px; border-radius: 8px; margin-bottom: 10px; }
.status-good { background: #d4edda; color: #155724; }
.status-info { background: #d1ecf1; color: #0c5460; }
.info-row { display: flex; justify-content: space-between; padding: 8px 0; border-bottom: 1px solid #dee2e6; }
.info-label { font-weight: bold; color: #666; }
.info-value { color: #333; }
</style>
<script>
function showTab(tabName) {
  // Hide all tabs
  document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
  document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
  
  // Show selected tab
  document.getElementById(tabName).classList.add('active');
  document.querySelector(`[onclick="showTab('${tabName}')"]`).classList.add('active');
}
</script>
</head>
<body>
<div class="container">
<div class="header">
<h1>🧪 ESP Assistant</h1>
<p>Test & Configuration Mode</p>
<p class="device-id">Device ID: )rawliteral";
  
  html += macAddr;
  
  html += R"rawliteral(</p>
</div>

<div class="tabs">
<button class="tab active" onclick="showTab('status')">📊 Status</button>
<button class="tab" onclick="showTab('config')">⚙️ Config</button>
<button class="tab" onclick="showTab('test')">🧪 Test</button>
</div>

<!-- STATUS TAB -->
<div id="status" class="tab-content active">
<div class="card">
<h2>📊 System Status</h2>
<div class="status status-good">✅ Test mode active</div>
<div class="status status-info">📡 AP: ESP_Assistant</div>
<a href="/status" class="btn">View Full Status</a>
</div>

<div class="card">
<h2>📁 File Browser</h2>
<p>Browse, download, and manage files on SD card</p>
<a href="/filebrowser" class="btn">Open File Browser</a>
</div>


<div class="card">
<h2>🚪 Exit Test Mode</h2>
<p>Restart device in normal mode</p>
<a href="/reboot" class="btn btn-danger">Reboot to Normal Mode</a>
</div>
</div>

<!-- CONFIG TAB -->
<div id="config" class="tab-content">
<div class="card">
<h2>📱 WiFi Configuration</h2>
<p>Configure WiFi credentials for normal operation</p>
<a href="/wifi" class="btn btn-success">Configure WiFi</a>
</div>

<div class="card">
<h2>⚙️ Device Settings</h2>
<p>Volume, mic gain, and timezone (saved to NVS)</p>
<a href="/devicesettings" class="btn">Device Settings</a>
</div>

<div class="card">
<h2>🌐 Server Configuration</h2>
<p>Configure voice assistant server URL</p>
<a href="/serverconfig" class="btn">Server Settings</a>
</div>

<div class="card">
<h2>🔄 Firmware Update (OTA)</h2>
<p>Upload new firmware over-the-air with rollback support</p>
<a href="/firmwareupdate" class="btn">Update Firmware</a>
</div>

<div class="card">
<h2>🚪 Exit</h2>
<a href="/" class="btn">← Back to Status</a>
</div>
</div>

<!-- TEST TAB -->
<div id="test" class="tab-content">
<div class="card">
<h2>🔘 Button Tests</h2>
<p>Test top and bottom buttons</p>
<a href="/buttontest" class="btn">Run Button Tests</a>
</div>

<div class="card">
<h2>💾 SD Card Tests</h2>
<p>Test SD card read/write operations</p>
<a href="/sdtest" class="btn">Run SD Tests</a>
</div>

<div class="card">
<h2>🎙️ Audio Hardware Test</h2>
<p>Test microphone recording & speaker playback</p>
<a href="/audiotest" class="btn">Run Audio Test</a>
</div>

<div class="card">
<h2>🚪 Exit</h2>
<a href="/" class="btn">← Back to Status</a>
</div>
</div>

</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleStatus() {
  unsigned long uptimeSeconds = millis() / 1000;
  unsigned long hours = uptimeSeconds / 3600;
  unsigned long minutes = (uptimeSeconds % 3600) / 60;
  unsigned long seconds = uptimeSeconds % 60;
  
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t usedHeap = totalHeap - freeHeap;
  float heapUsagePercent = (float)usedHeap / totalHeap * 100;
  
  // PSRAM info
  uint32_t freePSRAM = ESP.getFreePsram();
  uint32_t totalPSRAM = ESP.getPsramSize();
  uint32_t usedPSRAM = totalPSRAM - freePSRAM;
  float psramUsagePercent = totalPSRAM > 0 ? ((float)usedPSRAM / totalPSRAM * 100) : 0;
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Device Status</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 700px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
h1 { color: #333; margin-bottom: 20px; }
.stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin: 20px 0; }
.stat-card { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 20px; border-radius: 10px; }
.stat-card h3 { font-size: 14px; margin: 0 0 10px 0; opacity: 0.9; }
.stat-card .value { font-size: 32px; font-weight: bold; margin: 0; }
.stat-card .unit { font-size: 14px; opacity: 0.8; }
.info-section { background: #f8f9fa; padding: 15px; border-radius: 8px; margin: 15px 0; }
.info-row { display: flex; justify-content: space-between; padding: 10px 0; border-bottom: 1px solid #dee2e6; }
.info-row:last-child { border-bottom: none; }
.info-label { font-weight: bold; color: #666; }
.info-value { color: #333; }
.progress-bar { background: #e9ecef; border-radius: 10px; height: 20px; overflow: hidden; margin: 10px 0; }
.progress-fill { background: linear-gradient(90deg, #667eea 0%, #764ba2 100%); height: 100%; transition: width 0.3s; }
.back { display: inline-block; margin-top: 15px; padding: 10px 20px; background: #667eea; color: white; text-decoration: none; border-radius: 5px; }
</style>
<script>
setTimeout(function(){ location.reload(); }, 5000);
</script>
</head>
<body>
<div class="container">
<h1>📊 Device Status</h1>
<div class="stat-grid">
<div class="stat-card">
<h3>⏱️ Uptime</h3>
<p class="value">)rawliteral";
  
  html += String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes);
  
  html += R"rawliteral(</p>
<p class="unit">hours:minutes</p>
</div>
<div class="stat-card">
<h3>💾 Free Memory</h3>
<p class="value">)rawliteral";
  
  html += String(freeHeap / 1024);
  
  html += R"rawliteral(</p>
<p class="unit">KB</p>
</div>
<div class="stat-card">
<h3>⚡ CPU Speed</h3>
<p class="value">)rawliteral";
  
  html += String(ESP.getCpuFreqMHz());
  
  html += R"rawliteral(</p>
<p class="unit">MHz</p>
</div>
</div>
<div class="info-section">
<h3>Memory Usage</h3>
<div class="progress-bar">
<div class="progress-fill" style="width: )rawliteral";
  
  html += String((int)heapUsagePercent);
  
  html += R"rawliteral(%"></div>
</div>
<div class="info-row">
<span class="info-label">Total Heap:</span>
<span class="info-value">)rawliteral";
  
  html += String(totalHeap / 1024) + " KB";
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Used:</span>
<span class="info-value">)rawliteral";
  
  html += String(usedHeap / 1024) + " KB (" + String((int)heapUsagePercent) + "%)";
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Free:</span>
<span class="info-value">)rawliteral";
  
  html += String(freeHeap / 1024) + " KB";
  
  html += R"rawliteral(</span>
</div>
</div>
<div class="info-section">
<h3>PSRAM (External Memory)</h3>
<div class="progress-bar">
<div class="progress-fill" style="width: )rawliteral";
  
  html += String((int)psramUsagePercent);
  
  html += R"rawliteral(%"></div>
</div>
<div class="info-row">
<span class="info-label">Total PSRAM:</span>
<span class="info-value">)rawliteral";
  
  html += String(totalPSRAM / 1024) + " KB";
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Used:</span>
<span class="info-value">)rawliteral";
  
  html += String(usedPSRAM / 1024) + " KB (" + String((int)psramUsagePercent) + "%)";
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Free:</span>
<span class="info-value">)rawliteral";
  
  html += String(freePSRAM / 1024) + " KB";
  
  html += R"rawliteral(</span>
</div>
</div>
<div class="info-section">
<h3>System Information</h3>
<div class="info-row">
<span class="info-label">Chip Model:</span>
<span class="info-value">)rawliteral";
  
  html += String(ESP.getChipModel());
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Chip Revision:</span>
<span class="info-value">)rawliteral";
  
  html += String(ESP.getChipRevision());
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Flash Size:</span>
<span class="info-value">)rawliteral";
  
  html += String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB";
  
  html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">SDK Version:</span>
<span class="info-value">)rawliteral";
  
  html += String(ESP.getSdkVersion());
  
  html += R"rawliteral(</span>
</div>
</div>
<div class="info-section">
<h3>🕐 Real-Time Clock</h3>)rawliteral";

  if (rtcInitialized) {
    html += R"rawliteral(<div class="info-row">
<span class="info-label">Current Time:</span>
<span class="info-value">)rawliteral";
    html += getCurrentTime();
    html += R"rawliteral(</span>
</div>)rawliteral";
  } else {
    html += R"rawliteral(<div class="info-row">
<span class="info-value" style="color: #999;">RTC not initialized</span>
</div>)rawliteral";
  }

  html += R"rawliteral(</div>
<div class="info-section">
<h3>🔋 Battery Status</h3>)rawliteral";

  if (batteryInitialized) {
    html += R"rawliteral(<div class="info-row">
<span class="info-label">Status:</span>
<span class="info-value">)rawliteral";
    html += getBatteryStatus();
    html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Voltage:</span>
<span class="info-value">)rawliteral";
    html += String(getBatteryVoltage(), 2) + " V";
    html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Percentage:</span>
<span class="info-value">)rawliteral";
    html += String(getBatteryPercent()) + "%";
    html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Charging:</span>
<span class="info-value">)rawliteral";
    html += isCharging() ? "Yes ⚡" : "No";
    html += R"rawliteral(</span>
</div>)rawliteral";
  } else {
    html += R"rawliteral(<div class="info-row">
<span class="info-value" style="color: #999;">Battery not initialized</span>
</div>)rawliteral";
  }

  html += R"rawliteral(</div>
<div class="info-section">
<h3>🎯 IMU Sensor (QMI8658)</h3>)rawliteral";

  if (IMU::isReady()) {
    IMU::update();  // Get latest sensor data
    IMUdata acc, gyr;
    IMU::getAccelerometer(acc);
    IMU::getGyroscope(gyr);
    
    html += R"rawliteral(<div class="info-row">
<span class="info-label">Accelerometer (g):</span>
<span class="info-value">X: )rawliteral";
    html += String(acc.x, 2) + " Y: " + String(acc.y, 2) + " Z: " + String(acc.z, 2);
    html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Gyroscope (dps):</span>
<span class="info-value">X: )rawliteral";
    html += String(gyr.x, 1) + " Y: " + String(gyr.y, 1) + " Z: " + String(gyr.z, 1);
    html += R"rawliteral(</span>
</div>
<div class="info-row">
<span class="info-label">Magnitude:</span>
<span class="info-value">)rawliteral";
    html += String(IMU::getAccelerometerMagnitude(), 2) + " g";
    html += R"rawliteral(</span>
</div>)rawliteral";
  } else {
    html += R"rawliteral(<div class="info-row">
<span class="info-value" style="color: #999;">IMU not initialized</span>
</div>)rawliteral";
  }

  html += R"rawliteral(</div>
<p style="text-align: center; color: #999; font-size: 12px; margin-top: 20px;">
Auto-refreshes every 5 seconds
</p>
<a href="/" class="back">← Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleWiFiConfig() {
  // Get current WiFi config from NVS
  char ssid[64] = "";
  char password[64] = "";
  NVS::loadWiFiConfig(ssid, password, 64);
  String currentSSID = String(ssid);
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>WiFi Configuration</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 500px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
h1 { color: #333; margin-bottom: 20px; }
.form-group { margin-bottom: 20px; }
label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }
input { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }
.btn { width: 100%; padding: 12px; background: #28a745; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; }
.btn:hover { background: #218838; }
.back { display: inline-block; margin-top: 15px; color: #667eea; text-decoration: none; }
.current { background: #e7f3ff; padding: 10px; border-radius: 5px; margin-bottom: 20px; }
</style>
</head>
<body>
<div class="container">
<h1>📱 WiFi Configuration</h1>
)rawliteral";
  
  if (currentSSID.length() > 0) {
    html += "<div class='current'>Current SSID: <strong>" + currentSSID + "</strong></div>";
  }
  
  html += R"rawliteral(
<form action='/savewifi' method='POST'>
<div class='form-group'>
<label>WiFi SSID:</label>
<input type='text' name='ssid' placeholder='Enter WiFi network name' required>
</div>
<div class='form-group'>
<label>WiFi Password:</label>
<input type='password' name='password' placeholder='Enter WiFi password'>
</div>
<button type='submit' class='btn'>💾 Save Configuration</button>
</form>
<a href='/' class='back'>← Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleSaveWiFi() {
  String ssid = webServer.arg("ssid");
  String password = webServer.arg("password");
  
  Serial.println("\n💾 Saving WiFi credentials to NVS...");
  Serial.printf("   SSID: %s\n", ssid.c_str());
  Serial.printf("   Password length: %d\n", password.length());
  
  // Use our NVS manager
  bool success = NVS::saveWiFiConfig(ssid.c_str(), password.c_str());
  
  if (success) {
    Serial.println("✅ WiFi credentials saved successfully!");
  } else {
    Serial.println("❌ Failed to save WiFi credentials!");
  }
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>WiFi Saved</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 500px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; text-align: center; }
h1 { color: #28a745; }
.success { background: #d4edda; color: #155724; padding: 15px; border-radius: 8px; margin: 20px 0; }
.btn { display: inline-block; margin-top: 15px; padding: 12px 24px; background: #667eea; color: white; text-decoration: none; border-radius: 5px; }
</style>
</head>
<body>
<div class="container">
<h1>✅ WiFi Saved!</h1>
<div class="success">
<strong>SSID:</strong> )rawliteral";
  
  html += ssid;
  
  html += R"rawliteral(<br>
<strong>Status:</strong> Credentials saved to NVS
</div>
<p>Device will use these credentials on next boot.</p>
<p><strong>Next step:</strong> Restart the device to connect to WiFi.</p>
<a href="/restart" class="btn">🔄 Restart Now</a>
<a href="/" class="btn" style="background: #6c757d;">← Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleButtonTest() {
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Button Test</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
h1 { color: #333; margin-bottom: 20px; }
.counter { background: #e7f3ff; padding: 20px; border-radius: 8px; margin: 15px 0; text-align: center; }
.counter h2 { font-size: 48px; color: #667eea; margin: 10px 0; }
.counter p { color: #666; font-size: 14px; }
.info { background: #fff3cd; padding: 15px; border-radius: 8px; margin: 15px 0; }
.back { display: inline-block; margin-top: 15px; padding: 10px 20px; background: #667eea; color: white; text-decoration: none; border-radius: 5px; }
</style>
<script>
setTimeout(function(){ location.reload(); }, 2000);
</script>
</head>
<body>
<div class="container">
<h1>🔘 Button Test</h1>
<div class="counter">
<p>Top Button (GPIO0)</p>
<h2>)rawliteral";
  
  html += String(topButtonPressCount);
  
  html += R"rawliteral(</h2>
<p>presses</p>
</div>
<div class="counter">
<p>Bottom Button (IO Expander Pin 4)</p>
<h2>)rawliteral";
  
  html += String(bottomButtonPressCount);
  
  html += R"rawliteral(</h2>
<p>presses</p>
</div>
<div class="info">
<strong>📝 Instructions:</strong><br>
Press buttons on the device to test.<br>
Page auto-refreshes every 2 seconds.
</div>
<a href="/" class="back">← Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleSDTest() {
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SD Card Test</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
h1 { color: #333; margin-bottom: 20px; }
.result { padding: 15px; border-radius: 8px; margin: 15px 0; }
.success { background: #d4edda; color: #155724; }
.error { background: #f8d7da; color: #721c24; }
.info { background: #d1ecf1; color: #0c5460; }
.back { display: inline-block; margin-top: 15px; padding: 10px 20px; background: #667eea; color: white; text-decoration: none; border-radius: 5px; }
pre { background: #f8f9fa; padding: 10px; border-radius: 5px; overflow-x: auto; }
</style>
</head>
<body>
<div class="container">
<h1>💾 SD Card Test</h1>
<div class="result )rawliteral";
  
  html += TestMode::sdCardTestPassed ? "success" : "error";
  
  html += R"rawliteral(">
<strong>Status:</strong> )rawliteral";
  
  html += TestMode::sdCardTestPassed ? "✅ Passed" : "❌ Failed";
  
  html += R"rawliteral(</div>
<div class="info">
<strong>Test Results:</strong><br>
<pre>)rawliteral";
  
  html += TestMode::sdCardInfo;
  
  html += R"rawliteral(</pre>
</div>
<a href="/" class="back">← Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleFileBrowser() {
  String path = webServer.arg("path");
  if (path.length() == 0) path = SD_MOUNT_POINT;
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>File Browser</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 1000px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; overflow-x: auto; }
h1 { color: #333; margin-bottom: 20px; }
.path { background: #e7f3ff; padding: 15px; border-radius: 8px; margin-bottom: 20px; font-family: monospace; word-break: break-all; }
.upload-section { background: #f8f9fa; padding: 20px; border-radius: 8px; margin: 20px 0; }
.upload-table { width: 100%; border-collapse: collapse; }
.upload-table td { padding: 8px; vertical-align: middle; }
.file-input { padding: 8px; border: 1px solid #ddd; border-radius: 4px; }
.file-table { width: 100%; border-collapse: collapse; margin: 20px 0; table-layout: fixed; }
.file-table th { background: #667eea; color: white; padding: 12px; text-align: left; font-weight: bold; }
.file-table td { padding: 12px; border-bottom: 1px solid #dee2e6; word-wrap: break-word; }
.file-table tr:hover { background: #f8f9fa; }
.file-name { font-weight: bold; color: #333; overflow: hidden; text-overflow: ellipsis; }
.folder { color: #667eea; cursor: pointer; text-decoration: underline; }
.folder:hover { color: #5568d3; }
.file-size { color: #666; font-size: 14px; white-space: nowrap; }
.btn { padding: 6px 12px; background: #667eea; color: white; text-decoration: none; border-radius: 5px; border: none; cursor: pointer; font-size: 13px; white-space: nowrap; margin-right: 5px; display: inline-block; }
.btn:hover { background: #5568d3; }
.btn-danger { background: #dc3545; }
.btn-danger:hover { background: #c82333; }
.btn-success { background: #28a745; }
.btn-success:hover { background: #218838; }
.back { display: inline-block; margin-top: 15px; padding: 10px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; }
.name-col { width: 50%; }
.size-col { width: 20%; }
.actions-col { width: 30%; text-align: right; }
</style>
<script>
function deleteFile(filepath, filename) {
  if (confirm('Delete ' + filename + '?')) {
    window.location.href = '/delete?file=' + encodeURIComponent(filepath);
  }
}
</script>
</head>
<body>
<div class="container">
<h1>📁 File Browser</h1>
<div class="path">Current Path: <strong>)rawliteral";
  
  html += path;
  
  html += R"rawliteral(</strong></div>
<div class="upload-section">
<h3>Upload File</h3>
<form action="/upload?path=)rawliteral";
  
  html += path;
  
  html += R"rawliteral(" method="POST" enctype="multipart/form-data">
<table class="upload-table">
<tr>
<td style="width: 70%;"><input type="file" name="file" class="file-input" required style="width: 100%;"></td>
<td style="width: 30%; text-align: right;"><button type="submit" class="btn btn-success">Upload</button></td>
</tr>
</table>
</form>
</div>
<table class="file-table">
<thead>
<tr>
<th class="name-col">Name</th>
<th class="size-col">Size</th>
<th class="actions-col">Actions</th>
</tr>
</thead>
<tbody>)rawliteral";
  
  // List files and folders using POSIX functions
  DIR* dir = opendir(path.c_str());
  if (dir) {
    // Add parent directory link if not at root
    if (path != String(SD_MOUNT_POINT)) {
      String parentPath = path;
      int lastSlash = parentPath.lastIndexOf('/');
      if (lastSlash > (int)strlen(SD_MOUNT_POINT)) {
        parentPath = parentPath.substring(0, lastSlash);
      } else {
        parentPath = SD_MOUNT_POINT;
      }
      html += "<tr><td colspan='2'><span class='folder' onclick=\"location.href='/filebrowser?path=" + parentPath + "'\">📂 ..</span></td><td></td></tr>";
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
      // Skip . and ..
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      
      String fileName = String(entry->d_name);
      String fullPath = path;
      if (!fullPath.endsWith("/")) fullPath += "/";
      fullPath += fileName;
      
      // Check if directory
      struct stat st;
      bool isDir = false;
      if (stat(fullPath.c_str(), &st) == 0) {
        isDir = S_ISDIR(st.st_mode);
      }
      
      if (isDir) {
        html += "<tr><td class='name-col' colspan='2'><span class='folder' onclick=\"location.href='/filebrowser?path=" + fullPath + "'\">📂 " + fileName + "</span></td><td class='actions-col'></td></tr>";
      } else {
        html += "<tr>";
        html += "<td class='name-col'><div class='file-name'>📄 " + fileName + "</div></td>";
        html += "<td class='size-col'>" + String(st.st_size) + " bytes</td>";
        html += "<td class='actions-col'>";
        html += "<a href='/download?file=" + fullPath + "' class='btn'>Download</a>";
        html += "<button onclick=\"deleteFile('" + fullPath + "', '" + fileName + "')\" class='btn btn-danger'>Delete</button>";
        html += "</td>";
        html += "</tr>";
      }
    }
    closedir(dir);
  } else {
    html += "<tr><td colspan='3' style='text-align: center; color: #dc3545;'>Cannot open directory</td></tr>";
  }
  
  html += R"rawliteral(</tbody>
</table>
<a href="/" class="back">&larr; Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleFileDownload() {
  String filename = webServer.arg("file");
  
  // Use POSIX stat to check if file exists
  struct stat st;
  if (stat(filename.c_str(), &st) != 0) {
    webServer.send(404, "text/plain", "File not found");
    Serial.printf("❌ Download failed: %s not found\n", filename.c_str());
    return;
  }
  
  // Open file with POSIX
  FILE* file = fopen(filename.c_str(), "rb");
  if (!file) {
    webServer.send(500, "text/plain", "Cannot open file");
    Serial.printf("❌ Download failed: Cannot open %s\n", filename.c_str());
    return;
  }
  
  // Extract just the filename from the full path
  String displayName = filename;
  int lastSlash = displayName.lastIndexOf('/');
  if (lastSlash >= 0) {
    displayName = displayName.substring(lastSlash + 1);
  }
  
  // Set content type
  String contentType = "application/octet-stream";
  if (filename.endsWith(".txt")) contentType = "text/plain";
  else if (filename.endsWith(".wav")) contentType = "audio/wav";
  else if (filename.endsWith(".jpg") || filename.endsWith(".jpeg")) contentType = "image/jpeg";
  else if (filename.endsWith(".png")) contentType = "image/png";
  
  // Send headers
  webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + displayName + "\"");
  webServer.setContentLength(st.st_size);
  webServer.send(200, contentType, "");
  
  // Stream file in chunks
  uint8_t buffer[1024];
  size_t bytesRead;
  while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    webServer.client().write(buffer, bytesRead);
  }
  
  fclose(file);
  Serial.printf("📥 Downloaded: %s (%ld bytes)\n", filename.c_str(), st.st_size);
}

void handleFileUpload() {
  HTTPUpload& upload = webServer.upload();
  static FILE* uploadFile = NULL;
  static String uploadFilename;
  
  if (upload.status == UPLOAD_FILE_START) {
    String path = webServer.arg("path");
    if (!path.endsWith("/")) path += "/";
    uploadFilename = path + upload.filename;
    
    Serial.printf("📤 Uploading: %s\n", uploadFilename.c_str());
    uploadFile = fopen(uploadFilename.c_str(), "wb");
    if (!uploadFile) {
      Serial.println("❌ Failed to create upload file");
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      fwrite(upload.buf, 1, upload.currentSize, uploadFile);
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      fclose(uploadFile);
      uploadFile = NULL;
      Serial.printf("✅ Upload complete: %s (%d bytes)\n", uploadFilename.c_str(), upload.totalSize);
    }
    
    String path = webServer.arg("path");
    String redirect = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='1;url=/filebrowser?path=" + path + "'></head><body style='font-family: Arial; text-align: center; padding: 50px;'><h1 style='color: #28a745;'>Upload Complete!</h1><p>" + String(upload.totalSize) + " bytes uploaded</p><p>Redirecting...</p></body></html>";
    webServer.send(200, "text/html", redirect);
  }
}

void handleFileDelete() {
  String filename = webServer.arg("file");
  
  // Use POSIX remove
  if (remove(filename.c_str()) == 0) {
    Serial.printf("🗑️  Deleted: %s\n", filename.c_str());
    
    // Get parent directory for redirect
    String path = filename;
    int lastSlash = path.lastIndexOf('/');
    if (lastSlash > (int)strlen(SD_MOUNT_POINT)) {
      path = path.substring(0, lastSlash);
    } else {
      path = SD_MOUNT_POINT;
    }
    
    String redirect = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='1;url=/filebrowser?path=" + path + "'></head><body style='font-family: Arial; text-align: center; padding: 50px;'><h1 style='color: #28a745;'>File Deleted!</h1><p>Redirecting...</p></body></html>";
    webServer.send(200, "text/html", redirect);
  } else {
    Serial.printf("❌ Failed to delete: %s\n", filename.c_str());
    String errorPage = "<html><head><meta charset='UTF-8'></head><body style='font-family: Arial; text-align: center; padding: 50px;'><h1 style='color: #dc3545;'>Delete Failed</h1><p>Could not delete file</p><a href='/filebrowser' style='color: #667eea;'>Back to File Browser</a></body></html>";
    webServer.send(500, "text/html", errorPage);
  }
}

void handleAudioTest() {
  String action = webServer.arg("action");
  String result = "";
  
  // Handle test actions (NO SETTINGS - testing only)
  if (action == "record") {
    // Start 5-second recording with fixed test filename
    if (audio_start_recording("test_audio.wav")) {
      result = "🔴 Recording...";
      // Update recording buffer periodically
      for (int i = 0; i < 500; i++) {
        audio_update_recording();
        delay(10);  // 500 * 10ms = 5 seconds
      }
      if (audio_stop_recording()) {
        result = "✅ Recording saved: test_audio.wav<br>Click 'Play Test' to hear it back.";
      } else {
        result = "❌ Failed to stop recording.";
      }
    } else {
      result = "❌ Failed to start recording.";
    }
    
  } else if (action == "play") {
    // Play the test audio file
    String testFile = "/sdcard/recordings/test_audio.wav";
    struct stat st;
    if (stat(testFile.c_str(), &st) == 0) {
      if (audio_play_file(testFile.c_str())) {
        result = "🔊 Playing: test_audio.wav<br><small>Playback will continue in background. Click Stop to end.</small>";
      } else {
        result = "❌ Failed to start playback. Check serial logs.";
      }
    } else {
      result = "❌ test_audio.wav not found. Record first!";
    }
    
  } else if (action == "stop") {
    if (audio_stop_playback()) {
      result = "⏹️ Playback stopped.";
    } else {
      result = "⚠️ No playback in progress.";
    }
  }
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Audio Hardware Test</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #0a0e27; color: #fff; }
.container { max-width: 600px; margin: 0 auto; background: linear-gradient(135deg, #1e3a8a 0%, #3b0764 100%); padding: 40px; border-radius: 20px; box-shadow: 0 20px 60px rgba(0,0,0,0.5); }
h1 { color: #fff; margin-bottom: 10px; text-align: center; font-size: 32px; }
.subtitle { text-align: center; color: #a5b4fc; font-size: 14px; margin-bottom: 30px; }
.test-panel { background: rgba(255,255,255,0.1); backdrop-filter: blur(10px); padding: 30px; border-radius: 15px; margin: 20px 0; border: 1px solid rgba(255,255,255,0.2); }
.btn-group { display: flex; flex-direction: column; gap: 15px; margin: 20px 0; }
.btn { padding: 18px 24px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; text-decoration: none; border-radius: 12px; border: none; cursor: pointer; font-size: 18px; font-weight: bold; display: flex; align-items: center; justify-content: center; gap: 10px; transition: all 0.3s; }
.btn:hover { transform: translateY(-2px); box-shadow: 0 10px 20px rgba(102, 126, 234, 0.4); }
.btn-record { background: linear-gradient(135deg, #f43f5e 0%, #e11d48 100%); }
.btn-record:hover { box-shadow: 0 10px 20px rgba(244, 63, 94, 0.4); }
.btn-play { background: linear-gradient(135deg, #10b981 0%, #059669 100%); }
.btn-play:hover { box-shadow: 0 10px 20px rgba(16, 185, 129, 0.4); }
.btn-stop { background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%); }
.btn-stop:hover { box-shadow: 0 10px 20px rgba(239, 68, 68, 0.4); }
.result { background: rgba(16, 185, 129, 0.2); padding: 20px; border-radius: 12px; margin: 20px 0; border-left: 4px solid #10b981; color: #fff; font-size: 16px; }
.warning { background: rgba(251, 191, 36, 0.2); border-left-color: #fbbf24; }
.error { background: rgba(239, 68, 68, 0.2); border-left-color: #ef4444; }
.info-box { background: rgba(139, 92, 246, 0.15); padding: 15px; border-radius: 10px; margin: 15px 0; font-size: 14px; line-height: 1.6; }
.back { display: inline-block; margin-top: 20px; padding: 12px 24px; background: rgba(255,255,255,0.1); color: white; text-decoration: none; border-radius: 10px; border: 1px solid rgba(255,255,255,0.2); }
.back:hover { background: rgba(255,255,255,0.2); }
.status-badge { display: inline-block; padding: 6px 12px; border-radius: 20px; font-size: 12px; font-weight: bold; background: rgba(16, 185, 129, 0.2); color: #10b981; margin-top: 10px; }
</style>
</head>
<body>
<div class="container">
<h1>🎙️ Audio Hardware Test</h1>
<div class="subtitle">Test Microphone & Speaker Functionality</div>)rawliteral";

  if (result.length() > 0) {
    String resultClass = "result";
    if (result.indexOf("❌") >= 0) resultClass += " error";
    else if (result.indexOf("⚠️") >= 0) resultClass += " warning";
    html += "<div class='" + resultClass + "'>" + result + "</div>";
  }

  html += R"rawliteral(
<div class="info-box">
📌 <strong>Before Testing:</strong><br>
• Configure volume & mic gain in <a href="/devicesettings" style="color: #a5b4fc;">Device Settings</a><br>
• Make sure device is not in sleep mode<br>
• Speak clearly into the microphone during recording
</div>

<div class="test-panel">
<h3 style="margin-top: 0; color: #fff;">🎤 Microphone Test</h3>
<div class="btn-group">
<button onclick="location.href='/audiotest?action=record'" class="btn btn-record">
🔴 Record 5 Seconds
</button>
</div>
<p style="color: #a5b4fc; font-size: 14px; margin-top: 10px;">
Records a 5-second audio sample. Saved to: /recordings/rec_XXX.wav
</p>
</div>

<div class="test-panel">
<h3 style="margin-top: 0; color: #fff;">🔊 Speaker Test</h3>
<div class="btn-group">
<button onclick="location.href='/audiotest?action=play'" class="btn btn-play">
▶️ Play Last Recording
</button>
<button onclick="location.href='/audiotest?action=stop'" class="btn btn-stop">
⏹️ Stop Playback
</button>
</div>
<p style="color: #a5b4fc; font-size: 14px; margin-top: 10px;">
Plays back the most recent recording to test speaker output
</p>
</div>

<div class="info-box">
<strong>System Info:</strong>
<div class="status-badge">16 kHz Mono • 16-bit</div>
<br><span style="font-size: 12px; color: #94a3b8;">Max duration: ~16 seconds • PSRAM buffer: 512 KB</span>
</div>

<a href="/" class="back">← Back to Test Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleDeviceSettings() {
  String result = "";
  
  // Load current settings from NVS
  int brightness, volume, mic_gain;
  if (!NVS::loadSettings(&brightness, &volume, &mic_gain)) {
    // Defaults if not found
    volume = 70;
    mic_gain = 4;
  }
  
  int timezone = 0;
  if (!NVS::loadTimezone(&timezone)) {
    timezone = 0;  // Default UTC
  }
  
  // Handle settings changes
  if (webServer.hasArg("volume")) {
    int vol = webServer.arg("volume").toInt();
    if (vol >= 0 && vol <= 100) {
      audio_set_volume(vol);
      // Save to NVS
      int brightness, old_volume, mic_gain;
      if (NVS::loadSettings(&brightness, &old_volume, &mic_gain)) {
        NVS::saveSettings(brightness, vol, mic_gain);
      }
      volume = vol;  // Update display
      result = "✅ Volume set to: " + String(vol) + "% (saved to NVS)";
    }
  } else if (webServer.hasArg("gain")) {
    int gain = webServer.arg("gain").toInt();
    if (gain >= 0 && gain <= 7) {
      audio_set_mic_gain(gain);
      // Save to NVS
      int brightness, volume, old_gain;
      if (NVS::loadSettings(&brightness, &volume, &old_gain)) {
        NVS::saveSettings(brightness, volume, gain);
      }
      mic_gain = gain;  // Update display
      result = "✅ Mic gain set to: " + String(gain) + "/7 (saved to NVS)";
    }
  } else if (webServer.hasArg("timezone")) {
    int tz = webServer.arg("timezone").toInt();
    if (tz >= -12 && tz <= 14) {
      NVS::saveTimezone(tz);
      timezone = tz;
      result = "✅ Timezone set to: UTC" + String(tz >= 0 ? "+" : "") + String(tz) + " (saved to NVS)";
    }
  }
  
  String html = R"rawliteral(<!DOCTYPE html)
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Device Settings</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
h1 { color: #333; margin-bottom: 10px; }
.subtitle { color: #666; font-size: 14px; margin-bottom: 30px; }
.setting-section { background: #f8f9fa; padding: 25px; border-radius: 8px; margin: 20px 0; }
.setting-section h3 { margin-top: 0; color: #667eea; }
.control-row { display: flex; align-items: center; gap: 15px; margin: 15px 0; }
.slider { flex: 1; height: 8px; border-radius: 5px; background: #ddd; outline: none; cursor: pointer; }
.slider::-webkit-slider-thumb { width: 24px; height: 24px; border-radius: 50%; background: #667eea; cursor: pointer; appearance: none; }
.slider::-moz-range-thumb { width: 24px; height: 24px; border-radius: 50%; background: #667eea; cursor: pointer; border: none; }
.slider-value { display: inline-block; min-width: 60px; font-weight: bold; color: #667eea; font-size: 18px; }
.btn { padding: 12px 24px; background: #667eea; color: white; text-decoration: none; border-radius: 8px; border: none; cursor: pointer; font-size: 16px; font-weight: bold; }
.btn:hover { background: #5568d3; }
.result { background: #d4edda; padding: 15px; border-radius: 8px; margin: 15px 0; border-left: 4px solid #28a745; color: #155724; }
.info { background: #d1ecf1; padding: 15px; border-radius: 8px; margin: 15px 0; color: #0c5460; font-size: 14px; }
.back { display: inline-block; margin-top: 20px; padding: 10px 20px; background: #6c757d; color: white; text-decoration: none; border-radius: 5px; }
.back:hover { background: #5a6268; }
</style>
<script>
function updateVolume(val) {
  document.getElementById('volumeValue').innerText = val + '%';
}
function updateGain(val) {
  document.getElementById('gainValue').innerText = val;
}
function setVolume() {
  var vol = document.getElementById('volumeSlider').value;
  location.href='/devicesettings?volume=' + vol;
}
function setGain() {
  var gain = document.getElementById('gainSlider').value;
  location.href='/devicesettings?gain=' + gain;
}
function setTimezone() {
  var tz = document.getElementById('timezoneSelect').value;
  location.href='/devicesettings?timezone=' + tz;
}
</script>
</head>
<body>
<div class="container">
<h1>⚙️ Device Settings</h1>
<div class="subtitle">Volume, Mic Gain, and Timezone</div>)rawliteral";

  if (result.length() > 0) {
    html += "<div class='result'>" + result + "</div>";
  }

  html += R"rawliteral(
<div class="info">
💡 <strong>Tip:</strong> Changes are saved automatically to device memory
</div>

<div class="setting-section">
<h3>🔊 Speaker Volume</h3>
<div class="control-row">
<input type="range" min="0" max="100" value=")rawliteral";
  html += String(volume);
  html += R"rawliteral(" class="slider" id="volumeSlider" oninput="updateVolume(this.value)">
<span class="slider-value" id="volumeValue">)rawliteral";
  html += String(volume) + "%";
  html += R"rawliteral(</span>
</div>
<button onclick="setVolume()" class="btn">Apply Volume</button>
<p style="font-size: 13px; color: #666; margin-top: 15px;">
Adjust speaker output level (0 = muted, 100 = maximum)
</p>
</div>

<div class="setting-section">
<h3>🎙️ Microphone Gain</h3>
<div class="control-row">
<input type="range" min="0" max="7" value=")rawliteral";
  html += String(mic_gain);
  html += R"rawliteral(" class="slider" id="gainSlider" oninput="updateGain(this.value)">
<span class="slider-value" id="gainValue">)rawliteral";
  html += String(mic_gain);
  html += R"rawliteral(</span>
</div>
<button onclick="setGain()" class="btn">Apply Gain</button>
<p style="font-size: 13px; color: #666; margin-top: 15px;">
Microphone sensitivity (0 = lowest, 7 = highest). Recommended: 3-5
</p>
</div>

<div class="setting-section">
<h3>🌍 Timezone</h3>
<div class="control-row">
<select id="timezoneSelect" style="flex: 1; padding: 12px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px;">
<option value="-12")rawliteral";
  html += (timezone == -12) ? " selected" : "";
  html += R"rawliteral(>UTC-12</option>
<option value="-11")rawliteral";
  html += (timezone == -11) ? " selected" : "";
  html += R"rawliteral(>UTC-11</option>
<option value="-10")rawliteral";
  html += (timezone == -10) ? " selected" : "";
  html += R"rawliteral(>UTC-10</option>
<option value="-9")rawliteral";
  html += (timezone == -9) ? " selected" : "";
  html += R"rawliteral(>UTC-9</option>
<option value="-8")rawliteral";
  html += (timezone == -8) ? " selected" : "";
  html += R"rawliteral(>UTC-8</option>
<option value="-7")rawliteral";
  html += (timezone == -7) ? " selected" : "";
  html += R"rawliteral(>UTC-7</option>
<option value="-6")rawliteral";
  html += (timezone == -6) ? " selected" : "";
  html += R"rawliteral(>UTC-6</option>
<option value="-5")rawliteral";
  html += (timezone == -5) ? " selected" : "";
  html += R"rawliteral(>UTC-5</option>
<option value="-4")rawliteral";
  html += (timezone == -4) ? " selected" : "";
  html += R"rawliteral(>UTC-4</option>
<option value="-3")rawliteral";
  html += (timezone == -3) ? " selected" : "";
  html += R"rawliteral(>UTC-3</option>
<option value="-2")rawliteral";
  html += (timezone == -2) ? " selected" : "";
  html += R"rawliteral(>UTC-2</option>
<option value="-1")rawliteral";
  html += (timezone == -1) ? " selected" : "";
  html += R"rawliteral(>UTC-1</option>
<option value="0")rawliteral";
  html += (timezone == 0) ? " selected" : "";
  html += R"rawliteral(>UTC+0 (GMT)</option>
<option value="1")rawliteral";
  html += (timezone == 1) ? " selected" : "";
  html += R"rawliteral(>UTC+1</option>
<option value="2")rawliteral";
  html += (timezone == 2) ? " selected" : "";
  html += R"rawliteral(>UTC+2</option>
<option value="3")rawliteral";
  html += (timezone == 3) ? " selected" : "";
  html += R"rawliteral(>UTC+3</option>
<option value="4")rawliteral";
  html += (timezone == 4) ? " selected" : "";
  html += R"rawliteral(>UTC+4</option>
<option value="5")rawliteral";
  html += (timezone == 5) ? " selected" : "";
  html += R"rawliteral(>UTC+5</option>
<option value="6")rawliteral";
  html += (timezone == 6) ? " selected" : "";
  html += R"rawliteral(>UTC+6</option>
<option value="7")rawliteral";
  html += (timezone == 7) ? " selected" : "";
  html += R"rawliteral(>UTC+7</option>
<option value="8")rawliteral";
  html += (timezone == 8) ? " selected" : "";
  html += R"rawliteral(>UTC+8</option>
<option value="9")rawliteral";
  html += (timezone == 9) ? " selected" : "";
  html += R"rawliteral(>UTC+9</option>
<option value="10")rawliteral";
  html += (timezone == 10) ? " selected" : "";
  html += R"rawliteral(>UTC+10</option>
<option value="11")rawliteral";
  html += (timezone == 11) ? " selected" : "";
  html += R"rawliteral(>UTC+11</option>
<option value="12")rawliteral";
  html += (timezone == 12) ? " selected" : "";
  html += R"rawliteral(>UTC+12</option>
<option value="13")rawliteral";
  html += (timezone == 13) ? " selected" : "";
  html += R"rawliteral(>UTC+13</option>
<option value="14")rawliteral";
  html += (timezone == 14) ? " selected" : "";
  html += R"rawliteral(>UTC+14</option>
</select>
</div>
<button onclick="setTimezone()" class="btn">Apply Timezone</button>
<p style="font-size: 13px; color: #666; margin-top: 15px;">
Select your local timezone offset from UTC for accurate time display
</p>
</div>

<a href="/" class="back">← Back to Home</a>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleServerConfig() {
  // Get current server config from NVS
  char url[256] = "";
  NVS::loadServerURL(url, 256);
  String currentUrl = String(url);
  
  char secret[65] = "";
  if (!NVS::loadDeviceSecret(secret)) {
    strcpy(secret, "0000000000000000000000000000000000000000000000000000000000000000");  // Default
  }
  String currentSecret = String(secret);
  
  // Get current Root CA from NVS (or empty if using default)
  char rootCA[4096] = "";
  bool hasCustomCA = NVS::loadRootCA(rootCA, 4096);
  String currentCA = hasCustomCA ? String(rootCA) : String("");
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Server Configuration</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
h1 { color: #333; margin-bottom: 20px; }
.form-group { margin-bottom: 20px; }
label { display: block; margin-bottom: 5px; font-weight: bold; color: #666; }
input { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }
.btn { display: inline-block; padding: 12px 24px; background: #667eea; color: white; text-decoration: none; border-radius: 8px; border: none; cursor: pointer; font-size: 16px; }
.btn:hover { background: #5568d3; }
.back { background: #6c757d; margin-left: 10px; }
.info { background: #d1ecf1; padding: 15px; border-radius: 5px; margin-bottom: 20px; color: #0c5460; }
</style>
</head>
<body>
<div class="container">
<h1>🌐 Server Configuration</h1>
<div class="info">
<strong>Voice Assistant Server Configuration</strong><br>
Configure server URL and device authentication secret<br>
Device MAC: )rawliteral" + deviceMAC + R"rawliteral(
</div>
<form action="/saveserver" method="POST">
<div class="form-group">
<label>Server URL:</label>
<input type="text" name="url" value=")rawliteral" + currentUrl + R"rawliteral(" placeholder="https://your-server.example.com/transcribe" required>
</div>
<div class="form-group">
<label>Device Secret (64 hex characters):</label>
<input type="text" name="secret" value=")rawliteral" + currentSecret + R"rawliteral(" pattern="[0-9a-fA-F]{64}" maxlength="64" required style="font-family: monospace;">
<small style="color: #666; font-size: 12px;">HMAC-SHA256 key for device authentication</small>
</div>

<details style="margin-top: 20px; border: 1px solid #ddd; padding: 15px; border-radius: 5px;">
<summary style="cursor: pointer; font-weight: bold; color: #667eea;">🔐 SSL Root CA Certificate (Advanced)</summary>
<div style="margin-top: 15px; background: #f8f9fa; padding: 15px; border-radius: 5px; font-size: 13px;">
<strong>Instructions for obtaining new certificates:</strong><br><br>
1. Visit your server domain in Chrome/Firefox<br>
2. Click the padlock icon in address bar → "Connection is secure"<br>
3. Click "Certificate is valid" (Chrome) or "More Information" (Firefox)<br>
4. Export the <strong>entire certificate chain</strong> (not just the leaf cert)<br>
5. Open exported file in text editor and copy ALL certificates<br>
6. Paste below including BEGIN/END lines<br><br>
<strong>Format:</strong> PEM format with full chain (leaf + intermediates + root)<br>
<strong>Max size:</strong> 4KB (typical certs are 1-2KB)<br>
<strong>Leave blank to use hardcoded default certificate</strong>
</div>
<div class="form-group" style="margin-top: 15px;">
<label>Root CA Certificate (PEM format):</label>
<textarea name="rootca" rows="15" style="width: 100%; padding: 10px; font-family: monospace; font-size: 11px; border: 1px solid #ddd; border-radius: 5px;" placeholder="-----BEGIN CERTIFICATE-----
(paste certificate chain here)
-----END CERTIFICATE-----">)rawliteral" + currentCA + R"rawliteral(</textarea>
<small style="color: #666; font-size: 12px;">
Current: )rawliteral" + (hasCustomCA ? String("Custom cert from NVS (") + String(strlen(rootCA)) + String(" bytes)") : String("Using hardcoded default")) + R"rawliteral(
</small>
</div>
</details>

<button type="submit" class="btn">💾 Save Configuration</button>
<a href="/" class="btn back">← Back</a>
</form>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleSaveServer() {
  String url = webServer.arg("url");
  String secret = webServer.arg("secret");
  String rootCA = webServer.arg("rootca");
  
  // Validate secret
  if (secret.length() != 64) {
    webServer.send(400, "text/html", "<html><body><h1>Error</h1><p>Device secret must be exactly 64 hex characters!</p><a href='/serverconfig'>Go Back</a></body></html>");
    return;
  }
  
  // Use our NVS manager
  bool success = NVS::saveServerURL(url.c_str());
  bool secretSuccess = NVS::saveDeviceSecret(secret.c_str());
  
  // Handle SSL certificate
  bool certSuccess = true;
  if (rootCA.length() > 0) {
    // Trim whitespace
    rootCA.trim();
    
    // Save custom certificate
    certSuccess = NVS::saveRootCA(rootCA.c_str());
    if (certSuccess) {
      Serial.printf("✅ Custom Root CA saved (%d bytes)\n", rootCA.length());
    } else {
      Serial.println("❌ Failed to save Root CA certificate");
    }
  } else {
    // Clear certificate (will use default)
    NVS::clearRootCA();
    Serial.println("✅ Cleared custom Root CA (will use hardcoded default)");
  }
  
  success = success && secretSuccess && certSuccess;
  
  if (success) {
    Serial.printf("✅ Server URL saved: %s\n", url.c_str());
  } else {
    Serial.println("❌ Failed to save server URL!");
  }
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta http-equiv="refresh" content="2;url=/">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Server Saved</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; text-align: center; }
.container { max-width: 500px; margin: 50px auto; background: white; padding: 40px; border-radius: 10px; }
h1 { color: #28a745; }
</style>
</head>
<body>
<div class="container">
<h1>✅ Server Configuration Saved!</h1>
<p>URL: )rawliteral" + url + R"rawliteral(</p>
<p>Redirecting to home...</p>
</div>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleFirmwareUpdate() {
  // Get OTA partition info
  const esp_partition_t *running = esp_ota_get_running_partition();
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  
  String runningLabel = (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? "app0 (ota_0)" : "app1 (ota_1)";
  String updateLabel = (update_partition->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? "app0 (ota_0)" : "app1 (ota_1)";
  
  String html = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Firmware Update (OTA)</title>
<style>
body { font-family: Arial, sans-serif; padding: 20px; background: #f5f5f5; }
.container { max-width: 700px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
h1 { color: #333; margin-bottom: 20px; }
.info-box { background: #d1ecf1; padding: 20px; border-radius: 8px; margin: 20px 0; color: #0c5460; }
.warning-box { background: #fff3cd; padding: 20px; border-radius: 8px; margin: 20px 0; color: #856404; }
.partition-info { background: #f8f9fa; padding: 15px; border-radius: 8px; margin: 15px 0; }
.upload-section { background: #e7f3ff; padding: 25px; border-radius: 8px; margin: 20px 0; }
input[type="file"] { width: 100%; padding: 12px; border: 2px dashed #667eea; border-radius: 8px; margin: 15px 0; cursor: pointer; }
.btn { display: inline-block; padding: 14px 28px; background: #667eea; color: white; text-decoration: none; border-radius: 8px; border: none; cursor: pointer; font-size: 16px; font-weight: bold; }
.btn:hover { background: #5568d3; }
.btn:disabled { background: #ccc; cursor: not-allowed; }
.back { background: #6c757d; margin-left: 10px; }
.progress-container { display: none; margin: 20px 0; }
.progress-bar { width: 100%; height: 30px; background: #e9ecef; border-radius: 15px; overflow: hidden; }
.progress-fill { height: 100%; background: linear-gradient(90deg, #667eea 0%, #764ba2 100%); width: 0%; transition: width 0.3s; }
.progress-text { text-align: center; margin-top: 10px; font-weight: bold; }
.status { margin: 20px 0; padding: 15px; border-radius: 8px; display: none; }
.status.success { background: #d4edda; color: #155724; display: block; }
.status.error { background: #f8d7da; color: #721c24; display: block; }
</style>
</head>
<body>
<div class="container">
<h1>🔄 Firmware Update (OTA)</h1>

<div class="info-box">
<strong>📋 Current Partition Information:</strong><br>
<div class="partition-info">
Running from: <strong>)rawliteral" + runningLabel + R"rawliteral(</strong><br>
Update target: <strong>)rawliteral" + updateLabel + R"rawliteral(</strong><br>
Flash size: <strong>)rawliteral" + String(ESP.getFlashChipSize() / (1024 * 1024)) + R"rawliteral( MB</strong>
</div>
</div>

<div class="warning-box">
<strong>⚠️ Important Notes:</strong><br>
• Upload only .bin firmware files from PlatformIO build<br>
• File location: <code>.pio/build/esp32-s3-n16r8/firmware.bin</code><br>
• Do NOT disconnect power during update<br>
• Device will reboot automatically after successful update<br>
• <strong>Rollback protection:</strong> If new firmware fails to boot, device automatically reverts to current firmware
</div>

<div class="upload-section">
<h3 style="margin-top: 0;">📤 Upload New Firmware</h3>
<form id="uploadForm" method="POST" enctype="multipart/form-data">
<input type="file" id="firmwareFile" name="firmware" accept=".bin" required>
<button type="submit" class="btn" id="uploadBtn">🚀 Upload & Flash Firmware</button>
<a href="/" class="btn back">← Cancel</a>
</form>
</div>

<div class="progress-container" id="progressContainer">
<div class="progress-bar">
<div class="progress-fill" id="progressFill"></div>
</div>
<div class="progress-text" id="progressText">0%</div>
</div>

<div class="status" id="status"></div>
</div>

<script>
document.getElementById('uploadForm').addEventListener('submit', function(e) {
  e.preventDefault();
  
  var fileInput = document.getElementById('firmwareFile');
  var file = fileInput.files[0];
  
  if (!file) {
    alert('Please select a firmware file');
    return;
  }
  
  if (!file.name.endsWith('.bin')) {
    alert('Please select a .bin firmware file');
    return;
  }
  
  // Show progress
  document.getElementById('progressContainer').style.display = 'block';
  document.getElementById('uploadBtn').disabled = true;
  document.getElementById('status').style.display = 'none';
  
  var formData = new FormData();
  formData.append('firmware', file);
  
  var xhr = new XMLHttpRequest();
  
  xhr.upload.addEventListener('progress', function(e) {
    if (e.lengthComputable) {
      var percent = Math.round((e.loaded / e.total) * 100);
      document.getElementById('progressFill').style.width = percent + '%';
      document.getElementById('progressText').innerText = percent + '%';
    }
  });
  
  xhr.addEventListener('load', function() {
    if (xhr.status === 200) {
      document.getElementById('status').className = 'status success';
      document.getElementById('status').innerHTML = '<strong>✅ Firmware uploaded successfully!</strong><br>Device will reboot in 3 seconds...';
      setTimeout(function() {
        window.location.href = '/';
      }, 3000);
    } else {
      document.getElementById('status').className = 'status error';
      document.getElementById('status').innerHTML = '<strong>❌ Update failed!</strong><br>' + xhr.responseText;
      document.getElementById('uploadBtn').disabled = false;
    }
  });
  
  xhr.addEventListener('error', function() {
    document.getElementById('status').className = 'status error';
    document.getElementById('status').innerHTML = '<strong>❌ Upload error!</strong><br>Connection lost';
    document.getElementById('uploadBtn').disabled = false;
  });
  
  xhr.open('POST', '/uploadfirmware');
  xhr.send(formData);
});
</script>
</body>
</html>)rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleFirmwareUploadPage() {
  HTTPUpload& upload = webServer.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("\n🔄 OTA Update Started: %s\n", upload.filename.c_str());
    Serial.printf("   File size: %u bytes\n", upload.totalSize);
    
    // Begin OTA update
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
      webServer.send(500, "text/plain", "OTA begin failed");
      return;
    }
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write firmware chunk
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
      webServer.send(500, "text/plain", "OTA write failed");
      return;
    }
    
    // Print progress
    static size_t lastPrintSize = 0;
    if (upload.totalSize - lastPrintSize >= 102400) {  // Print every 100KB
      Serial.printf("   Uploaded: %u bytes\n", upload.totalSize);
      lastPrintSize = upload.totalSize;
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    // Finish OTA update
    if (Update.end(true)) {
      Serial.printf("✅ OTA Update Success: %u bytes\n", upload.totalSize);
      Serial.println("   Device will reboot in 2 seconds...");
      
      webServer.send(200, "text/plain", "Update successful! Rebooting...");
      
      delay(2000);
      ESP.restart();
      
    } else {
      Update.printError(Serial);
      webServer.send(500, "text/plain", "OTA end failed");
    }
    
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("❌ OTA Update aborted");
    Update.abort();
    webServer.send(500, "text/plain", "Update aborted");
  }
}

void handleNotFound() {
  // Captive portal redirect
  webServer.sendHeader("Location", "/", true);
  webServer.send(302, "text/plain", "");
}
