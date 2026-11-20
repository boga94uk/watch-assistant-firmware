// ============================================
// UPLOAD QUEUE MANAGER
// ============================================
// Handles failed upload queue with PSRAM + SD persistence
// - Keep recent recordings in PSRAM for fast retry
// - Persist to SD for reboot survival
// - Dynamic admission based on free PSRAM
// - FIFO with exponential backoff

#pragma once

#include <Arduino.h>
#include <deque>
#include <vector>

// Queue item structure
struct QueuedRecording {
    uint8_t* data;          // WAV blob in PSRAM (nullptr if SD-only)
    size_t size;            // WAV file size
    String filename;        // SD path: /sdcard/queue/rec_xxx.wav
    uint32_t timestamp;     // Epoch seconds
};

// Configuration
#define PSRAM_SOFT_FLOOR (3 * 1024 * 1024)  // 3MB - stop queueing below this
#define QUEUE_DIR "/sdcard/queue"           // SD queue directory

// ============================================
// INITIALIZATION
// ============================================

/**
 * Initialize queue system (call on boot)
 * Creates queue directory and restores from SD
 */
void queue_init();

/**
 * Check if we can start a new recording (PSRAM check)
 * @param estimatedSize Estimated recording size (bytes)
 * @return true if PSRAM has room
 */
bool queue_can_record(size_t estimatedSize = 256 * 1024);

// ============================================
// QUEUE MANAGEMENT
// ============================================

/**
 * Enqueue current recording after failed upload
 * Saves to SD and keeps in PSRAM if space available
 * @param wavData Pointer to complete WAV file
 * @param size WAV file size
 * @param filename Original filename (for logging)
 * @return true if successfully queued
 */
bool queue_enqueue_recording(uint8_t* wavData, size_t size, const String& filename);

/**
 * Try to upload oldest queued recording
 * @return true if upload succeeded (item removed from queue)
 */
bool queue_try_upload_next();

/**
 * Get queue size
 * @return Number of items in queue
 */
size_t queue_size();

/**
 * Get free PSRAM
 * @return Free PSRAM in bytes
 */
size_t queue_get_free_psram();

/**
 * Process queue (call periodically when WiFi connected)
 * Attempts to upload queued items with backoff
 */
void queue_process();

// ============================================
// SD PERSISTENCE
// ============================================

/**
 * Restore queue from SD on boot
 * Loads queue file list (does not load to PSRAM)
 */
void queue_restore_from_sd();

/**
 * Clean up queue (remove old/corrupted files)
 */
void queue_cleanup();

// Queue item info for display
struct QueueItemInfo {
    String filename;        // Full path
    String time_str;        // Formatted time (e.g., "22:07")
    String duration_str;    // Formatted duration (e.g., "3.2s")
    int duration_sec;       // Duration in seconds
};

/**
 * Get list of all queued filenames
 * @return String with all filenames separated by newlines
 */
String queue_get_filenames_list();

/**
 * Get detailed queue info for display (time + duration)
 * @return std::vector of QueueItemInfo
 */
std::vector<QueueItemInfo> queue_get_items_info();

/**
 * Get oldest queued filename
 * @return Oldest filename or empty string if queue empty
 */
String queue_get_oldest_filename();

/**
 * Delete all queue files
 * @return true if successful
 */
bool queue_delete_all();

/**
 * Move processed file to /processed folder
 * @param filename Filename to move
 * @return true if successful
 */
bool queue_move_to_processed(const String& filename);

/**
 * Remove item from in-memory queue (after manual delete)
 * @param filename Full path to file that was deleted
 */
void queue_remove_from_memory(const String& filename);
