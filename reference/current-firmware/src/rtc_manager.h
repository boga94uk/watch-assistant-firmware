#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Arduino.h>
#include "SensorPCF85063.hpp"

// RTC state
extern bool rtcInitialized;
extern SensorPCF85063 rtc;

// Function declarations
bool initRTC();
bool syncTimeFromWiFi();
bool setTimeFromLastRecording();  // Fallback: set time from most recent recording
String getCurrentTime();
String getCurrentDate();
String getCurrentDateTime();
String getTimestampForFilename();  // Returns filename-safe timestamp (e.g., "2025-10-20_01-48-30")
void setRTCTime(int year, int month, int day, int hour, int minute, int second);

#endif // RTC_MANAGER_H
