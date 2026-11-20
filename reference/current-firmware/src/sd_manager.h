// ============================================
// SD CARD MANAGER - ESP-IDF Native Version
// ============================================
// Handles all SD card operations using ESP-IDF native API:
// - SDMMC peripheral (1-bit mode)
// - VFS FAT filesystem mounting
// - POSIX file operations (fopen, fwrite, etc.)
// - Audio file storage and management
// Based on working Platformio_audio_test + Device_Rom_Complete features

#pragma once

#include <Arduino.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dirent.h>

// SD card state
extern bool sdCardAvailable;
extern sdmmc_card_t* sdCard;
extern uint64_t sdCardSize;
extern uint64_t sdCardUsed;
extern uint64_t sdCardFree;
extern String sdCardType;

// Pin definitions (from Device_Rom_Complete - CORRECT for this hardware!)
#define SDMMC_CLK  2
#define SDMMC_CMD  1
#define SDMMC_DATA 3

// Mount point
#define SD_MOUNT_POINT "/sdcard"

// ============================================
// INITIALIZATION
// ============================================

/**
 * Initialize SD card using ESP-IDF native SDMMC
 * NOTE: IO Expander must power SD card (pin 7) BEFORE calling this!
 * NOTE: Wait 500ms after power-on before calling this!
 * @param verbose Print detailed info
 * @return true if successful
 */
bool initSDCard(bool verbose = false);
void unmountSDCard();
bool remountSDCard(bool verbose = false);
bool createDirectory(const char* path);
bool fileExists(const char* path);
bool deleteFile(const char* path);
bool renameFile(const char* oldPath, const char* newPath);
bool writeFile(const char* path, const char* data);
String readFile(const char* path);
bool appendFile(const char* path, const char* data);
void listDirectory(const char* path);

// ============================================
// FILE OPERATIONS (POSIX API)
// ============================================

/**
 * Open file for writing (POSIX style)
 * @param path File path (e.g. "/sdcard/test.txt")
 * @return FILE* pointer (use fwrite/fclose)
 */
FILE* openFileForWrite(const char* path);

/**
 * Open file for reading (POSIX style)
 * @param path File path
 * @return FILE* pointer (use fread/fclose)
 */
FILE* openFileForRead(const char* path);

/**
 * Open file for appending (POSIX style)
 * @param path File path
 * @return FILE* pointer (use fwrite/fclose)
 */
FILE* openFileForAppend(const char* path);

// ============================================
// AUDIO-SPECIFIC FUNCTIONS
// ============================================
bool createRecordingsDirectory();
String getNextRecordingFilename();
String getLastRecordingFilename();
bool saveAudioRecording(const char* filename, uint8_t* data, size_t length);
bool deleteOldestRecording();
int getRecordingCount();
uint64_t getRecordingsSize();

// ============================================
// AUDIO RECORDING STREAMING API
// ============================================

/**
 * Open a new recording file for streaming audio data
 * Creates file and writes placeholder WAV header
 * @param filename Full path to file (e.g. "/sdcard/recordings/rec_123.wav")
 * @return FILE* pointer or NULL on error
 */
FILE* openRecordingFile(const char* filename);

/**
 * Write audio data to recording file
 * @param file FILE* from openRecordingFile()
 * @param data Audio data buffer
 * @param length Number of bytes to write
 * @return Number of bytes written
 */
size_t writeRecordingData(FILE* file, const uint8_t* data, size_t length);

/**
 * Finalize recording file (update WAV header and close)
 * @param file FILE* from openRecordingFile()
 * @param totalDataBytes Total audio data bytes written (excluding header)
 * @param sampleRate Sample rate (e.g. 16000)
 * @param channels Number of channels (1=mono, 2=stereo)
 * @param bitsPerSample Bits per sample (8 or 16)
 * @return true if successful
 */
bool finalizeRecordingFile(FILE* file, uint32_t totalDataBytes, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample);

// Test functions (for test mode)
bool testSDCardWrite();
bool testSDCardRead();
String getSDCardInfo();
uint64_t getSDCardSize();
uint64_t getSDCardFree();
bool isSDCardAvailable();
