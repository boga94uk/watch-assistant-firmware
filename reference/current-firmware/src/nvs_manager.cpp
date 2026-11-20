#include "nvs_manager.h"
#include <Preferences.h>

// NVS namespace for our app
#define NVS_NAMESPACE "esp_assist"

// Keys
#define KEY_WIFI_SSID       "wifi_ssid"
#define KEY_WIFI_PASSWORD   "wifi_pass"
#define KEY_SERVER_URL      "server_url"
#define KEY_ROOT_CA         "root_ca"      // SSL Root CA certificate
#define KEY_BRIGHTNESS      "brightness"
#define KEY_VOLUME          "volume"
#define KEY_MIC_GAIN        "mic_gain"
#define KEY_DEVICE_SECRET   "dev_secret"
#define KEY_TIMEZONE        "timezone"

// Global Preferences instance
static Preferences prefs;

namespace NVS {

bool init() {
    Serial.println("📦 Initializing NVS Manager...");
    // Preferences lib initializes NVS automatically
    Serial.println("✅ NVS Manager ready");
    return true;
}

// ============================================
// WIFI CREDENTIALS
// ============================================

bool saveWiFiConfig(const char* ssid, const char* password) {
    if (!ssid || !password) return false;
    
    prefs.begin(NVS_NAMESPACE, false);  // Open in read/write mode
    
    bool success = true;
    if (!prefs.putString(KEY_WIFI_SSID, ssid)) {
        Serial.println("❌ Failed to save WiFi SSID");
        success = false;
    }
    if (!prefs.putString(KEY_WIFI_PASSWORD, password)) {
        Serial.println("❌ Failed to save WiFi password");
        success = false;
    }
    
    prefs.end();
    
    if (success) {
        Serial.printf("✅ WiFi config saved: %s\n", ssid);
    }
    
    return success;
}

bool loadWiFiConfig(char* ssid, char* password, size_t max_len) {
    if (!ssid || !password) return false;
    
    prefs.begin(NVS_NAMESPACE, true);  // Open in read-only mode
    
    String ssid_str = prefs.getString(KEY_WIFI_SSID, "");
    String pass_str = prefs.getString(KEY_WIFI_PASSWORD, "");
    
    prefs.end();
    
    if (ssid_str.length() == 0) {
        return false;  // No WiFi config stored
    }
    
    strncpy(ssid, ssid_str.c_str(), max_len - 1);
    strncpy(password, pass_str.c_str(), max_len - 1);
    ssid[max_len - 1] = '\0';
    password[max_len - 1] = '\0';
    
    Serial.printf("✅ WiFi config loaded: %s\n", ssid);
    return true;
}

bool hasWiFiConfig() {
    prefs.begin(NVS_NAMESPACE, true);
    String ssid = prefs.getString(KEY_WIFI_SSID, "");
    prefs.end();
    return ssid.length() > 0;
}

void clearWiFiConfig() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(KEY_WIFI_SSID);
    prefs.remove(KEY_WIFI_PASSWORD);
    prefs.end();
    Serial.println("🗑️  WiFi config cleared");
}

// ============================================
// SERVER CONFIGURATION
// ============================================

bool saveServerURL(const char* url) {
    if (!url) return false;
    
    prefs.begin(NVS_NAMESPACE, false);
    bool success = prefs.putString(KEY_SERVER_URL, url);
    prefs.end();
    
    if (success) {
        Serial.printf("✅ Server URL saved: %s\n", url);
    } else {
        Serial.println("❌ Failed to save server URL");
    }
    
    return success;
}

bool loadServerURL(char* url, size_t max_len) {
    if (!url) return false;
    
    prefs.begin(NVS_NAMESPACE, true);
    String url_str = prefs.getString(KEY_SERVER_URL, "");
    prefs.end();
    
    if (url_str.length() == 0) {
        return false;  // No server URL stored
    }
    
    strncpy(url, url_str.c_str(), max_len - 1);
    url[max_len - 1] = '\0';
    
    Serial.printf("✅ Server URL loaded: %s\n", url);
    return true;
}

bool hasServerURL() {
    prefs.begin(NVS_NAMESPACE, true);
    String url = prefs.getString(KEY_SERVER_URL, "");
    prefs.end();
    return url.length() > 0;
}

void clearServerURL() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(KEY_SERVER_URL);
    prefs.end();
    Serial.println("🗑️  Server URL cleared");
}

// ============================================
// SSL CERTIFICATES
// ============================================

bool saveRootCA(const char* cert) {
    if (!cert || strlen(cert) == 0) return false;
    
    // Check size (NVS supports up to ~4KB per key)
    size_t cert_len = strlen(cert);
    if (cert_len > 4096) {
        Serial.printf("❌ Certificate too large: %u bytes (max 4096)\n", cert_len);
        return false;
    }
    
    prefs.begin(NVS_NAMESPACE, false);
    bool success = prefs.putString(KEY_ROOT_CA, cert);
    prefs.end();
    
    if (success) {
        Serial.printf("✅ Root CA certificate saved (%u bytes)\n", cert_len);
    } else {
        Serial.println("❌ Failed to save Root CA certificate");
    }
    
    return success;
}

bool loadRootCA(char* cert, size_t max_len) {
    if (!cert) return false;
    
    prefs.begin(NVS_NAMESPACE, true);
    String cert_str = prefs.getString(KEY_ROOT_CA, "");
    prefs.end();
    
    if (cert_str.length() == 0) {
        return false;  // No certificate stored
    }
    
    if (cert_str.length() >= max_len) {
        Serial.println("❌ Certificate buffer too small");
        return false;
    }
    
    strncpy(cert, cert_str.c_str(), max_len - 1);
    cert[max_len - 1] = '\0';
    
    Serial.printf("✅ Root CA certificate loaded (%u bytes)\n", cert_str.length());
    return true;
}

bool hasRootCA() {
    prefs.begin(NVS_NAMESPACE, true);
    String cert = prefs.getString(KEY_ROOT_CA, "");
    prefs.end();
    return cert.length() > 0;
}

void clearRootCA() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(KEY_ROOT_CA);
    prefs.end();
    Serial.println("🗑️  Root CA certificate cleared (will use hardcoded default)");
}

// ============================================
// USER SETTINGS
// ============================================

bool saveSettings(int brightness, int volume, int mic_gain) {
    prefs.begin(NVS_NAMESPACE, false);
    
    bool success = true;
    
    // Save only non-negative values (-1 = skip)
    if (brightness >= 0) {
        if (!prefs.putInt(KEY_BRIGHTNESS, brightness)) success = false;
    }
    if (volume >= 0) {
        if (!prefs.putInt(KEY_VOLUME, volume)) success = false;
    }
    if (mic_gain >= 0) {
        if (!prefs.putInt(KEY_MIC_GAIN, mic_gain)) success = false;
    }
    
    prefs.end();
    
    if (success) {
        Serial.printf("✅ Settings saved: ");
        if (brightness >= 0) Serial.printf("brightness=%d ", brightness);
        if (volume >= 0) Serial.printf("volume=%d ", volume);
        if (mic_gain >= 0) Serial.printf("mic_gain=%d", mic_gain);
        Serial.println();
    } else {
        Serial.println("❌ Failed to save settings");
    }
    
    return success;
}

bool loadSettings(int* brightness, int* volume, int* mic_gain) {
    if (!brightness || !volume || !mic_gain) return false;
    
    prefs.begin(NVS_NAMESPACE, true);
    
    // Check if settings exist (brightness is required)
    if (!prefs.isKey(KEY_BRIGHTNESS)) {
        prefs.end();
        return false;
    }
    
    *brightness = prefs.getInt(KEY_BRIGHTNESS, 80);  // Default: 80%
    *volume = prefs.getInt(KEY_VOLUME, 70);          // Default: 70%
    *mic_gain = prefs.getInt(KEY_MIC_GAIN, 4);       // Default: 4/7
    
    prefs.end();
    
    Serial.printf("✅ Settings loaded: brightness=%d, volume=%d, mic_gain=%d\n", 
                  *brightness, *volume, *mic_gain);
    return true;
}

bool hasSettings() {
    prefs.begin(NVS_NAMESPACE, true);
    bool exists = prefs.isKey(KEY_BRIGHTNESS);
    prefs.end();
    return exists;
}

bool saveDeviceSecret(const char* secret) {
    if (!secret || strlen(secret) != 64) return false;
    
    prefs.begin(NVS_NAMESPACE, false);
    bool success = prefs.putString(KEY_DEVICE_SECRET, secret);
    prefs.end();
    
    if (success) {
        Serial.println("🔐 Device secret saved to NVS");
    } else {
        Serial.println("❌ Failed to save device secret");
    }
    return success;
}

bool loadDeviceSecret(char* secret) {
    if (!secret) return false;
    
    prefs.begin(NVS_NAMESPACE, true);
    String loaded = prefs.getString(KEY_DEVICE_SECRET, "");
    prefs.end();
    
    if (loaded.length() == 64) {
        strcpy(secret, loaded.c_str());
        Serial.println("🔐 Device secret loaded from NVS");
        return true;
    }
    return false;
}

bool saveTimezone(int timezone_hours) {
    if (timezone_hours < -12 || timezone_hours > 14) return false;
    
    prefs.begin(NVS_NAMESPACE, false);
    bool success = prefs.putInt(KEY_TIMEZONE, timezone_hours);
    prefs.end();
    
    if (success) {
        Serial.printf("🌍 Timezone UTC%+d saved to NVS\n", timezone_hours);
    } else {
        Serial.println("❌ Failed to save timezone");
    }
    return success;
}

bool loadTimezone(int* timezone_hours) {
    if (!timezone_hours) return false;
    
    prefs.begin(NVS_NAMESPACE, true);
    
    if (!prefs.isKey(KEY_TIMEZONE)) {
        prefs.end();
        return false;
    }
    
    *timezone_hours = prefs.getInt(KEY_TIMEZONE, 0);
    prefs.end();
    
    Serial.printf("🌍 Timezone loaded: UTC%+d\n", *timezone_hours);
    return true;
}

void clearSettings() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.remove(KEY_BRIGHTNESS);
    prefs.remove(KEY_VOLUME);
    prefs.remove(KEY_MIC_GAIN);
    prefs.end();
    Serial.println("🗑️  Settings cleared");
}

// ============================================
// UTILITY
// ============================================

void clearAll() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();  // Clear all keys in namespace
    prefs.end();
    Serial.println("🗑️  All NVS data cleared!");
}

void printConfig() {
    Serial.println("\n📋 NVS Configuration:");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    prefs.begin(NVS_NAMESPACE, true);
    
    // WiFi
    String ssid = prefs.getString(KEY_WIFI_SSID, "");
    String pass = prefs.getString(KEY_WIFI_PASSWORD, "");
    if (ssid.length() > 0) {
        Serial.printf("📡 WiFi SSID: %s\n", ssid.c_str());
        Serial.printf("📡 WiFi Password: %s\n", pass.length() > 0 ? "********" : "(none)");
    } else {
        Serial.println("📡 WiFi: Not configured");
    }
    
    // Server
    String url = prefs.getString(KEY_SERVER_URL, "");
    if (url.length() > 0) {
        Serial.printf("🌐 Server URL: %s\n", url.c_str());
    } else {
        Serial.println("🌐 Server URL: Not configured");
    }
    
    // Settings
    if (prefs.isKey(KEY_BRIGHTNESS)) {
        int brightness = prefs.getInt(KEY_BRIGHTNESS, 80);
        int volume = prefs.getInt(KEY_VOLUME, 70);
        int mic_gain = prefs.getInt(KEY_MIC_GAIN, 4);
        Serial.printf("⚙️  Brightness: %d%%\n", brightness);
        Serial.printf("⚙️  Volume: %d%%\n", volume);
        Serial.printf("⚙️  Mic Gain: %d/7\n", mic_gain);
    } else {
        Serial.println("⚙️  Settings: Not configured (using defaults)");
    }
    
    prefs.end();
    
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

} // namespace NVS
