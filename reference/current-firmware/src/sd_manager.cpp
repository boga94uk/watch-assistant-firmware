// ============================================
// SD CARD MANAGER - ESP-IDF Native Implementation
// ============================================
// Combined from working Platformio_audio_test + Device_Rom_Complete
// Uses ESP-IDF native SDMMC + POSIX file API

#include "sd_manager.h"

// SD card state
bool sdCardAvailable = false;
sdmmc_card_t* sdCard = NULL;
uint64_t sdCardSize = 0;
uint64_t sdCardUsed = 0;
uint64_t sdCardFree = 0;
String sdCardType = "UNKNOWN";

// ============================================
// INITIALIZATION
// ============================================

bool initSDCard(bool verbose) {
    if (verbose) {
        Serial.println("\n💾 Initializing SD card (ESP-IDF native)...");
    }
    
    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    
    // SDMMC host configuration
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    
    // Slot configuration (1-bit mode)
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;  // 1-bit mode
    slot_config.clk = (gpio_num_t)SDMMC_CLK;
    slot_config.cmd = (gpio_num_t)SDMMC_CMD;
    slot_config.d0 = (gpio_num_t)SDMMC_DATA;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    
    // Mount the filesystem
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sdCard);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            if (verbose) {
                Serial.println("❌ Failed to mount filesystem");
            }
        } else {
            if (verbose) {
                Serial.printf("❌ Failed to initialize SD card: %s\n", esp_err_to_name(ret));
            }
        }
        sdCardAvailable = false;
        return false;
    }
    
    // Get card info (check capacity to determine type)
    if (sdCard->csd.capacity > 2048) {  // >2GB = SDHC/SDXC
        sdCardType = "SDHC/SDXC";
    } else if (sdCard->csd.capacity > 0) {
        sdCardType = "SD";
    } else {
        sdCardType = "SDSC";
    }
    
    sdCardSize = ((uint64_t) sdCard->csd.capacity) * sdCard->csd.sector_size / (1024 * 1024);
    
    // Note: Used/Free space calculation requires statvfs which isn't available in Arduino
    // Set to 0 for now - can be calculated on-demand if needed
    sdCardUsed = 0;
    sdCardFree = sdCardSize;
    
    if (verbose) {
        Serial.println("✅ SD card mounted");
        sdmmc_card_print_info(stdout, sdCard);
        Serial.printf("✅ SD card size: %llu MB\n", sdCardSize);
    }
    
    sdCardAvailable = true;
    return true;
}

void unmountSDCard() {
    if (sdCardAvailable) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sdCard);
        sdCardAvailable = false;
        sdCard = NULL;
    }
}

bool remountSDCard(bool verbose) {
    // Only remount if not already mounted
    if (sdCardAvailable) {
        if (verbose) {
            Serial.println("   SD card already mounted");
        }
        return true;
    }
    
    if (verbose) {
        Serial.println("   🔌 Remounting SD card...");
    }
    
    bool result = initSDCard(false);
    
    if (result && verbose) {
        Serial.println("   ✅ SD remounted");
    } else if (!result && verbose) {
        Serial.println("   ❌ SD remount failed");
    }
    
    return result;
}

// ============================================
// BASIC FILE OPERATIONS
// ============================================

bool createDirectory(const char* path) {
    if (!sdCardAvailable) return false;
    
    struct stat st;
    if (stat(path, &st) == 0) {
        return true;  // Already exists
    }
    
    return (mkdir(path, 0775) == 0);
}

bool fileExists(const char* path) {
    if (!sdCardAvailable) return false;
    
    struct stat st;
    return (stat(path, &st) == 0);
}

bool deleteFile(const char* path) {
    if (!sdCardAvailable) return false;
    return (unlink(path) == 0);
}

bool renameFile(const char* oldPath, const char* newPath) {
    if (!sdCardAvailable) return false;
    return (rename(oldPath, newPath) == 0);
}

bool writeFile(const char* path, const char* data) {
    if (!sdCardAvailable) return false;
    
    FILE* f = fopen(path, "w");
    if (f == NULL) return false;
    
    fputs(data, f);
    fclose(f);
    return true;
}

String readFile(const char* path) {
    if (!sdCardAvailable) return "";
    
    FILE* f = fopen(path, "r");
    if (f == NULL) return "";
    
    String content = "";
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        content += buffer;
    }
    fclose(f);
    
    return content;
}

bool appendFile(const char* path, const char* data) {
    if (!sdCardAvailable) return false;
    
    FILE* f = fopen(path, "a");
    if (f == NULL) return false;
    
    fputs(data, f);
    fclose(f);
    return true;
}

void listDirectory(const char* path) {
    if (!sdCardAvailable) {
        Serial.println(" SD card not available");
        return;
    }
    
    Serial.printf("\n Listing: %s\n", path);
    
    DIR* dir = opendir(path);
    if (dir == NULL) {
        Serial.println(" Failed to open directory");
        return;
    }
    
    struct dirent* entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            Serial.printf("  %s/\n", entry->d_name);
        } else {
            char fullPath[256];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entry->d_name);
            struct stat st;
            if (stat(fullPath, &st) == 0) {
                Serial.printf("  %s (%ld bytes)\n", entry->d_name, st.st_size);
            } else {
                Serial.printf("  %s\n", entry->d_name);
            }
            count++;
        }
    }
    
    closedir(dir);
    Serial.printf(" Total files: %d\n", count);
}

// ============================================
// FILE STREAMING SUPPORT (POSIX API)
// ============================================

FILE* openFileForWrite(const char* path) {
    if (!sdCardAvailable) return NULL;
    return fopen(path, "w");
}

FILE* openFileForRead(const char* path) {
    if (!sdCardAvailable) return NULL;
    return fopen(path, "r");
}

FILE* openFileForAppend(const char* path) {
    if (!sdCardAvailable) return NULL;
    return fopen(path, "a");
}

// ============================================
// AUDIO-SPECIFIC FUNCTIONS
// ============================================

bool createRecordingsDirectory() {
    bool recordingsOk = createDirectory(SD_MOUNT_POINT "/recordings");
    bool ttsOk = createDirectory(SD_MOUNT_POINT "/tts");
    bool queueOk = createDirectory(SD_MOUNT_POINT "/queue");
    return recordingsOk && ttsOk && queueOk;
}

String getNextRecordingFilename() {
    if (!sdCardAvailable) return "";
    
    int fileNum = 0;
    String filename;
    char fullPath[256];
    
    do {
        snprintf(fullPath, sizeof(fullPath), SD_MOUNT_POINT "/recordings/rec_%d.wav", fileNum);
        filename = fullPath;
        fileNum++;
    } while (fileExists(fullPath) && fileNum < 10000);
    
    return filename;
}

String getLastRecordingFilename() {
    if (!sdCardAvailable) return "";
    
    DIR* dir = opendir(SD_MOUNT_POINT "/recordings");
    if (!dir) return "";
    
    String lastFile = "";
    time_t newestTime = 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        String name = String(entry->d_name);
        // Look for rec_*.wav files (now using RTC timestamps: rec_2025-10-20_01-48-30.wav)
        // Also skip test_audio.wav
        if (name.startsWith("rec_") && name.endsWith(".wav") && name != "test_audio.wav") {
            String fullPath = String(SD_MOUNT_POINT "/recordings/") + name;
            
            // Get file modification time
            struct stat fileStat;
            if (stat(fullPath.c_str(), &fileStat) == 0) {
                // Compare modification time
                if (fileStat.st_mtime > newestTime) {
                    newestTime = fileStat.st_mtime;
                    lastFile = fullPath;
                }
            }
        }
    }
    closedir(dir);
    
    return lastFile;
}

bool saveAudioRecording(const char* filename, uint8_t* data, size_t length) {
    if (!sdCardAvailable) return false;
    
    FILE* f = fopen(filename, "w");
    if (f == NULL) {
        Serial.printf(" Failed to create: %s\n", filename);
        return false;
    }
    
    size_t written = fwrite(data, 1, length, f);
    fclose(f);
    
    if (written == length) {
        Serial.printf(" Saved: %s (%d bytes)\n", filename, length);
        return true;
    } else {
        Serial.printf(" Write error: %s (wrote %d/%d bytes)\n", filename, written, length);
        return false;
    }
}

bool deleteOldestRecording() {
    if (!sdCardAvailable) return false;
    
    DIR* dir = opendir(SD_MOUNT_POINT "/recordings");
    if (dir == NULL) return false;
    
    String oldestFile = "";
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            String filename = String(entry->d_name);
            if (filename.endsWith(".wav")) {
                if (oldestFile == "") {
                    oldestFile = SD_MOUNT_POINT "/recordings/" + filename;
                }
            }
        }
    }
    
    closedir(dir);
    
    if (oldestFile.length() > 0) {
        return deleteFile(oldestFile.c_str());
    }
    
    return false;
}

int getRecordingCount() {
    if (!sdCardAvailable) return 0;
    
    DIR* dir = opendir(SD_MOUNT_POINT "/recordings");
    if (dir == NULL) return 0;
    
    int count = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            String filename = String(entry->d_name);
            if (filename.endsWith(".wav")) {
                count++;
            }
        }
    }
    
    closedir(dir);
    return count;
}

uint64_t getRecordingsSize() {
    if (!sdCardAvailable) return 0;
    
    DIR* dir = opendir(SD_MOUNT_POINT "/recordings");
    if (dir == NULL) return 0;
    
    uint64_t totalSize = 0;
    struct dirent* entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            String filename = String(entry->d_name);
            if (filename.endsWith(".wav")) {
                char fullPath[256];
                snprintf(fullPath, sizeof(fullPath), SD_MOUNT_POINT "/recordings/%s", entry->d_name);
                struct stat st;
                if (stat(fullPath, &st) == 0) {
                    totalSize += st.st_size;
                }
            }
        }
    }
    
    closedir(dir);
    return totalSize;
}

// ============================================
// AUDIO RECORDING STREAMING API
// ============================================

// Helper: Create WAV header
static void createWAVHeader(uint8_t* header, uint32_t dataSize, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
    uint16_t blockAlign = channels * (bitsPerSample / 8);
    uint32_t chunkSize = dataSize + 36;
    
    memcpy(header, "RIFF", 4);
    memcpy(header + 4, &chunkSize, 4);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(header + 16, &fmtSize, 4);
    uint16_t audioFormat = 1;  // PCM
    memcpy(header + 20, &audioFormat, 2);
    memcpy(header + 22, &channels, 2);
    memcpy(header + 24, &sampleRate, 4);
    memcpy(header + 28, &byteRate, 4);
    memcpy(header + 32, &blockAlign, 2);
    memcpy(header + 34, &bitsPerSample, 2);
    memcpy(header + 36, "data", 4);
    memcpy(header + 40, &dataSize, 4);
}

FILE* openRecordingFile(const char* filename) {
    if (!sdCardAvailable) {
        Serial.println("❌ SD card not available");
        return NULL;
    }
    
    FILE* f = fopen(filename, "w");
    if (f == NULL) {
        Serial.printf("❌ Failed to create recording file: %s\n", filename);
        return NULL;
    }
    
    // Write placeholder WAV header (will be updated on finalize)
    uint8_t wavHeader[44];
    createWAVHeader(wavHeader, 0, 16000, 1, 16);  // Placeholder values
    fwrite(wavHeader, 1, 44, f);
    
    return f;
}

size_t writeRecordingData(FILE* file, const uint8_t* data, size_t length) {
    if (file == NULL || data == NULL || length == 0) {
        return 0;
    }
    
    return fwrite(data, 1, length, file);
}

bool finalizeRecordingFile(FILE* file, uint32_t totalDataBytes, uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample) {
    if (file == NULL) {
        return false;
    }
    
    // Create final WAV header with actual data size
    uint8_t wavHeader[44];
    createWAVHeader(wavHeader, totalDataBytes, sampleRate, channels, bitsPerSample);
    
    // Seek to beginning and update header
    fseek(file, 0, SEEK_SET);
    fwrite(wavHeader, 1, 44, file);
    
    // Close file
    fclose(file);
    
    return true;
}

// ============================================
// TEST FUNCTIONS
// ============================================

bool testSDCardWrite() {
    if (!sdCardAvailable) return false;
    
    String testData = "ESP Assistant Test - " + String(millis());
    FILE* f = fopen(SD_MOUNT_POINT "/test_write.txt", "w");
    
    if (f == NULL) {
        Serial.println(" Write test failed: Cannot create file");
        return false;
    }
    
    fputs(testData.c_str(), f);
    fclose(f);
    
    Serial.println(" Write test passed");
    return true;
}

bool testSDCardRead() {
    if (!sdCardAvailable) return false;
    
    FILE* f = fopen(SD_MOUNT_POINT "/test_write.txt", "r");
    if (f == NULL) {
        Serial.println(" Read test failed: Cannot open file");
        return false;
    }
    
    char buffer[256];
    int bytesRead = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        bytesRead += strlen(buffer);
    }
    fclose(f);
    
    Serial.printf(" Read test passed (read %d bytes)\n", bytesRead);
    return true;
}

String getSDCardInfo() {
    if (!sdCardAvailable) {
        return " SD card not available\nNo card inserted or initialization failed.";
    }
    
    String info = "Card Type: " + sdCardType + "\n";
    info += "Card Size: " + String(sdCardSize) + " MB\n";
    info += "Used: " + String(sdCardUsed) + " MB\n";
    info += "Free: " + String(sdCardFree) + " MB\n\n";
    
    bool writeTest = testSDCardWrite();
    bool readTest = testSDCardRead();
    
    info += "Write Test: " + String(writeTest ? " PASS" : " FAIL") + "\n";
    info += "Read Test: " + String(readTest ? " PASS" : " FAIL") + "\n";
    
    if (writeTest && readTest) {
        info += "\n All tests passed!";
    } else {
        info += "\n Some tests failed!";
    }
    
    return info;
}

uint64_t getSDCardSize() {
    return sdCardSize;
}

uint64_t getSDCardFree() {
    return sdCardFree;
}

bool isSDCardAvailable() {
    return sdCardAvailable;
}
