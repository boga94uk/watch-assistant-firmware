// WIFI MANAGER - Connection & Auto-Reconnect
// ============================================

#include "wifi_manager.h"
#include "nvs_manager.h"
#include "rtc_manager.h"
#include "https_client.h"
#include "settings_manager.h"
#include <WiFi.h>
#include <time.h>

// WiFi state
static bool wifi_initialized = false;
static bool wifi_connecting = false;
static unsigned long last_reconnect_attempt = 0;
static const unsigned long RECONNECT_INTERVAL = 10000;  // 10 seconds
static bool ntp_synced = false;
static bool https_initialized = false;

// Credentials (loaded from NVS)
static char saved_ssid[64] = "";
static char saved_password[64] = "";
static bool has_credentials = false;

// NTP sync function
void syncTimeWithNTP() {
  Serial.println("🕐 Syncing time with NTP...");
  
  // Reset system time to epoch 0 to force NTP to actually sync
  // (Otherwise it may keep the SD fallback time)
  struct timeval tv = {0, 0};
  settimeofday(&tv, NULL);
  
  // Configure NTP (UTC timezone)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // Wait for NTP to provide a valid time (2024-2100)
  int retry = 0;
  const int retry_count = 30;  // 30 * 500ms = 15 seconds
  bool synced = false;
  
  while (retry < retry_count && !synced) {
    delay(500);
    Serial.print(".");
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    int year = timeinfo.tm_year + 1900;
    
    // Check if we got a valid year (system time was 0, so any valid year means NTP worked)
    if (year >= 2024 && year <= 2100) {
      Serial.println();
      Serial.printf("✅ Time synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                   year, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      
      // Update RTC with NTP time + timezone offset
      if (rtcInitialized) {
        // Get timezone offset from settings
        int timezone_hours = settings_manager_get_timezone();
        
        // Apply timezone offset (convert to seconds)
        time_t local_time = now + (timezone_hours * 3600);
        struct tm local_timeinfo;
        localtime_r(&local_time, &local_timeinfo);
        
        int local_year = local_timeinfo.tm_year + 1900;
        
        rtc.setDateTime(local_year, local_timeinfo.tm_mon + 1, local_timeinfo.tm_mday,
                       local_timeinfo.tm_hour, local_timeinfo.tm_min, local_timeinfo.tm_sec);
        
        Serial.printf("✅ RTC synced: %04d-%02d-%02d %02d:%02d:%02d (UTC%+d)\n",
                     local_year, local_timeinfo.tm_mon + 1, local_timeinfo.tm_mday,
                     local_timeinfo.tm_hour, local_timeinfo.tm_min, local_timeinfo.tm_sec,
                     timezone_hours);
      }
      
      ntp_synced = true;
      synced = true;
    }
    
    retry++;
  }
  
  if (!synced) {
    Serial.println();
    Serial.println("⚠️  NTP sync timeout - will retry on next WiFi connect");
  }
}

bool initWiFi() {
  Serial.println("\n📡 Initializing WiFi manager...");
  
  // Set WiFi mode
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // We'll handle reconnect ourselves
  
  // Load credentials from NVS
  if (!NVS::loadWiFiConfig(saved_ssid, saved_password, 64)) {
    Serial.println("⚠️  No WiFi credentials in NVS");
    Serial.println("   Configure via test mode web interface");
    wifi_initialized = true;  // Init successful, just no credentials
    has_credentials = false;
    return false;
  }
  
  Serial.printf("✅ WiFi credentials loaded: SSID='%s'\n", saved_ssid);
  wifi_initialized = true;
  has_credentials = true;
  
  // Start connection attempt
  return connectWiFi();
}

bool connectWiFi() {
  if (!has_credentials || saved_ssid[0] == '\0') {
    Serial.println("⚠️  Cannot connect: No WiFi credentials");
    return false;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ Already connected to WiFi");
    return true;
  }
  
  Serial.printf("📡 Connecting to WiFi: %s\n", saved_ssid);
  WiFi.begin(saved_ssid, saved_password);
  wifi_connecting = true;
  last_reconnect_attempt = millis();
  
  return true;
}

void disconnectWiFi() {
  Serial.println("📡 Disconnecting WiFi...");
  WiFi.disconnect(true);
  wifi_connecting = false;
}

bool updateWiFi() {
  if (!wifi_initialized) {
    return false;
  }
  
  wl_status_t status = WiFi.status();
  
  // Connection successful
  if (status == WL_CONNECTED) {
    if (wifi_connecting) {
      wifi_connecting = false;
      Serial.printf("✅ WiFi: %s (IP: %s, %d dBm)\n", 
                   WiFi.SSID().c_str(), 
                   WiFi.localIP().toString().c_str(),
                   WiFi.RSSI());
      
      // Sync time with NTP (will retry if previous sync failed)
      syncTimeWithNTP();
      
      // Initialize HTTPS client on first connect
      if (!https_initialized) {
        initHttpsClient();
        https_initialized = true;
      }
    }
    return true;
  }
  
  // Connection failed or lost
  if (wifi_connecting) {
    // Still connecting, check timeout
    if (millis() - last_reconnect_attempt > 15000) {
      Serial.println("❌ WiFi connection timeout");
      wifi_connecting = false;
    }
  } else {
    // Not connecting, check if we should auto-reconnect
    if (has_credentials && saved_ssid[0] != '\0' && 
        millis() - last_reconnect_attempt > RECONNECT_INTERVAL) {
      Serial.println("🔄 WiFi disconnected - attempting reconnect...");
      connectWiFi();
    }
  }
  
  return false;
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

int getWiFiSignalBars() {
  if (!isWiFiConnected()) {
    return 0;
  }
  
  int rssi = WiFi.RSSI();
  
  // Convert RSSI to bars (0-4)
  if (rssi >= -50) return 4;      // Excellent
  if (rssi >= -60) return 3;      // Good
  if (rssi >= -70) return 2;      // Fair
  if (rssi >= -80) return 1;      // Weak
  return 0;                       // Very weak/disconnected
}

int getWiFiRSSI() {
  if (!isWiFiConnected()) {
    return -100;  // No signal
  }
  return WiFi.RSSI();
}

String getWiFiSSID() {
  if (!isWiFiConnected()) {
    return "";
  }
  return WiFi.SSID();
}

String getWiFiIP() {
  if (!isWiFiConnected()) {
    return "0.0.0.0";
  }
  return WiFi.localIP().toString();
}
