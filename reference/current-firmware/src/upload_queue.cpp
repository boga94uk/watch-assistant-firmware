// ============================================
// UPLOAD QUEUE MANAGER - Implementation
// ============================================

#include "upload_queue.h"
#include "sd_manager.h"
#include "https_client.h"
#include <esp_heap_caps.h>
#include <time.h>
#include <dirent.h>
#include <SD.h>  // For File type in queue_get_items_info()

// Queue storage
static std::deque<QueuedRecording> g_queue;

// ============================================
// HELPERS
// ============================================

static size_t psram_free() {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static bool psram_has_room(size_t need) {
    return psram_free() > (PSRAM_SOFT_FLOOR + need);
}

static String makeQueueFilename() {
    // Build timestamped name
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char buf[64];
    
    if (now > 0) {
        strftime(buf, sizeof(buf), "rec_%Y-%m-%d_%H-%M-%S.wav", &t);
    } else {
        // Fallback: use millis
        snprintf(buf, sizeof(buf), "rec_%lu.wav", (unsigned long)millis());
    }
    
    return String(QUEUE_DIR) + "/" + String(buf);
}

static bool ensureQueueDir() {
    if (!fileExists(QUEUE_DIR)) {
        return createDirectory(QUEUE_DIR);
    }
    return true;
}

static bool saveToSD(const String& path, const uint8_t* data, size_t len) {
    if (!ensureQueueDir()) {
        Serial.println("❌ Failed to create queue directory");
        return false;
    }
    
    FILE* f = openFileForWrite(path.c_str());
    if (!f) {
        Serial.printf("❌ Failed to open queue file: %s\n", path.c_str());
        return false;
    }
    
    size_t written = 0;
    while (written < len) {
        size_t toWrite = min((size_t)4096, len - written);
        size_t n = fwrite(data + written, 1, toWrite, f);
        if (n == 0) {
            Serial.println("❌ SD write failed");
            fclose(f);
            deleteFile(path.c_str());
            return false;
        }
        written += n;
    }
    
    fclose(f);
    return true;
}

static bool loadFromSD(const String& path, uint8_t*& out, size_t& sz) {
    FILE* f = openFileForRead(path.c_str());
    if (!f) {
        Serial.printf("❌ Failed to open queue file: %s\n", path.c_str());
        return false;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Allocate PSRAM
    out = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out) {
        Serial.println("❌ Failed to allocate PSRAM for queue file");
        fclose(f);
        return false;
    }
    
    // Read file
    size_t read = 0;
    while (read < sz) {
        size_t toRead = min((size_t)4096, sz - read);
        size_t n = fread(out + read, 1, toRead, f);
        if (n == 0) {
            Serial.println("❌ SD read failed");
            fclose(f);
            heap_caps_free(out);
            out = nullptr;
            return false;
        }
        read += n;
    }
    
    fclose(f);
    return true;
}

// ============================================
// INITIALIZATION
// ============================================

void queue_init() {
    if (!ensureQueueDir()) {
        Serial.println("⚠️  Failed to create queue directory");
        return;
    }
    
    queue_restore_from_sd();
    
    Serial.printf("📦 Queue: %d items, %d KB PSRAM free\n", g_queue.size(), psram_free() / 1024);
}

bool queue_can_record(size_t estimatedSize) {
    // Check if we have room for new recording
    size_t freePsram = psram_free();
    size_t needed = PSRAM_SOFT_FLOOR + estimatedSize;
    
    if (freePsram < needed) {
        // PSRAM low - check if SD card is available
        // If SD has >50MB free, we can still record (will queue to SD only)
        if (isSDCardAvailable()) {
            uint64_t freeBytes = getSDCardFree();
            
            if (freeBytes > 50 * 1024 * 1024) {  // >50MB free
                Serial.printf("⚠️  PSRAM low (%d KB), but SD has space (%llu MB) - recording to SD only\n", 
                              freePsram / 1024, freeBytes / (1024 * 1024));
                return true;  // Allow recording to SD
            }
        }
        
        Serial.printf("❌ Cannot record: PSRAM < 4MB and SD unavailable/full\n");
        uint64_t sdFree = isSDCardAvailable() ? getSDCardFree() : 0;
        Serial.printf("   Free PSRAM: %d KB, SD free: %s\n", 
                      freePsram / 1024,
                      isSDCardAvailable() ? String((sdFree / (1024 * 1024))) + " MB" : "N/A");
        return false;
    }
    
    return true;
}

// ============================================
// QUEUE MANAGEMENT
// ============================================

bool queue_enqueue_recording(uint8_t* wavData, size_t size, const String& filename) {
    if (!wavData || size == 0) {
        return false;
    }
    
    // Generate queue filename
    String queuePath = makeQueueFilename();
    
    Serial.printf("📦 Queueing: %d KB\n", size / 1024);
    
    // Persist to SD first (source of truth)
    if (!saveToSD(queuePath, wavData, size)) {
        return false;
    }
    
    // Try to keep in PSRAM for fast retry
    uint8_t* copy = nullptr;
    if (psram_has_room(size)) {
        copy = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy) {
            memcpy(copy, wavData, size);
        }
    }
    
    // Add to queue
    QueuedRecording q;
    q.data = copy;
    q.size = size;
    q.filename = queuePath;
    q.timestamp = (uint32_t)time(nullptr);
    
    g_queue.push_back(q);
    
    Serial.printf("   ✅ %d items in queue\n", g_queue.size());
    return true;
}

bool queue_try_upload_next() {
    if (g_queue.empty()) {
        return false;
    }
    
    QueuedRecording& q = g_queue.front();
    
    Serial.printf("📤 Queue upload: %d KB\n", q.size / 1024);
    
    // Ensure data is in PSRAM
    if (!q.data) {
        if (!psram_has_room(q.size)) {
            Serial.println("   ⚠️  PSRAM full, cannot upload");
            return false;
        }
        
        if (!loadFromSD(q.filename, q.data, q.size)) {
            // File missing/corrupt - remove from queue
            Serial.println("   ❌ Failed to load from SD - removing from queue");
            deleteFile(q.filename.c_str());
            g_queue.pop_front();
            return true;  // Continue to next item
        }
    }
    
    // Try upload (single attempt)
    ServerResponse response = transcribeAudioFromBuffer(q.data, q.size, q.filename.c_str());
    
    if (response.success) {
        // Success - remove from queue
        Serial.println("   ✅ Upload succeeded!");
        if (q.data) {
            heap_caps_free(q.data);
        }
        deleteFile(q.filename.c_str());
        g_queue.pop_front();
        return true;
    }
    
    // Failed - keep in queue for user to retry manually
    Serial.printf("   ❌ Upload failed: %s\n", response.errorMessage.c_str());
    Serial.println("   💡 Item remains in queue - press upload button to retry");
    
    // Free PSRAM copy to relieve pressure
    if (q.data) {
        heap_caps_free(q.data);
        q.data = nullptr;
    }
    
    return false;
}

size_t queue_size() {
    return g_queue.size();
}

size_t queue_get_free_psram() {
    return psram_free();
}

void queue_process() {
    // MANUAL ONLY: User must press button to upload
    // This function just checks server status for UI updates
    // Actual upload happens via queue_upload_selected() called by button handler
    
    // Future: Could update UI here to show server status
}

// ============================================
// SD PERSISTENCE
// ============================================

void queue_restore_from_sd() {
    if (!ensureQueueDir()) {
        return;
    }
    
    DIR* dir = opendir(QUEUE_DIR);
    if (!dir) {
        return;
    }
    
    struct dirent* entry;
    int count = 0;
    
    while ((entry = readdir(dir)) != nullptr) {
        // Skip . and ..
        if (entry->d_name[0] == '.') {
            continue;
        }
        
        // Only .wav files
        String name = String(entry->d_name);
        if (!name.endsWith(".wav")) {
            continue;
        }
        
        String fullPath = String(QUEUE_DIR) + "/" + name;
        
        // Get file size
        FILE* f = openFileForRead(fullPath.c_str());
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        size_t sz = ftell(f);
        fclose(f);
        
        // Add to queue (SD-only, no PSRAM preload)
        QueuedRecording q;
        q.data = nullptr;
        q.size = sz;
        q.filename = fullPath;
        q.timestamp = 0;
        
        g_queue.push_back(q);
        count++;
    }
    
    closedir(dir);
    
    Serial.printf("   ✅ Restored %d items from SD\n", count);
}

void queue_cleanup() {
    // Future: Remove files older than N days, corrupted files, etc.
    Serial.println("🧹 Queue cleanup not yet implemented");
}

String queue_get_filenames_list() {
    String list = "";
    int count = 0;
    
    for (const auto& item : g_queue) {
        // Extract just the filename from the full path
        int lastSlash = item.filename.lastIndexOf('/');
        String basename = (lastSlash >= 0) ? item.filename.substring(lastSlash + 1) : item.filename;
        
        list += basename;
        list += "\n";
        count++;
    }
    
    if (count == 0) {
        return "No recordings in queue";
    }
    
    return list;
}

std::vector<QueueItemInfo> queue_get_items_info() {
    std::vector<QueueItemInfo> items;
    
    int index = 1;  // Start indexing at 1
    for (const auto& qitem : g_queue) {
        QueueItemInfo info;
        info.filename = qitem.filename;
        
        // Extract timestamp from filename: rec_2025-10-27_22-07-11.wav
        int lastSlash = qitem.filename.lastIndexOf('/');
        String basename = (lastSlash >= 0) ? qitem.filename.substring(lastSlash + 1) : qitem.filename;
        
        // Parse: rec_YYYY-MM-DD_HH-MM-SS.wav (27 chars total)
        // Position:  rec_2025-10-27_22-07-11.wav
        // Indices:   0  4   8  1  1 1 1 1 2 2 2
        //                      1  4 5 7 8 0 1 3
        String dateStr = "??";
        String monthStr = "??";
        String yearStr = "????";
        String hourStr = "??";
        String minStr = "??";
        String secStr = "??";
        
        if (basename.startsWith("rec_") && basename.length() == 27) {
            yearStr = basename.substring(4, 8);     // YYYY
            monthStr = basename.substring(9, 11);   // MM
            dateStr = basename.substring(12, 14);   // DD
            hourStr = basename.substring(15, 17);   // HH
            minStr = basename.substring(18, 20);    // MM
            secStr = basename.substring(21, 23);    // SS
        }
        
        // Get duration by reading WAV header or estimating from file size
        String durStr = "?.?s";
        
        FILE* f = openFileForRead(qitem.filename.c_str());
        if (f) {
            // Get file size
            fseek(f, 0, SEEK_END);
            size_t fileSize = ftell(f);
            fseek(f, 0, SEEK_SET);
            
            if (fileSize > 44) {
                // Try reading WAV header first
                fseek(f, 40, SEEK_SET);
                uint32_t dataSize = 0;
                size_t bytesRead = fread(&dataSize, 1, 4, f);  // Little-endian
                
                if (bytesRead == 4 && dataSize > 0 && dataSize < fileSize) {
                    // Valid WAV header data
                    const uint32_t bytesPerSecond = 16000 * 1 * 2;  // 32000 bytes/sec (16kHz, mono, 16-bit)
                    float seconds = (float)dataSize / bytesPerSecond;
                    info.duration_sec = (uint32_t)seconds;
                    durStr = String(seconds, 1) + "s";  // e.g., "4.0s"
                } else {
                    // WAV header read failed - estimate from total file size
                    const uint32_t bytesPerSecond = 16000 * 1 * 2;
                    uint32_t estimatedDataSize = fileSize - 44;  // Subtract header
                    float seconds = (float)estimatedDataSize / bytesPerSecond;
                    info.duration_sec = (uint32_t)seconds;
                    durStr = String(seconds, 1) + "s";  // e.g., "4.0s"
                }
            } else {
                // File too small
                info.duration_sec = 0;
                durStr = "0.0s";
            }
            fclose(f);
        } else {
            // File open failed
            info.duration_sec = 0;
        }
        
        // Format with fixed-width columns for table-like alignment:
        // "## | DD.MM.YYYY | HH:MM:SS | ##.#s"
        // Pad index to 2 chars for alignment
        char indexStr[8];
        snprintf(indexStr, sizeof(indexStr), "%2d", index);
        
        info.time_str = String(indexStr) + " | " + dateStr + "." + monthStr + "." + yearStr + " | " + 
                        hourStr + ":" + minStr + ":" + secStr + " | " + durStr;
        info.duration_str = durStr;  // Keep for compatibility
        
        items.push_back(info);
        index++;
    }
    
    return items;
}

String queue_get_oldest_filename() {
    if (g_queue.empty()) {
        return "";
    }
    return g_queue.front().filename;
}

bool queue_delete_all() {
    Serial.printf("🗑️  Deleting all %d queue files...\n", g_queue.size());
    
    bool success = true;
    
    // Delete from SD and free PSRAM
    while (!g_queue.empty()) {
        QueuedRecording& item = g_queue.front();
        
        // Delete SD file
        if (!deleteFile(item.filename.c_str())) {
            Serial.printf("   ⚠️  Failed to delete: %s\n", item.filename.c_str());
            success = false;
        }
        
        // Free PSRAM if allocated
        if (item.data) {
            heap_caps_free(item.data);
        }
        
        g_queue.pop_front();
    }
    
    Serial.println(success ? "   ✅ All queue files deleted" : "   ⚠️  Some files failed to delete");
    return success;
}

/**
 * @brief Remove an item from the in-memory queue
 * Called after delete/move/upload to keep g_queue in sync with SD
 */
void queue_remove_from_memory(const String& filename) {
    for (size_t i = 0; i < g_queue.size(); i++) {
        if (g_queue[i].filename == filename) {
            // Free PSRAM buffer if cached
            if (g_queue[i].data) {
                heap_caps_free(g_queue[i].data);
            }
            g_queue.erase(g_queue.begin() + i);
            Serial.printf("   🗑️  Removed from memory queue: %s\n", filename.c_str());
            break;
        }
    }
}

bool queue_move_to_processed(const String& filename) {
    // Create processed directory if it doesn't exist
    const char* processedDir = "/sdcard/processed";
    if (!fileExists(processedDir)) {
        if (!createDirectory(processedDir)) {
            Serial.println("❌ Failed to create processed directory");
            return false;
        }
    }
    
    // Extract basename
    int lastSlash = filename.lastIndexOf('/');
    String basename = (lastSlash >= 0) ? filename.substring(lastSlash + 1) : filename;
    
    // Build destination path
    String destPath = String(processedDir) + "/" + basename;
    
    // Read source file
    FILE* src = openFileForRead(filename.c_str());
    if (!src) {
        Serial.printf("❌ Failed to open source: %s\n", filename.c_str());
        return false;
    }
    
    // Get size
    fseek(src, 0, SEEK_END);
    size_t size = ftell(src);
    fseek(src, 0, SEEK_SET);
    
    // Open destination
    FILE* dest = openFileForWrite(destPath.c_str());
    if (!dest) {
        Serial.printf("❌ Failed to open dest: %s\n", destPath.c_str());
        fclose(src);
        return false;
    }
    
    // Copy in chunks
    uint8_t buffer[4096];
    size_t copied = 0;
    while (copied < size) {
        size_t toRead = min((size_t)4096, size - copied);
        size_t n = fread(buffer, 1, toRead, src);
        if (n == 0) break;
        fwrite(buffer, 1, n, dest);
        copied += n;
    }
    
    fclose(src);
    fclose(dest);
    
    // Delete original
    if (!deleteFile(filename.c_str())) {
        Serial.printf("⚠️  Failed to delete original: %s\n", filename.c_str());
    }
    
    // Remove from in-memory queue
    queue_remove_from_memory(filename);
    
    Serial.printf("   ✅ Moved to processed: %s\n", basename.c_str());
    return true;
}
