// ============================================
// RTC MANAGER - Implementation
// ============================================

#include "rtc_manager.h"
#include "hardware_pins.h"
#include "sd_manager.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <FS.h>

// RTC state
bool rtcInitialized = false;
SensorPCF85063 rtc;

bool initRTC() {
  Serial.println("\n🕐 Initializing RTC (PCF85063)...");
  
  // Initialize RTC on I2C
  if (!rtc.begin(Wire, PCF85063_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("❌ Failed to initialize PCF85063");
    return false;
  }
  
  Serial.println("✅ PCF85063 initialized");
  rtcInitialized = true;
  
  // Show current RTC time
  RTC_DateTime datetime = rtc.getDateTime();
  Serial.printf("   Current RTC time: %04d-%02d-%02d %02d:%02d:%02d\n",
                   datetime.year, datetime.month, datetime.day,
                   datetime.hour, datetime.minute, datetime.second);
  
  return true;
}

bool syncTimeFromWiFi() {
  if (!rtcInitialized) {
    Serial.println("❌ RTC not initialized");
    return false;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️  WiFi not connected - cannot sync time");
    return false;
  }
  
  Serial.println("🌐 Syncing time from NTP server...");
  
  // Configure NTP
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  // Wait for time to be set (max 10 seconds)
  int attempts = 0;
  time_t now = 0;
  struct tm timeinfo = {0};
  
  while (timeinfo.tm_year < (2024 - 1900) && attempts < 20) {
    delay(500);
    time(&now);
    localtime_r(&now, &timeinfo);
    attempts++;
  }
  
  if (timeinfo.tm_year < (2024 - 1900)) {
    Serial.println("❌ Failed to get time from NTP");
    return false;
  }
  
  // Set RTC from NTP time
  rtc.setDateTime(
    timeinfo.tm_year + 1900,  // Year
    timeinfo.tm_mon + 1,      // Month (0-11 -> 1-12)
    timeinfo.tm_mday,         // Day
    timeinfo.tm_hour,         // Hour
    timeinfo.tm_min,          // Minute
    timeinfo.tm_sec           // Second
  );
  
  Serial.println("✅ RTC synced from NTP");
  Serial.printf("   Time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                   timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                   timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  
  return true;
}

String getCurrentTime() {
  if (!rtcInitialized) {
    return "RTC N/A";
  }
  
  RTC_DateTime datetime = rtc.getDateTime();
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", 
           datetime.hour, datetime.minute, datetime.second);
  return String(buffer);
}

String getCurrentDate() {
  if (!rtcInitialized) {
    return "RTC N/A";
  }
  
  RTC_DateTime datetime = rtc.getDateTime();
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", 
           datetime.year, datetime.month, datetime.day);
  return String(buffer);
}

String getCurrentDateTime() {
  if (!rtcInitialized) {
    return "RTC N/A";
  }
  
  RTC_DateTime datetime = rtc.getDateTime();
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", 
           datetime.year, datetime.month, datetime.day,
           datetime.hour, datetime.minute, datetime.second);
  return String(buffer);
}

String getTimestampForFilename() {
  if (!rtcInitialized) {
    // Fallback to millis if RTC not available
    return String(millis());
  }
  
  RTC_DateTime datetime = rtc.getDateTime();
  char buffer[32];
  // Use underscores and hyphens (filename-safe)
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d_%02d-%02d-%02d", 
           datetime.year, datetime.month, datetime.day,
           datetime.hour, datetime.minute, datetime.second);
  return String(buffer);
}

void setRTCTime(int year, int month, int day, int hour, int minute, int second) {
  if (!rtcInitialized) {
    return;
  }
  
  rtc.setDateTime(year, month, day, hour, minute, second);
  Serial.printf("🕐 RTC time set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                   year, month, day, hour, minute, second);
}

bool setTimeFromLastRecording() {
  Serial.println("🕐 Looking for last recording to set fallback time...");
  
  // Check if SD is available
  if (!sdCardAvailable) {
    Serial.println("   ⚠️  SD card not available");
    return false;
  }
  
  // Open recordings directory
  DIR* dir = opendir(SD_MOUNT_POINT "/recordings");
  if (!dir) {
    Serial.println("   ⚠️  Recordings directory not found");
    return false;
  }
  
  // Find the most recent file
  time_t newest_time = 0;
  String newest_filename = "";
  
  struct dirent* entry;
  while ((entry = readdir(dir)) != NULL) {
    // Skip "." and ".."
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    
    String filename = String(entry->d_name);
    
    // Parse filename: rec_YYYY-MM-DD_HH-MM-SS.wav
    if (filename.startsWith("rec_") && filename.endsWith(".wav")) {
      // STRICT validation: Must be exactly 27 characters
      // rec_2025-10-20_23-29-56.wav
      // 012345678901234567890123456
      if (filename.length() != 27) {
        continue;  // Skip invalid format
      }
      
      // Validate structure: rec_YYYY-MM-DD_HH-MM-SS.wav
      if (filename.charAt(8) != '-' || filename.charAt(11) != '-' || 
          filename.charAt(14) != '_' || filename.charAt(17) != '-' || 
          filename.charAt(20) != '-') {
        continue;  // Skip invalid format
      }
      
      // Extract timestamp from filename
      // Format: rec_2025-01-20_23-45-30.wav
      //            ^^^^-^^-^^-^^-^^-^^
      int year = filename.substring(4, 8).toInt();
      int month = filename.substring(9, 11).toInt();
      int day = filename.substring(12, 14).toInt();
      int hour = filename.substring(15, 17).toInt();
      int minute = filename.substring(18, 20).toInt();
      int second = filename.substring(21, 23).toInt();
      
      // Sanity check values
      if (year < 2024 || year > 2100 || month < 1 || month > 12 || 
          day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
        continue;  // Skip invalid values
      }
      
      // Convert to time_t
      struct tm timeinfo = {0};
      timeinfo.tm_year = year - 1900;
      timeinfo.tm_mon = month - 1;
      timeinfo.tm_mday = day;
      timeinfo.tm_hour = hour;
      timeinfo.tm_min = minute;
      timeinfo.tm_sec = second;
      
      time_t file_time = mktime(&timeinfo);
      
      if (file_time > newest_time) {
        newest_time = file_time;
        newest_filename = filename;
      }
    }
  }
  
  closedir(dir);
  
  if (newest_time == 0) {
    Serial.println("   ⚠️  No valid recordings found");
    return false;
  }
  
  // Set system time to last recording + 1 second (not 1 minute!)
  newest_time += 1;  // Add 1 second to avoid exact match
  
  struct timeval tv;
  tv.tv_sec = newest_time;
  tv.tv_usec = 0;
  settimeofday(&tv, NULL);
  
  struct tm* timeinfo = localtime(&newest_time);
  Serial.printf("✅ Fallback time set from: %s\n", newest_filename.c_str());
  Serial.printf("   Current time estimate: %04d-%02d-%02d %02d:%02d:%02d\n",
               timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
               timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  
  return true;
}
