#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ============================================
// HTTPS CLIENT FOR SERVER COMMUNICATION
// ============================================

// Server response types (Say/Ask/Confirm)
enum ResponseType {
  RESPONSE_SAY,      // Display and end (no interaction needed)
  RESPONSE_ASK,      // Display and wait for audio response
  RESPONSE_CONFIRM,  // Display and wait for button confirmation
  RESPONSE_UNKNOWN   // Parsing failed or unexpected type
};

// Server response structure
struct ServerResponse {
  bool success;
  int httpCode;
  ResponseType type;           // NEW: say/ask/confirm
  String conversationId;       // For multi-turn conversations
  String transcription;
  String message;              // The text to display/speak
  String ttsUrl;               // Audio URL (may be null)
  String command;              // For confirm type: the command name
  String errorMessage;
};

// Server configuration
extern String serverUrl;
extern String deviceMac;
extern String deviceId;

// Upload control
extern volatile bool g_upload_cancel;  // Set to true to cancel ongoing upload

// Function declarations
void initHttpsClient();
bool loadServerConfig();
bool saveServerConfig(const char* url);
String getServerUrl();
bool setServerUrl(const char* url);

// API calls
ServerResponse transcribeAudio(const char* audioFilePath);
ServerResponse transcribeAudioFromBuffer(const uint8_t* wavData, size_t size, const char* logFilename);
ServerResponse executeCommand(const char* conversationId, const char* command);  // NEW: /execute endpoint
bool sendHeartbeat(const char* state, int battery, int wifiRssi);
bool downloadAudio(const char* url, const char* savePath);
bool acknowledgePlayback(const char* conversationId, const char* audioFile);
bool checkServerStatus();

// Get downloaded TTS buffer (in PSRAM) for direct playback
// Returns nullptr if TTS was saved to SD card (too large for PSRAM)
const uint8_t* getTtsBuffer(size_t* outSize);

// Check if last downloaded TTS was saved to SD card (file >2 MB)
bool isTtsOnSdCard();

// Get SD card path for TTS file (only valid if isTtsOnSdCard() returns true)
const char* getTtsSdCardPath();

// Get total size of last downloaded TTS (works for both PSRAM and SD)
size_t getTtsSize();

// Free TTS buffers (call after playback complete to free PSRAM)
void freeTtsBuffers();

// Helper functions
String getMacAddress();
String generateDeviceId();
void addAuthHeaders(HTTPClient &http, bool includeContentType = true);

#endif // HTTPS_CLIENT_H
