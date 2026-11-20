#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "mbedtls/md.h"
#include "esp_heap_caps.h"
#include <FS.h>
#include <SD.h>

#include "https_client.h"
#include "audio/audio_manager.h"
#include "settings_manager.h"
#include "display/display_manager.h"
#include "nvs_manager.h"
#include "sd_manager.h"
#include <lvgl.h>
#include <esp_task_wdt.h>

// =====================================================
// ====== GLOBAL SERVER / DEVICE STATE =================
// =====================================================

// These are declared extern in https_client.h
String serverUrl = "https://your-server.example.com/transcribe";
String deviceMac = "";
String deviceId  = "";

// Cancel flag (set elsewhere if user cancels mid-upload)
volatile bool g_upload_cancel = false;

// Default Root CA chain for your-server.example.com
// This is used as fallback if no certificate is stored in NVS
static const char *DEFAULT_ROOT_CA = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDwTCCA2egAwIBAgIRAIdav1ABKgsjDka0rY+/9EwwCgYIKoZIzj0EAwIwOzEL
MAkGA1UEBhMCVVMxHjAcBgNVBAoTFUdvb2dsZSBUcnVzdCBTZXJ2aWNlczEMMAoG
A1UEAxMDV0UxMB4XDTI1MTAxMjE2MjY1MVoXDTI2MDExMDE3MjUzM1owHzEdMBsG
A1UEAxMUZXNwY29ubmVjdGlvbi5xenouaW8wWTATBgcqhkjOPQIBBggqhkjOPQMB
BwNCAATJ3AZX2nf6bMDMHI+gRFlsuuhJSCNgEGlbamwqENG8rnINgyyPbf1YEdsl
WoQrCKQ8NpaJC+dhQzTtoyzQgPKmo4ICZjCCAmIwDgYDVR0PAQH/BAQDAgeAMBMG
A1UdJQQMMAoGCCsGAQUFBwMBMAwGA1UdEwEB/wQCMAAwHQYDVR0OBBYEFB8W/pQ1
D6c7/uPIe3s+8ItgQeVlMB8GA1UdIwQYMBaAFJB3kjVnxP+ozKnme9mAeXvMk/k4
MF4GCCsGAQUFBwEBBFIwUDAnBggrBgEFBQcwAYYbaHR0cDovL28ucGtpLmdvb2cv
cy93ZTEvaDFvMCUGCCsGAQUFBzAChhlodHRwOi8vaS5wa2kuZ29vZy93ZTEuY3J0
MDcGA1UdEQQwMC6CFGVzcGNvbm5lY3Rpb24ucXp6LmlvghYqLmVzcGNvbm5lY3Rp
b24ucXp6LmlvMBMGA1UdIAQMMAowCAYGZ4EMAQIBMDYGA1UdHwQvMC0wK6ApoCeG
JWh0dHA6Ly9jLnBraS5nb29nL3dlMS90Yjhyb1pOclo0MC5jcmwwggEFBgorBgEE
AdZ5AgQCBIH2BIHzAPEAdwCWl2S/VViXrfdDh2g3CEJ36fA61fak8zZuRqQ/D8qp
xgAAAZnZdfrNAAAEAwBIMEYCIQC3QlJ82RPtu2S1m+erTs8aP2rHLquAsWiuRRFD
vBtBOwIhAP1ta7Alir4BDAETPGB6DCX4uFKFXSw9hFW1cDkVfLUgAHYAyzj3FYl8
hKFEX1vB3fvJbvKaWc1HCmkFhbDLFMMUWOcAAAGZ2XX67AAABAMARzBFAiEAyav1
tyZ38L5j1BV3+w5E0X/iDbVdXyqO8fC+5M+C0fACIDwj0Yyr5UWPTb8TfaQ5nrez
NmtXGO8y2iGMHWJy2Y2OMAoGCCqGSM49BAMCA0gAMEUCIGiREBOvyZcqbaDm5WJy
21M8oNqCgpbwqWhhYM+yPgxrAiEAjcHGXnaY6FZRZHvqpIo6YZfcImZEgsyPv4XU
KyDAkt4=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICnzCCAiWgAwIBAgIQf/MZd5csIkp2FV0TttaF4zAKBggqhkjOPQQDAzBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjQwHhcNMjMxMjEzMDkwMDAwWhcNMjkwMjIwMTQw
MDAwWjA7MQswCQYDVQQGEwJVUzEeMBwGA1UEChMVR29vZ2xlIFRydXN0IFNlcnZp
Y2VzMQwwCgYDVQQDEwNXRTEwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAARvzTr+
Z1dHTCEDhUDCR127WEcPQMFcF4XGGTfn1XzthkubgdnXGhOlCgP4mMTG6J7/EFmP
LCaY9eYmJbsPAvpWo4H+MIH7MA4GA1UdDwEB/wQEAwIBhjAdBgNVHSUEFjAUBggr
BgEFBQcDAQYIKwYBBQUHAwIwEgYDVR0TAQH/BAgwBgEB/wIBADAdBgNVHQ4EFgQU
kHeSNWfE/6jMqeZ72YB5e8yT+TgwHwYDVR0jBBgwFoAUgEzW63T/STaj1dj8tT7F
avCUHYwwNAYIKwYBBQUHAQEEKDAmMCQGCCsGAQUFBzAChhhodHRwOi8vaS5wa2ku
Z29vZy9yNC5jcnQwKwYDVR0fBCQwIjAgoB6gHIYaaHR0cDovL2MucGtpLmdvb2cv
ci9yNC5jcnwwEwYDVR0gBAwwCjAIBgZngQwBAgEwCgYIKoZIzj0EAwMDaAAwZQIx
AOcCq1HW90OVznX+0RGU1cxAQXomvtgM8zItPZCuFQ8jSBJSjz5keROv9aYsAm5V
sQIwJonMaAFi54mrfhfoFNZEfuNMSQ6/bIBiNLiyoX46FohQvKeIoJ99cx7sUkFN
7uJW
-----END CERTIFICATE-----
)EOF";

// Runtime certificate (loaded from NVS or defaults to DEFAULT_ROOT_CA)
static char* g_root_ca = nullptr;

/**
 * Load Root CA certificate from NVS or use hardcoded default
 * Call this once during initialization
 */
static void loadRootCA() {
    // Allocate buffer for certificate (4KB max)
    const size_t CERT_BUFFER_SIZE = 4096;
    static char cert_buffer[CERT_BUFFER_SIZE];
    
    // Try to load from NVS first
    if (NVS::loadRootCA(cert_buffer, CERT_BUFFER_SIZE)) {
        Serial.println("🔐 Using Root CA from NVS (custom)");
        g_root_ca = cert_buffer;
    } else {
        Serial.println("🔐 Using default hardcoded Root CA");
        g_root_ca = (char*)DEFAULT_ROOT_CA;
    }
}

/**
 * Get current Root CA certificate
 */
static const char* getRootCA() {
    if (!g_root_ca) {
        loadRootCA();
    }
    return g_root_ca;
}

// =====================================================
// ====== CONSTANTS / LIMITS ===========================
// =====================================================

static const size_t   MAX_REPLY_WAV = 2048 * 1024; // cap ~2MB for TTS reply (~64s @ 16kHz mono)
static const size_t   CHUNK_SIZE    = 8 * 1024;   // upload chunk size
static const char    *BOUNDARY      = "----ESP32Boundary12345";

static uint8_t *g_replyBuf = nullptr;
static size_t   g_replyLen = 0;
static bool     g_ttsOnSdCard = false;  // True if last TTS was saved to SD (too large for PSRAM)
static String   g_ttsSdCardPath = "";   // Path to TTS file on SD card (if g_ttsOnSdCard)

// =====================================================
// ====== AUDIO BUFFER INTEGRATION ======================
// =====================================================
//
// Audio manager provides:
//  - audio_get_recording_wav(): Returns pointer to complete WAV in PSRAM
//    (includes 44B header + PCM data), or nullptr if not available
//  - audio_free_recording_buffer(): Frees the buffer after upload
//
// Declared in audio/audio_manager.h

// =====================================================
// ====== RAM DEBUG HELPER =============================
// =====================================================

static void logRam(const char *stage) {
  multi_heap_info_t internal;
  multi_heap_info_t external;
  heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL);
  heap_caps_get_info(&external, MALLOC_CAP_SPIRAM);

  Serial.printf(
    "[RAM][%s] internal_free=%u largest_block=%u | psram_free=%u largest_block=%u\n",
    stage,
    (unsigned)internal.total_free_bytes,
    (unsigned)internal.largest_free_block,
    (unsigned)external.total_free_bytes,
    (unsigned)external.largest_free_block
  );
}

// =====================================================
// ====== UTILS (HEX, TIME, URL PARSING) ===============
// =====================================================

static String toHex(const uint8_t *data, size_t len) {
  static const char *hexchars = "0123456789abcdef";
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    out += hexchars[(b >> 4) & 0x0F];
    out += hexchars[b & 0x0F];
  }
  return out;
}

// UTC "YYYY-MM-DDTHH:MM:SSZ"
static String iso8601Now() {
  time_t now = time(nullptr);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);

  char buf[32];
  snprintf(buf, sizeof(buf),
           "%04d-%02d-%02dT%02d:%02d:%02dZ",
           tm_utc.tm_year + 1900,
           tm_utc.tm_mon + 1,
           tm_utc.tm_mday,
           tm_utc.tm_hour,
           tm_utc.tm_min,
           tm_utc.tm_sec);
  return String(buf);
}

// HMAC-SHA256 (secret is hex string, server does bytes.fromhex)
static String hmac_sha256_hex_hexKey(const char *hexKey, const String &message) {
  size_t hexLen = strlen(hexKey);
  size_t keyLen = hexLen / 2;
  uint8_t *keyBytes = (uint8_t *)malloc(keyLen);
  if (!keyBytes) {
    Serial.println("!! malloc keyBytes failed");
    return "";
  }

  auto hexToNibble = [](char c)->uint8_t {
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t)(10 + (c - 'a'));
    if (c >= 'A' && c <= 'F') return (uint8_t)(10 + (c - 'A'));
    return 0;
  };

  for (size_t i = 0; i < keyLen; i++) {
    char hi = hexKey[i * 2];
    char lo = hexKey[i * 2 + 1];
    keyBytes[i] = (hexToNibble(hi) << 4) | hexToNibble(lo);
  }

  uint8_t hmacOut[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  if (mbedtls_md_setup(&ctx, info, 1) != 0) {
    Serial.println("!! mbedtls_md_setup failed");
    mbedtls_md_free(&ctx);
    free(keyBytes);
    return "";
  }

  mbedtls_md_hmac_starts(&ctx, keyBytes, keyLen);
  mbedtls_md_hmac_update(&ctx,
                         (const unsigned char *)message.c_str(),
                         message.length());
  mbedtls_md_hmac_finish(&ctx, hmacOut);

  mbedtls_md_free(&ctx);
  free(keyBytes);

  return toHex(hmacOut, sizeof(hmacOut));
}

// Extract "audio_url":"..." or detect "audio_url":null
static bool extractAudioUrl(const String &json, String &outUrl) {
  int keyPos = json.indexOf("\"audio_url\"");
  if (keyPos < 0) return false;

  int colonPos = json.indexOf(":", keyPos);
  if (colonPos < 0) return false;

  // Check if value is null (not a string)
  int checkStart = colonPos + 1;
  while (checkStart < json.length() && (json[checkStart] == ' ' || json[checkStart] == '\t')) {
    checkStart++;  // Skip whitespace
  }
  
  // If next non-whitespace char is 'n', check for "null"
  if (checkStart < json.length() && json[checkStart] == 'n') {
    if (json.substring(checkStart, checkStart + 4) == "null") {
      return false;  // audio_url is null
    }
  }

  // Value should be a string - find quotes
  int firstQ = json.indexOf("\"", colonPos);
  if (firstQ < 0) return false;

  int secondQ = json.indexOf("\"", firstQ + 1);
  if (secondQ < 0) return false;

  outUrl = json.substring(firstQ + 1, secondQ);
  if (outUrl.length() == 0) return false;
  
  return true;
}

// Split "https://host/path..." into hostOut="host", pathOut="/path..."
static bool splitUrlHostPath(const String &url, String &hostOut, String &pathOut) {
  String work = url;
  const String prefix = "https://";
  if (work.startsWith(prefix)) {
    work.remove(0, prefix.length());
  }
  int slashPos = work.indexOf('/');
  if (slashPos < 0) return false;
  hostOut = work.substring(0, slashPos);
  pathOut = work.substring(slashPos);
  return true;
}

// Split global serverUrl (e.g. "https://your-server.example.com/transcribe")
static bool splitServerUrl(String &hostOut, String &pathOut) {
  return splitUrlHostPath(serverUrl, hostOut, pathOut);
}

// =====================================================
// ====== TLS DOWNLOAD (TTS WAV) =======================
// =====================================================
//
// downloadAudio(url, savePath):
// - Downloads TTS audio from URL to PSRAM buffer (g_replyBuf)
// - Saves to SD card at savePath for audio playback
// - Returns true if download succeeded (even if SD save failed)
//

bool downloadAudio(const char* url, const char* savePath) {
  String sUrl(url ? url : "");
  Serial.println("🔽 Starting downloadAudio...");
  Serial.printf("🔽 URL: %s\n", sUrl.c_str());
  // logRam("beforeDownload");

  // Parse target host/path
  String host, path;
  if (!splitUrlHostPath(sUrl, host, path)) {
    Serial.println("❌ splitUrlHostPath failed");
    return false;
  }

  // Reset SD card flags
  g_ttsOnSdCard = false;
  g_ttsSdCardPath = "";

  // Allocate PSRAM buffer once
  if (!g_replyBuf) {
    g_replyBuf = (uint8_t *)heap_caps_malloc(MAX_REPLY_WAV, MALLOC_CAP_SPIRAM);
    if (!g_replyBuf) {
      Serial.println("❌ PSRAM alloc failed for reply buffer");
      return false;
    }
  }
  g_replyLen = 0;

  WiFiClientSecure client;
  client.setCACert(getRootCA());
  client.setTimeout(5000);

  Serial.printf("🔐 TLS GET connect to %s ...\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("❌ TLS connect() failed (download)");
    return false;
  }

  // Send GET
  client.printf(
    "GET %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Connection: close\r\n"
    "\r\n",
    path.c_str(),
    host.c_str()
  );

  // Read headers
  String header;
  bool headerDone = false;
  unsigned long lastRead = millis();
  size_t contentLen = 0;  // Content-Length from server (needed for progress tracking)
  
  // Track last LVGL refresh for 24fps updates
  unsigned long last_lvgl_refresh = millis();

  while (!headerDone && (millis() - lastRead < 5000)) {
    while (client.available()) {
      char c = client.read();
      if (c < 0) break;
      header += c;
      lastRead = millis();

      // Refresh LVGL at 24fps during header read
      unsigned long now = millis();
      if (now - last_lvgl_refresh >= 42) {
        lv_refr_now(NULL);
        last_lvgl_refresh = now;
      }
      
      int idx = header.indexOf("\r\n\r\n");
      if (idx >= 0) {
        String bodyStart = header.substring(idx + 4);
        header = header.substring(0, idx);

        // status code
        int statusCode = -1;
        {
          int sp1 = header.indexOf(' ');
          int sp2 = header.indexOf(' ', sp1 + 1);
          if (sp1 >= 0 && sp2 > sp1) {
            String codeStr = header.substring(sp1 + 1, sp2);
            statusCode = codeStr.toInt();
          }
        }
        Serial.printf("🔽 Download HTTP status: %d\n", statusCode);

        // Content-Length (optional)
        {
          int clPos = header.indexOf("Content-Length:");
          if (clPos >= 0) {
            int lineEnd = header.indexOf("\r\n", clPos);
            String line;
            if (lineEnd > clPos) {
              line = header.substring(clPos, lineEnd);
            } else {
              line = header.substring(clPos);
            }
            int colon = line.indexOf(':');
            if (colon >= 0) {
              String num = line.substring(colon + 1);
              num.trim();
              contentLen = (size_t) num.toInt();
            }
          }
        }
        Serial.printf("🔽 Declared Content-Length: %u\n", (unsigned)contentLen);
        
        // Check if file is too large for PSRAM - save to SD card instead
        if (contentLen > MAX_REPLY_WAV) {
          Serial.printf("⚠️  File too large for PSRAM (%u KB > %u KB)\n", 
                       (unsigned)(contentLen / 1024), (unsigned)(MAX_REPLY_WAV / 1024));
          Serial.println("💾 Automatically saving to SD card...");
          
          // Generate unique filename for SD card
          g_ttsSdCardPath = "/sdcard/tts/response_" + String(millis()) + ".wav";
          
          // Ensure /tts directory exists
          if (!SD.exists("/sdcard/tts")) {
            Serial.println("📁 Creating /sdcard/tts directory...");
            if (!SD.mkdir("/sdcard/tts")) {
              Serial.println("❌ Failed to create /sdcard/tts directory");
              client.stop();
              return false;
            }
          }
          
          // Open SD file for writing
          File sdFile = SD.open(g_ttsSdCardPath.c_str(), FILE_WRITE);
          if (!sdFile) {
            Serial.printf("❌ Failed to open %s for writing\n", g_ttsSdCardPath.c_str());
            client.stop();
            return false;
          }
          
          // Write initial body data to SD
          if (bodyStart.length() > 0) {
            sdFile.write((const uint8_t*)bodyStart.c_str(), bodyStart.length());
            g_replyLen = bodyStart.length();
          }
          
          Serial.printf("💾 Streaming to SD: %s\n", g_ttsSdCardPath.c_str());
          
          // Stream rest of body directly to SD card
          uint8_t chunk[1024];
          size_t last_progress_update = g_replyLen;
          
          headerDone = true;
          
          while (client.connected() || client.available()) {
            // Check for cancel
            if (g_upload_cancel) {
              Serial.println("⚠️ Download cancelled by flag");
              sdFile.close();
              client.stop();
              SD.remove(g_ttsSdCardPath.c_str());  // Delete incomplete file
              return false;
            }
            
            size_t available = client.available();
            if (available > 0) {
              size_t toRead = available > sizeof(chunk) ? sizeof(chunk) : available;
              size_t bytesRead = client.read(chunk, toRead);
              if (bytesRead > 0) {
                sdFile.write(chunk, bytesRead);
                g_replyLen += bytesRead;
                
                // Update progress every 16KB
                if ((g_replyLen - last_progress_update) >= (16 * 1024)) {
                  int kb_data[2] = {(int)(g_replyLen / 1024), (int)(contentLen / 1024)};
                  Serial.printf("📊 Download: Progress %d kb / %d kb (%.1f%%)\n", 
                               kb_data[0], kb_data[1], 
                               100.0 * ((double)g_replyLen / (double)contentLen));
                  display_manager_update_data(kb_data);
                  last_progress_update = g_replyLen;
                }
                
                // Refresh LVGL at 24fps
                unsigned long now = millis();
                if (now - last_lvgl_refresh >= 42) {
                  lv_refr_now(NULL);
                  last_lvgl_refresh = now;
                }
              }
            }
          }
          
          sdFile.close();
          client.stop();
          
          // Final progress
          int kb_data[2] = {(int)(g_replyLen / 1024), (int)(contentLen / 1024)};
          Serial.printf("📊 Download: Complete %d kb / %d kb (100%%)\n", kb_data[0], kb_data[1]);
          display_manager_update_data(kb_data);
          
          Serial.printf("✅ Saved large TTS to SD: %s (%u bytes)\n", g_ttsSdCardPath.c_str(), (unsigned)g_replyLen);
          g_ttsOnSdCard = true;
          return true;
        }

        // Normal PSRAM path - copy bodyStart into PSRAM
        int bodyLen0 = bodyStart.length();
        if ((size_t)bodyLen0 > MAX_REPLY_WAV) {
          bodyLen0 = (int)MAX_REPLY_WAV;
        }
        memcpy(g_replyBuf, bodyStart.c_str(), bodyLen0);
        g_replyLen = bodyLen0;

        headerDone = true;
        break;
      }
    }
  }

  if (!headerDone) {
    Serial.println("❌ header not received / timeout");
    client.stop();
    return false;
  }

  // Initial progress update (show header bytes received)
  if (contentLen > 0) {
    int kb_data[2] = {(int)(g_replyLen / 1024), (int)(contentLen / 1024)};
    Serial.printf("\r📊 Download: %d / %d kb (0%%)  ", kb_data[0], kb_data[1]);
    Serial.flush();
    display_manager_update_data(kb_data);
  }

  // Slurp rest of body with progress updates
  size_t last_progress_update = g_replyLen;
  while (client.connected() || client.available()) {
    while (client.available()) {
      int c = client.read();
      if (c < 0) break;
      if (g_replyLen < MAX_REPLY_WAV) {
        g_replyBuf[g_replyLen++] = (uint8_t)c;
      }
    }
    
    // Update progress every 16KB
    if (contentLen > 0 && (g_replyLen - last_progress_update) >= (16 * 1024)) {
      int kb_data[2] = {(int)(g_replyLen / 1024), (int)(contentLen / 1024)};
      Serial.printf("\r📊 Download: %d / %d kb (%.0f%%)  ", 
                    kb_data[0], kb_data[1], 
                    100.0 * ((double)g_replyLen / (double)contentLen));
      Serial.flush();
      display_manager_update_data(kb_data);
      last_progress_update = g_replyLen;
      
      // Reset watchdog during download
      esp_task_wdt_reset();
    }
    
    // Refresh LVGL at 24fps during download
    unsigned long now = millis();
    if (now - last_lvgl_refresh >= 42) {
      lv_refr_now(NULL);
      last_lvgl_refresh = now;
    }
  }
  
  // Final progress update
  if (contentLen > 0) {
    int kb_data[2] = {(int)(g_replyLen / 1024), (int)(contentLen / 1024)};
    Serial.printf("\r📊 Download: %d / %d kb (100%%) ✓\n", kb_data[0], kb_data[1]);
    display_manager_update_data(kb_data);
  }

  client.stop();

  // Save to SD card if savePath provided
  if (savePath && g_replyLen > 0) {
    // Verify SD card is mounted
    if (SD.cardType() == CARD_NONE) {
      Serial.println("❌ SD card not mounted - cannot save TTS file");
      return true;  // Download succeeded, just couldn't save
    }
    
    // Ensure /tts directory exists
    if (!SD.exists("/tts")) {
      Serial.println("📁 Creating /tts directory...");
      if (!SD.mkdir("/tts")) {
        Serial.println("❌ Failed to create /tts directory");
        return true;  // Download succeeded, just couldn't save
      }
    }
    
    File f = SD.open(savePath, "w");
    if (f) {
      size_t written = f.write(g_replyBuf, g_replyLen);
      f.close();
      if (written == g_replyLen) {
        Serial.printf("💾 Saved TTS to: %s (%u bytes)\n", savePath, (unsigned)written);
      } else {
        Serial.printf("⚠️  Partial write to %s (%u/%u bytes)\n", savePath, (unsigned)written, (unsigned)g_replyLen);
      }
    } else {
      Serial.printf("❌ Failed to open %s for writing\n", savePath);
    }
  }

  // logRam("afterDownload");
  return true;
}

// =====================================================
// ====== CORE MULTIPART UPLOAD ========================
// =====================================================
//
// doMultipartUpload():
// - opens TLS
// - POST multipart/form-data with entire WAV
// - reads JSON
// - extracts audio_url
// - calls downloadAudio() to pull TTS
//

static bool doMultipartUpload(
    const uint8_t *wavData,
    size_t wavSize,
    ServerResponse &outResp
) {
  outResp = {};

  if (!wavData || wavSize == 0) {
    Serial.println("❌ No wavData");
    outResp.success = false;
    outResp.httpCode = -1;
    outResp.errorMessage = "No wavData";
    return false;
  }

  // Parse serverUrl into host+path
  String host, path;
  if (!splitServerUrl(host, path)) {
    Serial.println("❌ Bad serverUrl format");
    outResp.success = false;
    outResp.httpCode = -1;
    outResp.errorMessage = "Bad serverUrl";
    return false;
  }

  // Device auth
  // Get device secret from settings_manager (stored in NVS)
  String deviceSecret = settings_manager_get_device_secret();

  if (deviceId.length() == 0) {
    deviceId = getMacAddress();
  }

  String deviceTime     = iso8601Now();
  String signingMessage = deviceId + "|" + deviceTime;
  String signatureHex   = hmac_sha256_hex_hexKey(deviceSecret.c_str(), signingMessage);

Serial.printf("🔐 Device: %s\n", deviceId.c_str());

// Get current conversation ID (if any) from state machine
extern const char* state_machine_get_conversation_id();
const char* convId = state_machine_get_conversation_id();
bool hasContinuation = (convId && strlen(convId) > 0);

// Build multipart boundaries
String preBody;
preBody.reserve(512);  // Increased for conversation_id field

// Add conversation_id field if we're continuing a conversation
if (hasContinuation) {
Serial.printf("🔄 Continuing conversation: %s\n", convId);
preBody += "--";
preBody += BOUNDARY;
preBody += "\r\n";
preBody += "Content-Disposition: form-data; name=\"conversation_id\"\r\n";
preBody += "\r\n";
preBody += convId;
preBody += "\r\n";
}

  // Audio file part
  preBody += "--";
  preBody += BOUNDARY;
  preBody += "\r\n";
  preBody += "Content-Disposition: form-data; name=\"audio\"; filename=\"rec.wav\"\r\n";
  preBody += "Content-Type: audio/wav\r\n";
  preBody += "\r\n";

  String postBody = "\r\n--";
  postBody += BOUNDARY;
  postBody += "--\r\n";

  size_t contentLength = preBody.length() + wavSize + postBody.length();

  // TLS
  WiFiClientSecure client;
  client.setCACert(getRootCA());
  client.setTimeout(5000);

  // logRam("preConnect");
  Serial.printf("🔐 TLS connect to %s:443 ...\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("❌ TLS connect() failed");
    outResp.success = false;
    outResp.httpCode = -1;
    outResp.errorMessage = "TLS connect failed";
    return false;
  }
  // logRam("postConnect");

  // HTTP headers
  client.printf(
    "POST %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Content-Type: multipart/form-data; boundary=%s\r\n"
    "Content-Length: %u\r\n"
    "Connection: close\r\n"
    "X-Device-ID: %s\r\n"
    "X-Device-Time: %s\r\n"
    "X-Device-Signature: %s\r\n"
    "\r\n",
    path.c_str(),
    host.c_str(),
    BOUNDARY,
    (unsigned)contentLength,
    deviceId.c_str(),
    deviceTime.c_str(),
    signatureHex.c_str()
  );

  // multipart prefix
  client.print(preBody);

  // stream WAV in CHUNK_SIZE slices
  size_t sent = 0;
  size_t offset = 0;
  
  // Track last LVGL refresh for 24fps updates (used in upload and response loops)
  unsigned long last_lvgl_refresh = millis();
  
  // Initial progress update (0 kb)
  int kb_data[2] = {0, (int)(wavSize / 1024)};
  Serial.printf("\r📊 Upload: 0 / %d kb (0%%)  ", kb_data[1]);
  Serial.flush();
  display_manager_update_data(kb_data);
  
  while (offset < wavSize) {
    if (g_upload_cancel) {
      Serial.println("⚠️ Upload cancelled by flag");
      break;
    }

    size_t n = wavSize - offset;
    if (n > CHUNK_SIZE) n = CHUNK_SIZE;

    size_t written = client.write(wavData + offset, n);
    offset += written;
    sent   += written;
    
    // Refresh LVGL at 24fps (every 42ms) for smooth spinner animation
    unsigned long now = millis();
    if (now - last_lvgl_refresh >= 42) {
      lv_refr_now(NULL);
      last_lvgl_refresh = now;
    }

    // progress log & update every 16KB (more frequent updates)
    if ((sent % (16 * 1024)) < CHUNK_SIZE) {
      // Update display with progress in KB
      int kb_data[2] = {
        (int)(sent / 1024),
        (int)(wavSize / 1024)
      };
      // Single line progress with carriage return
      Serial.printf("\r📊 Upload: %d / %d kb (%.0f%%)  ",
                    kb_data[0], kb_data[1],
                    100.0 * ((double)sent / (double)wavSize));
      Serial.flush();
      display_manager_update_data(kb_data);
      
      // Reset watchdog during upload
      esp_task_wdt_reset();
    }
  }
  
  // Final progress update (complete)
  kb_data[0] = (int)(sent / 1024);
  kb_data[1] = (int)(wavSize / 1024);
  display_manager_update_data(kb_data);
  Serial.printf("\r📊 Upload: %d / %d kb (100%%) ✓\n",
                kb_data[0], kb_data[1]);

  // multipart tail
  client.print(postBody);
  sent += postBody.length();

  // read full response with timeout and cancel support
  client.setTimeout(10000);  // 10s socket timeout
  String rawResp;
  unsigned long responseStart = millis();
  const unsigned long RESPONSE_TIMEOUT = 90000;  // 90 second timeout (matches server)
  
  while (client.connected() || client.available()) {
    // Check for timeout
    if (millis() - responseStart > RESPONSE_TIMEOUT) {
      Serial.println("⚠️  Response timeout - closing connection");
      break;
    }
    
    // Check for cancel
    if (g_upload_cancel) {
      Serial.println("⚠️  Upload cancelled during response read");
      break;
    }
    
    while (client.available()) {
      char c = client.read();
      if (c < 0) break;
      rawResp += c;
    }
    
    // Refresh LVGL at 24fps during response wait
    unsigned long now = millis();
    if (now - last_lvgl_refresh >= 42) {
      lv_refr_now(NULL);
      last_lvgl_refresh = now;
    }
    
    delay(1);  // Yield to other tasks
  }
  client.stop();
  
  // Check if cancelled or timed out
  if (g_upload_cancel) {
    outResp.success = false;
    outResp.httpCode = -1;
    outResp.errorMessage = "Upload cancelled";
    return false;
  }
  
  if (rawResp.length() == 0) {
    Serial.println("❌ No response received from server");
    outResp.success = false;
    outResp.httpCode = -1;
    outResp.errorMessage = "No response (timeout or connection closed)";
    return false;
  }

  // separate headers/body
  int hdrEnd = rawResp.indexOf("\r\n\r\n");
  String headerPart;
  String bodyPart;
  if (hdrEnd >= 0) {
    headerPart = rawResp.substring(0, hdrEnd);
    bodyPart   = rawResp.substring(hdrEnd + 4);
  } else {
    headerPart = rawResp;
    bodyPart   = "";
  }

  // status code
  int statusCode = -1;
  {
    int sp1 = headerPart.indexOf(' ');
    int sp2 = headerPart.indexOf(' ', sp1 + 1);
    if (sp1 >= 0 && sp2 > sp1) {
      String codeStr = headerPart.substring(sp1 + 1, sp2);
      statusCode = codeStr.toInt();
    }
  }

  Serial.printf("🌍 Server HTTP status: %d\n", statusCode);
  Serial.println("🌍 Response body:");
  Serial.println(bodyPart);
  // logRam("afterUpload");

  // fill outResp
  outResp.httpCode  = statusCode;
  outResp.success   = (statusCode == 200);
  outResp.errorMessage = outResp.success ? "" : "HTTP error";

  // Extract response type (say/ask/confirm)
  outResp.type = RESPONSE_UNKNOWN;
  {
    int k = bodyPart.indexOf("\"type\"");
    if (k >= 0) {
      int cpos = bodyPart.indexOf(":", k);
      int q1   = bodyPart.indexOf("\"", cpos);
      int q2   = bodyPart.indexOf("\"", q1 + 1);
      if (cpos >= 0 && q1 >= 0 && q2 > q1) {
        String typeStr = bodyPart.substring(q1 + 1, q2);
        if (typeStr == "say") {
          outResp.type = RESPONSE_SAY;
        } else if (typeStr == "ask") {
          outResp.type = RESPONSE_ASK;
        } else if (typeStr == "confirm") {
          outResp.type = RESPONSE_CONFIRM;
        }
        Serial.printf("🔍 Response type: %s\n", typeStr.c_str());
      }
    }
  }

  // Extract conversation_id (for multi-turn)
  {
    int k = bodyPart.indexOf("\"conversation_id\"");
    if (k >= 0) {
      int cpos = bodyPart.indexOf(":", k);
      int q1   = bodyPart.indexOf("\"", cpos);
      int q2   = bodyPart.indexOf("\"", q1 + 1);
      if (cpos >= 0 && q1 >= 0 && q2 > q1) {
        outResp.conversationId = bodyPart.substring(q1 + 1, q2);
        Serial.printf("🔍 Conversation ID: %s\n", outResp.conversationId.c_str());
      }
    }
  }

  // Extract transcription
  {
    int k = bodyPart.indexOf("\"transcription\"");
    if (k >= 0) {
      int cpos = bodyPart.indexOf(":", k);
      int q1   = bodyPart.indexOf("\"", cpos);
      int q2   = bodyPart.indexOf("\"", q1 + 1);
      if (cpos >= 0 && q1 >= 0 && q2 > q1) {
        outResp.transcription = bodyPart.substring(q1 + 1, q2);
        Serial.printf("🎯 Transcription: %s\n", outResp.transcription.c_str());
      }
    }
  }

  // Extract message (the response text) - properly handle escaped quotes
  {
    int k = bodyPart.indexOf("\"message\"");
    if (k >= 0) {
      int cpos = bodyPart.indexOf(":", k);
      int q1   = bodyPart.indexOf("\"", cpos);
      if (cpos >= 0 && q1 >= 0) {
        // Scan for closing quote, skipping escaped quotes (\")
        int q2 = -1;
        for (int i = q1 + 1; i < bodyPart.length(); i++) {
          if (bodyPart[i] == '\\') {
            i++;  // Skip next character (escaped char)
            continue;
          }
          if (bodyPart[i] == '"') {
            q2 = i;
            break;
          }
        }
        
        if (q2 > q1) {
          String rawJson = bodyPart.substring(q1 + 1, q2);
          // Unescape: replace \" with ", \\ with \, etc.
          outResp.message = "";
          for (int i = 0; i < rawJson.length(); i++) {
            if (rawJson[i] == '\\' && i + 1 < rawJson.length()) {
              char next = rawJson[i + 1];
              if (next == '"' || next == '\\' || next == '/') {
                outResp.message += next;
                i++;  // Skip the escaped char
              } else if (next == 'n') {
                outResp.message += '\n';
                i++;
              } else if (next == 't') {
                outResp.message += '\t';
                i++;
              } else {
                outResp.message += rawJson[i];  // Keep backslash for unknown escapes
              }
            } else {
              outResp.message += rawJson[i];
            }
          }
          Serial.printf("🔍 Message: %s\n", outResp.message.c_str());
        }
      }
    }
  }

  // Extract command (for confirm type) - properly handle escaped quotes
  {
    int k = bodyPart.indexOf("\"command\"");
    if (k >= 0) {
      int cpos = bodyPart.indexOf(":", k);
      int q1   = bodyPart.indexOf("\"", cpos);
      if (cpos >= 0 && q1 >= 0) {
        // Scan for closing quote, skipping escaped quotes
        int q2 = -1;
        for (int i = q1 + 1; i < bodyPart.length(); i++) {
          if (bodyPart[i] == '\\') {
            i++;  // Skip next character
            continue;
          }
          if (bodyPart[i] == '"') {
            q2 = i;
            break;
          }
        }
        
        if (q2 > q1) {
          String rawJson = bodyPart.substring(q1 + 1, q2);
          // Unescape
          outResp.command = "";
          for (int i = 0; i < rawJson.length(); i++) {
            if (rawJson[i] == '\\' && i + 1 < rawJson.length()) {
              char next = rawJson[i + 1];
              if (next == '"' || next == '\\' || next == '/') {
                outResp.command += next;
                i++;
              } else {
                outResp.command += rawJson[i];
              }
            } else {
              outResp.command += rawJson[i];
            }
          }
          Serial.printf("🔍 Command: %s\n", outResp.command.c_str());
        }
      }
    }
  }

  // Extract TTS URL (download will happen later in main.cpp)
  {
    String ttsUrl;
    if (extractAudioUrl(bodyPart, ttsUrl)) {
      outResp.ttsUrl = ttsUrl;
      Serial.printf("🔎 Found audio_url: %s\n", ttsUrl.c_str());
      Serial.println("   ℹ️  TTS URL stored - download will happen in DOWNLOADING state");
    } else {
      Serial.println("ℹ️  No audio_url in response (null or missing) - no TTS to download");
    }
  }

  return outResp.success;
}

// =====================================================
// ====== PUBLIC API (MATCHES https_client.h) ==========
// =====================================================

// Init once after WiFi + NTP is up.
void initHttpsClient() {
  deviceMac = getMacAddress();
  deviceId  = generateDeviceId();

  Serial.printf("🔐 initHttpsClient(): deviceMac=%s deviceId=%s\n",
                deviceMac.c_str(),
                deviceId.c_str());

  // If you have NVS-backed serverUrl, you can call loadServerConfig() here.
}

// load server config (e.g. from NVS). Stubbed -> true.
bool loadServerConfig() {
  return true;
}

// save server config (serverUrl). Stub just updates global.
bool saveServerConfig(const char* url) {
  if (!url) return false;
  serverUrl = String(url);
  Serial.printf("💾 Saved serverUrl = %s\n", serverUrl.c_str());
  return true;
}

String getServerUrl() {
  return serverUrl;
}

bool setServerUrl(const char* url) {
  return saveServerConfig(url);
}

// MAIN ENTRY: upload most recent recording.
// 1. Try direct PSRAM buffer from recorder (no SD).
// 2. If not available, fall back to opening the file from SD.
ServerResponse transcribeAudio(const char* audioFilePath) {
  ServerResponse respOut = {};

  // Fast path: use in-PSRAM WAV from recorder
  size_t psramLen = 0;
  uint8_t *psramBuf = audio_get_recording_wav(&psramLen);
  
  if (psramBuf != nullptr && psramLen > 0) {
    Serial.printf(
      "📤 transcribeAudio(): Using direct PSRAM buffer (%u bytes), skipping SD reload\n",
      (unsigned)psramLen
    );

    bool ok = doMultipartUpload(psramBuf, psramLen, respOut);
    if (!ok) {
      Serial.println("❌ doMultipartUpload() failed (PSRAM path)");
    }
    // We DO NOT call audio_free_recording_buffer() here.
    // Your queue logic decides when to free.
    return respOut;
  }

  // Slow path: pull file from SD into PSRAM (for retries / queue)
  if (!audioFilePath) {
    respOut.success = false;
    respOut.httpCode = -1;
    respOut.errorMessage = "null path";
    Serial.println("❌ transcribeAudio(): null path and no PSRAM buffer");
    return respOut;
  }

  Serial.printf(
    "📤 transcribeAudio(): No PSRAM buffer, loading from SD: %s\n",
    audioFilePath
  );

  // Use POSIX file API (keep full /sdcard path)
  FILE* f = openFileForRead(audioFilePath);
  if (!f) {
    Serial.printf("❌ Failed to open %s\n", audioFilePath);
    respOut.success = false;
    respOut.httpCode = -1;
    respOut.errorMessage = "open fail";
    return respOut;
  }

  // Get file size
  fseek(f, 0, SEEK_END);
  size_t sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  uint8_t *buf = (uint8_t*) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
  if (!buf) {
    Serial.println("❌ PSRAM alloc failed for WAV file");
    fclose(f);
    respOut.success = false;
    respOut.httpCode = -1;
    respOut.errorMessage = "psram alloc fail";
    return respOut;
  }

  size_t rd = fread(buf, 1, sz, f);
  fclose(f);
  if (rd != sz) {
    Serial.println("❌ short read");
    heap_caps_free(buf);
    respOut.success = false;
    respOut.httpCode = -1;
    respOut.errorMessage = "short read";
    return respOut;
  }

  Serial.printf(
    "📤 transcribeAudio(): loaded %u bytes from SD into PSRAM\n",
    (unsigned)sz
  );

  bool ok = doMultipartUpload(buf, sz, respOut);
  heap_caps_free(buf);

  if (!ok) {
    Serial.println("❌ doMultipartUpload() failed (SD path)");
  }
  return respOut;
}

// Direct "here's a buffer in RAM already" path. Useful for queue retries
// that already staged the WAV in memory.
ServerResponse transcribeAudioFromBuffer(const uint8_t* wavData,
                                         size_t size,
                                         const char* logFilename)
{
  (void)logFilename;
  ServerResponse respOut = {};

  Serial.printf(
    "📤 transcribeAudioFromBuffer(): size=%u (%s)\n",
    (unsigned)size,
    logFilename ? logFilename : "no-name"
  );

  doMultipartUpload(wavData, size, respOut);
  return respOut;
}

// Confirmation / cancel / heartbeat / etc. are currently stubs so
// the rest of your firmware links and runs.
// You can wire them to actual endpoints later.

// Execute confirmed command (POST /execute)
ServerResponse executeCommand(const char* conversationId, const char* command) {
  ServerResponse r = {};
  
  if (!conversationId || !command) {
    r.success = false;
    r.httpCode = -1;
    r.errorMessage = "Missing conversationId or command";
    Serial.println("❌ executeCommand: null params");
    return r;
  }

  // Parse serverUrl to extract just the host (ignore path - /execute is absolute)
  String host, path;
  if (!splitServerUrl(host, path)) {
    Serial.println("❌ Bad serverUrl format");
    r.success = false;
    r.httpCode = -1;
    r.errorMessage = "Bad serverUrl";
    return r;
  }

  // Device auth
  String deviceSecret = settings_manager_get_device_secret();
  if (deviceId.length() == 0) {
    deviceId = getMacAddress();
  }

  String deviceTime = iso8601Now();
  String signingMessage = deviceId + "|" + deviceTime;
  String signatureHex = hmac_sha256_hex_hexKey(deviceSecret.c_str(), signingMessage);

  // Build JSON body
  String jsonBody = "{\"command\":\"" + String(command) + "\",\"conversation_id\":\"" + String(conversationId) + "\"}";
  
  // TLS
  WiFiClientSecure client;
  client.setCACert(getRootCA());
  client.setTimeout(10000);

  Serial.printf("🔐 TLS connect to %s:443 for /execute...\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("❌ TLS connect() failed");
    r.success = false;
    r.httpCode = -1;
    r.errorMessage = "TLS connect failed";
    return r;
  }

  // POST /execute (absolute path, not relative to transcribe)
  String executePath = "/execute";

  client.printf(
    "POST %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %u\r\n"
    "Connection: close\r\n"
    "X-Device-ID: %s\r\n"
    "X-Device-Time: %s\r\n"
    "X-Device-Signature: %s\r\n"
    "\r\n",
    executePath.c_str(),
    host.c_str(),
    jsonBody.length(),
    deviceId.c_str(),
    deviceTime.c_str(),
    signatureHex.c_str()
  );

  client.print(jsonBody);

  // Read response
  String rawResp;
  unsigned long start = millis();
  while (client.connected() || client.available()) {
    if (millis() - start > 10000) break;  // 10s timeout
    while (client.available()) {
      char c = client.read();
      if (c < 0) break;
      rawResp += c;
    }
    delay(1);
  }
  client.stop();

  Serial.println("🌍 /execute response:");
  Serial.println(rawResp);

  // Parse status code
  int statusCode = -1;
  int hdrEnd = rawResp.indexOf("\r\n\r\n");
  String headerPart = hdrEnd >= 0 ? rawResp.substring(0, hdrEnd) : rawResp;
  String bodyPart = hdrEnd >= 0 ? rawResp.substring(hdrEnd + 4) : "";

  int sp1 = headerPart.indexOf(' ');
  int sp2 = headerPart.indexOf(' ', sp1 + 1);
  if (sp1 >= 0 && sp2 > sp1) {
    String codeStr = headerPart.substring(sp1 + 1, sp2);
    statusCode = codeStr.toInt();
  }

  r.httpCode = statusCode;
  r.success = (statusCode == 200);
  r.conversationId = conversationId;
  r.errorMessage = r.success ? "" : "Execute failed";
  r.type = RESPONSE_SAY;  // Execute always returns SAY response

  if (r.success && bodyPart.length() > 0) {
    Serial.println("🔍 Parsing /execute response JSON...");
    
    // Extract message - properly handle escaped quotes
    {
      int k = bodyPart.indexOf("\"message\"");
      if (k >= 0) {
        int cpos = bodyPart.indexOf(":", k);
        int q1   = bodyPart.indexOf("\"", cpos);
        if (cpos >= 0 && q1 >= 0) {
          // Scan for closing quote, skipping escaped quotes
          int q2 = -1;
          for (int i = q1 + 1; i < bodyPart.length(); i++) {
            if (bodyPart[i] == '\\') {
              i++;  // Skip next character
              continue;
            }
            if (bodyPart[i] == '"') {
              q2 = i;
              break;
            }
          }
          
          if (q2 > q1) {
            String rawJson = bodyPart.substring(q1 + 1, q2);
            // Unescape: replace \" with ", \\ with \, etc.
            r.message = "";
            for (int i = 0; i < rawJson.length(); i++) {
              if (rawJson[i] == '\\' && i + 1 < rawJson.length()) {
                char next = rawJson[i + 1];
                if (next == '"' || next == '\\' || next == '/') {
                  r.message += next;
                  i++;
                } else if (next == 'n') {
                  r.message += '\n';
                  i++;
                } else if (next == 't') {
                  r.message += '\t';
                  i++;
                } else {
                  r.message += rawJson[i];
                }
              } else {
                r.message += rawJson[i];
              }
            }
            Serial.printf("🔍 Message: %s\n", r.message.c_str());
          }
        }
      }
    }
    
    // Extract TTS URL
    {
      String ttsUrl;
      if (extractAudioUrl(bodyPart, ttsUrl)) {
        r.ttsUrl = ttsUrl;
        Serial.printf("🔎 Found audio_url: %s\n", ttsUrl.c_str());
      }
    }
  }

  Serial.printf("✅ Execute command '%s' - HTTP %d\n", command, statusCode);
  return r;
}

bool sendHeartbeat(const char* state, int battery, int wifiRssi) {
  Serial.printf("💓 Heartbeat stub: state=%s bat=%d wifi=%d\n",
                state ? state : "(null)", battery, wifiRssi);
  return true;
}

bool acknowledgePlayback(const char* conversationId, const char* audioFile) {
  Serial.printf("🔊 acknowledgePlayback stub: convId=%s file=%s\n",
                conversationId ? conversationId : "(null)",
                audioFile ? audioFile : "(null)");
  return true;
}

bool checkServerStatus() {
  Serial.println("🌐 checkServerStatus stub -> true");
  return true;
}

const uint8_t* getTtsBuffer(size_t* outSize) {
  // If TTS was saved to SD card (too large for PSRAM), return nullptr
  // Caller should check isTtsOnSdCard() and use getTtsSdCardPath() instead
  if (g_ttsOnSdCard) {
    if (outSize) {
      *outSize = 0;
    }
    return nullptr;
  }
  
  if (outSize) {
    *outSize = g_replyLen;
  }
  return g_replyBuf;
}

bool isTtsOnSdCard() {
  return g_ttsOnSdCard;
}

const char* getTtsSdCardPath() {
  return g_ttsSdCardPath.c_str();
}

size_t getTtsSize() {
  return g_replyLen;
}

void freeTtsBuffers() {
  // Free PSRAM buffer if allocated
  if (g_replyBuf) {
    free(g_replyBuf);
    g_replyBuf = nullptr;
    Serial.printf("🧹 Freed TTS PSRAM buffer (%d KB)\n", g_replyLen / 1024);
  }
  
  // Delete SD file if TTS was saved there
  if (g_ttsOnSdCard && g_ttsSdCardPath.length() > 0) {
    if (SD.remove(g_ttsSdCardPath.c_str())) {
      Serial.printf("🧹 Deleted TTS SD file: %s\n", g_ttsSdCardPath.c_str());
    }
  }
  
  // Reset state
  g_replyLen = 0;
  g_ttsOnSdCard = false;
  g_ttsSdCardPath = "";
}

// =====================================================
// ====== REQUIRED HELPERS FROM HEADER =================
// =====================================================

String getMacAddress() {
  return WiFi.macAddress();
}

// If you already generate and persist a deviceId somewhere else,
// replace this to load that instead of just MAC.
String generateDeviceId() {
  if (deviceMac.length() == 0) {
    deviceMac = WiFi.macAddress();
  }
  return deviceMac;
}

// Some other modules still use HTTPClient and expect us to attach
// auth headers. We keep this helper compatible with that.
void addAuthHeaders(HTTPClient &http, bool includeContentType) {
  (void)includeContentType;

  if (deviceId.length() == 0) {
    deviceId = getMacAddress();
  }

  // Get device secret from settings_manager (stored in NVS)
  String deviceSecret = settings_manager_get_device_secret();

  String nowIso = iso8601Now();
  String signingMessage = deviceId + "|" + nowIso;
  String sigHex = hmac_sha256_hex_hexKey(deviceSecret.c_str(), signingMessage);

  http.addHeader("X-Device-ID",        deviceId);
  http.addHeader("X-Device-Time",      nowIso);
  http.addHeader("X-Device-Signature", sigHex);

  // Caller can still add Content-Type etc.
}
