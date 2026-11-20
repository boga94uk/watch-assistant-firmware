/**
 * @file display_manager.cpp
 * @brief Display manager implementation
 */

#include <Arduino.h>
#include "display/display_manager.h"
#include "display/display_screens.h"
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "settings_manager.h"

// Brightness transition state
static int g_brightness_before_transition = 0;  // Store original brightness for restore

// Manager state
typedef struct {
    lv_obj_t *screen_boot;
    lv_obj_t *screen_idle;
    lv_obj_t *screen_recording;
    lv_obj_t *screen_uploading;
    lv_obj_t *screen_processing;
    lv_obj_t *screen_downloading;
    lv_obj_t *screen_playing;
    lv_obj_t *screen_confirmation;
    lv_obj_t *screen_queued;
    lv_obj_t *screen_sleep;
    lv_obj_t *screen_settings;
    lv_obj_t *screen_error;
    
    display_state_t current_state;
    display_event_cb_t event_cb;
    idle_status_t status;
} display_manager_t;

static display_manager_t manager = {
    .screen_boot = NULL,
    .screen_idle = NULL,
    .screen_recording = NULL,
    .screen_uploading = NULL,
    .screen_processing = NULL,
    .screen_downloading = NULL,
    .screen_playing = NULL,
    .screen_confirmation = NULL,
    .screen_queued = NULL,
    .screen_sleep = NULL,
    .screen_settings = NULL,
    .screen_error = NULL,
    .current_state = DISPLAY_STATE_BOOT,
    .event_cb = NULL,
    .status = {0}
};

/**
 * @brief Log memory usage for debugging
 */
static void log_memory(const char* tag) {
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    // Check LVGL heap (note: shows 0 when using custom PSRAM allocator)
    lv_mem_monitor_t mem_mon;
    lv_mem_monitor(&mem_mon);
    size_t lvgl_free = mem_mon.free_size / 1024;  // KB
    
    if (mem_mon.total_size > 0) {
        Serial.printf("💾 [%s] Internal: %u KB, PSRAM: %u KB, LVGL: %u KB\n",
            tag,
            (unsigned)(internal_free / 1024),
            (unsigned)(psram_free / 1024),
            (unsigned)lvgl_free);
    } else {
        Serial.printf("💾 [%s] Internal: %u KB, PSRAM: %u KB (LVGL in PSRAM)\n",
            tag,
            (unsigned)(internal_free / 1024),
            (unsigned)(psram_free / 1024));
    }
}

/**
 * @brief Delete a screen and set pointer to NULL
 */
static void delete_screen(lv_obj_t** screen_ptr) {
    if (screen_ptr && *screen_ptr) {
        lv_obj_del(*screen_ptr);
        *screen_ptr = NULL;
    }
}

/**
 * @brief Dim brightness during screen transition (phase 1: old screen visible)
 */
static void brightness_dim() {
    // Save original brightness BEFORE dimming
    g_brightness_before_transition = settings_manager_get_brightness();
    int dim_brightness = g_brightness_before_transition / 3;  // Dim to 33%
    settings_manager_set_brightness(dim_brightness, false);
    // No delay - creates jittery appearance. Screen creation time provides the dim duration.
}

/**
 * @brief Restore brightness after screen transition (phase 2: new screen visible)
 */
static void brightness_restore() {
    // Restore to original brightness saved before dimming
    if (g_brightness_before_transition > 0) {
        settings_manager_set_brightness(g_brightness_before_transition, false);
    }
}

/**
 * @brief IDLE screen event handler
 */
static void idle_event_handler(idle_event_t event) {
    if (!manager.event_cb) return;
    
    switch (event) {
        case IDLE_EVENT_MIC_CLICKED:
            manager.event_cb(manager.current_state, 1);  // Event data: 1 = mic
            break;
        case IDLE_EVENT_POWER_CLICKED:
            manager.event_cb(manager.current_state, 2);  // Event data: 2 = power
            break;
        default:
            break;
    }
}

/**
 * @brief RECORDING screen event handler
 */
static void recording_event_handler(recording_event_t event) {
    if (!manager.event_cb) return;
    
    switch (event) {
        case RECORDING_EVENT_STOP_CLICKED:
            manager.event_cb(manager.current_state, 3);  // Event data: 3 = stop
            break;
        case RECORDING_EVENT_POWER_CLICKED:
            manager.event_cb(manager.current_state, 2);  // Event data: 2 = power
            break;
        default:
            break;
    }
}

/**
 * @brief UPLOADING screen event handler
 */
static void uploading_event_handler(uploading_event_t event) {
    if (!manager.event_cb) return;
    
    switch (event) {
        case UPLOADING_EVENT_CANCEL_CLICKED:
            manager.event_cb(manager.current_state, 4);  // Event data: 4 = cancel
            break;
        case UPLOADING_EVENT_POWER_CLICKED:
            manager.event_cb(manager.current_state, 2);  // Event data: 2 = power
            break;
        default:
            break;
    }
}

/**
 * @brief Settings screen event handler
 */
static void settings_event_handler(settings_event_data_t event_data) {
    if (!manager.event_cb) return;
    
    // Forward settings events to main app
    // Event data format: event type (0-3) * 100 + value
    int encoded = (int)event_data.event * 100 + event_data.value;
    manager.event_cb(manager.current_state, encoded);
}

/**
 * @brief Queue screen event handler
 */
static void queue_event_handler(queue_event_t event) {
    if (!manager.event_cb) return;
    
    switch (event) {
        case QUEUE_EVENT_PROCESS:
            manager.event_cb(manager.current_state, 0);  // Event data: 0 = QUEUE_EVENT_PROCESS
            break;
        case QUEUE_EVENT_DELETE_ALL:
            manager.event_cb(manager.current_state, 1);  // Event data: 1 = QUEUE_EVENT_DELETE_ALL
            break;
        case QUEUE_EVENT_PLAY:
            manager.event_cb(manager.current_state, 2);  // Event data: 2 = QUEUE_EVENT_PLAY
            break;
        case QUEUE_EVENT_STOP:
            manager.event_cb(manager.current_state, 3);  // Event data: 3 = QUEUE_EVENT_STOP
            break;
        default:
            break;
    }
}

/**
 * @brief Create IDLE screen if not already created
 */
static void ensure_idle_screen(void) {
    if (!manager.screen_idle) {
        manager.screen_idle = screen_idle_create(lv_scr_act(), idle_event_handler);
        screen_idle_update_status(manager.screen_idle, &manager.status);
    }
}

/**
 * @brief Create BOOT screen if not already created
 */
static void ensure_boot_screen(void) {
    if (!manager.screen_boot) {
        manager.screen_boot = screen_boot_create(lv_scr_act());
    }
}

/**
 * @brief Create RECORDING screen if not already created
 */
static void ensure_recording_screen(void) {
    if (!manager.screen_recording) {
        manager.screen_recording = screen_recording_create(lv_scr_act(), recording_event_handler);
        screen_recording_update_status(manager.screen_recording, &manager.status);
        screen_recording_update_duration(manager.screen_recording, 0);
    }
}

/**
 * @brief Create UPLOADING screen if not already created
 */
static void ensure_uploading_screen(void) {
    if (!manager.screen_uploading) {
        manager.screen_uploading = screen_uploading_create(lv_scr_act(), uploading_event_handler);
        screen_uploading_update_status(manager.screen_uploading, &manager.status);
        screen_uploading_update_progress(manager.screen_uploading, 0, 0);
    }
}

/**
 * @brief Hide all screens
 */
static void hide_all_screens(void) {
    if (manager.screen_boot) screen_boot_set_visible(manager.screen_boot, false);
    if (manager.screen_idle) screen_idle_set_visible(manager.screen_idle, false);
    if (manager.screen_recording) screen_recording_set_visible(manager.screen_recording, false);
    if (manager.screen_uploading) screen_uploading_set_visible(manager.screen_uploading, false);
    if (manager.screen_processing) screen_placeholder_set_visible(manager.screen_processing, false);
    if (manager.screen_downloading) screen_downloading_set_visible(manager.screen_downloading, false);
    if (manager.screen_playing) screen_playing_set_visible(manager.screen_playing, false);  // FIXED: was using placeholder function!
    if (manager.screen_confirmation) screen_placeholder_set_visible(manager.screen_confirmation, false);
    if (manager.screen_queued) screen_queue_set_visible(manager.screen_queued, false);
    if (manager.screen_sleep) screen_sleep_set_visible(manager.screen_sleep, false);
    if (manager.screen_settings) screen_settings_set_visible(manager.screen_settings, false);
    if (manager.screen_error) screen_placeholder_set_visible(manager.screen_error, false);
}

/**
 * @brief Initialize display manager (public API)
 */
bool display_manager_init(display_event_cb_t event_cb) {
    memset(&manager, 0, sizeof(manager));
    manager.event_cb = event_cb;
    manager.current_state = DISPLAY_STATE_BOOT;  // Start in BOOT state
    
    // Default status
    manager.status.battery_percent = 75;
    manager.status.battery_charging = false;
    manager.status.wifi_bars = 3;
    manager.status.wifi_connected = true;
    
    // Create boot screen
    ensure_boot_screen();
    screen_boot_set_visible(manager.screen_boot, true);
    
    return true;
}

/**
 * @brief Switch screen state (public API)
 */
void display_manager_set_state(display_state_t state) {
    if (state == manager.current_state) return;
    
    // Log memory BEFORE transition
    log_memory("before_transition");
    
    // Save old state for cleanup later
    display_state_t old_state = manager.current_state;
    
    // Phase 1: Dim brightness while old screen is still visible
    // SKIP for IDLE/QUEUED→RECORDING (instant response needed for voice recording)
    bool skip_transition = ((old_state == DISPLAY_STATE_IDLE || old_state == DISPLAY_STATE_QUEUED) && 
                            state == DISPLAY_STATE_RECORDING);
    
    if (!skip_transition) {
        brightness_dim();
    }
    
    // Phase 2: Create/show new screen (while still dimmed)
    // DON'T hide old screens yet - prevents gray flash
    switch (state) {
        case DISPLAY_STATE_BOOT:
            ensure_boot_screen();
            screen_boot_set_visible(manager.screen_boot, true);
            break;
            
        case DISPLAY_STATE_IDLE:
            ensure_idle_screen();
            screen_idle_set_visible(manager.screen_idle, true);
            break;
            
        case DISPLAY_STATE_RECORDING:
            ensure_recording_screen();
            screen_recording_set_visible(manager.screen_recording, true);
            break;
            
        case DISPLAY_STATE_UPLOADING:
            ensure_uploading_screen();
            screen_uploading_set_visible(manager.screen_uploading, true);
            break;
            
        case DISPLAY_STATE_PROCESSING:
            if (!manager.screen_processing) {
                manager.screen_processing = screen_placeholder_create(lv_scr_act(), "PROCESSING", "AI is thinking...");
            }
            screen_placeholder_set_visible(manager.screen_processing, true);
            break;
            
        case DISPLAY_STATE_DOWNLOADING:
            if (!manager.screen_downloading) {
                manager.screen_downloading = screen_downloading_create(lv_scr_act(), NULL);
            }
            screen_downloading_set_visible(manager.screen_downloading, true);
            // Update status bar
            screen_downloading_update_status(manager.screen_downloading, &manager.status);
            break;
            
        case DISPLAY_STATE_PLAYING:
            if (!manager.screen_playing) {
                manager.screen_playing = screen_playing_create(lv_scr_act(), NULL);
            }
            screen_playing_set_visible(manager.screen_playing, true);
            screen_playing_set_mode(manager.screen_playing, 0);  // SAY mode (green)
            // Update status bar
            screen_playing_update_status(manager.screen_playing, &manager.status);
            break;
            
        case DISPLAY_STATE_PLAYING_ASK:
            if (!manager.screen_playing) {
                manager.screen_playing = screen_playing_create(lv_scr_act(), NULL);
            }
            screen_playing_set_visible(manager.screen_playing, true);
            screen_playing_set_mode(manager.screen_playing, 1);  // ASK mode (yellow, mic only)
            // Update status bar
            screen_playing_update_status(manager.screen_playing, &manager.status);
            break;
            
        case DISPLAY_STATE_PLAYING_CONFIRM:
            if (!manager.screen_playing) {
                manager.screen_playing = screen_playing_create(lv_scr_act(), NULL);
            }
            screen_playing_set_visible(manager.screen_playing, true);
            screen_playing_set_mode(manager.screen_playing, 2);  // CONFIRM mode (purple, mic+checkmark)
            // Update status bar
            screen_playing_update_status(manager.screen_playing, &manager.status);
            break;
            
        case DISPLAY_STATE_CONFIRMATION:
            if (!manager.screen_confirmation) {
                manager.screen_confirmation = screen_placeholder_create(lv_scr_act(), "CONFIRM?", "Press button to confirm/cancel");
            }
            screen_placeholder_set_visible(manager.screen_confirmation, true);
            break;
            
        case DISPLAY_STATE_QUEUED:
            if (!manager.screen_queued) {
                Serial.println("📋 Creating queue screen...");
                manager.screen_queued = screen_queue_create(lv_scr_act(), queue_event_handler);
                Serial.println("   ✅ Queue screen created");
            }
            screen_queue_set_visible(manager.screen_queued, true);
            // Update status bar and WiFi state
            screen_queue_update_status(manager.screen_queued, &manager.status);
            screen_queue_update_wifi(manager.screen_queued, manager.status.wifi_connected);
            break;
            
        case DISPLAY_STATE_SETTINGS:
            if (!manager.screen_settings) {
                Serial.println("📋 Creating settings screen...");
                manager.screen_settings = screen_settings_create(lv_scr_act(), settings_event_handler);
                Serial.println("   ✅ Settings screen created");
            } else {
                Serial.println("📋 Reusing existing settings screen");
            }
            screen_settings_set_visible(manager.screen_settings, true);
            break;
            
        case DISPLAY_STATE_SLEEP:
            if (!manager.screen_sleep) {
                manager.screen_sleep = screen_sleep_create(lv_scr_act());
            }
            screen_sleep_set_visible(manager.screen_sleep, true);
            break;
            
        case DISPLAY_STATE_ERROR:
            if (!manager.screen_error) {
                manager.screen_error = screen_placeholder_create(lv_scr_act(), "ERROR", "Something went wrong");
            }
            screen_placeholder_set_visible(manager.screen_error, true);
            break;
            
        case DISPLAY_STATE_TEST_CONFIG:
            // Use SETTINGS screen for test config
            if (!manager.screen_settings) {
                manager.screen_settings = screen_placeholder_create(lv_scr_act(), "TEST CONFIG", "Configuration mode");
            }
            screen_placeholder_set_visible(manager.screen_settings, true);
            break;
            
        default:
            // Fallback to IDLE
            ensure_idle_screen();
            screen_idle_set_visible(manager.screen_idle, true);
            state = DISPLAY_STATE_IDLE;
            break;
    }
    
    // Let LVGL render new screen first
    lv_timer_handler();
    
    // Phase 3: Restore brightness now that new screen is visible (skip if no dim occurred)
    if (!skip_transition) {
        brightness_restore();
    }
    
    // Log memory AFTER creating new screen
    log_memory("after_create");
    
    // NOW hide old screen (after new screen is visible)
    switch (old_state) {
        case DISPLAY_STATE_BOOT:
            if (manager.screen_boot) screen_boot_set_visible(manager.screen_boot, false);
            break;
        case DISPLAY_STATE_IDLE:
            if (manager.screen_idle) screen_idle_set_visible(manager.screen_idle, false);
            break;
        case DISPLAY_STATE_RECORDING:
            if (manager.screen_recording) screen_recording_set_visible(manager.screen_recording, false);
            break;
        case DISPLAY_STATE_UPLOADING:
            if (manager.screen_uploading) screen_uploading_set_visible(manager.screen_uploading, false);
            break;
        case DISPLAY_STATE_DOWNLOADING:
            if (manager.screen_downloading) screen_downloading_set_visible(manager.screen_downloading, false);
            break;
        case DISPLAY_STATE_PLAYING:
        case DISPLAY_STATE_PLAYING_ASK:
        case DISPLAY_STATE_PLAYING_CONFIRM:
            if (manager.screen_playing) screen_playing_set_visible(manager.screen_playing, false);
            break;
        case DISPLAY_STATE_SLEEP:
            if (manager.screen_sleep) screen_sleep_set_visible(manager.screen_sleep, false);
            break;
        case DISPLAY_STATE_QUEUED:
            if (manager.screen_queued) screen_queue_set_visible(manager.screen_queued, false);
            break;
        case DISPLAY_STATE_SETTINGS:
            if (manager.screen_settings) screen_settings_set_visible(manager.screen_settings, false);
            break;
        default:
            break;
    }
    
    // NOW delete old transient screen (after it's hidden)
    // Keep persistent screens: IDLE (galaxy always animating), RECORDING, SLEEP, QUEUE (frequently used)
    // Delete transient screens: BOOT, UPLOADING, DOWNLOADING, PLAYING, SETTINGS, etc.
    switch (old_state) {
        case DISPLAY_STATE_BOOT:
            Serial.println("🧹 Deleting BOOT screen (boot complete)...");
            delete_screen(&manager.screen_boot);
            break;
        // IDLE screen is now persistent - galaxy animation runs in background always
        // case DISPLAY_STATE_IDLE:
        //     Serial.println("🧹 Deleting IDLE screen (frees galaxy animation)...");
        //     delete_screen(&manager.screen_idle);
        //     break;
        case DISPLAY_STATE_UPLOADING:
            Serial.println("🧹 Deleting old UPLOADING screen...");
            delete_screen(&manager.screen_uploading);
            break;
        case DISPLAY_STATE_DOWNLOADING:
            Serial.println("🧹 Deleting old DOWNLOADING screen...");
            delete_screen(&manager.screen_downloading);
            break;
        case DISPLAY_STATE_PLAYING:
        case DISPLAY_STATE_PLAYING_ASK:
        case DISPLAY_STATE_PLAYING_CONFIRM:
            Serial.println("🧹 Deleting old PLAYING screen...");
            delete_screen(&manager.screen_playing);
            break;
        case DISPLAY_STATE_PROCESSING:
            Serial.println("🧹 Deleting old PROCESSING screen...");
            delete_screen(&manager.screen_processing);
            break;
        case DISPLAY_STATE_CONFIRMATION:
            Serial.println("🧹 Deleting old CONFIRMATION screen...");
            delete_screen(&manager.screen_confirmation);
            break;
        case DISPLAY_STATE_ERROR:
            Serial.println("🧹 Deleting old ERROR screen...");
            delete_screen(&manager.screen_error);
            break;
        case DISPLAY_STATE_SETTINGS:
            Serial.println("🧹 Deleting old SETTINGS screen...");
            delete_screen(&manager.screen_settings);
            break;
        default:
            // IDLE, RECORDING, SLEEP, QUEUE are persistent - keep them
            break;
    }
    
    // Let LVGL process deletions
    if (old_state != manager.current_state) {
        lv_timer_handler();
        log_memory("after_cleanup");
    }
    
    manager.current_state = state;
}

/**
 * @brief Get current state (public API)
 */
display_state_t display_manager_get_state(void) {
    return manager.current_state;
}

/**
 * @brief Get playing screen object (public API)
 */
lv_obj_t* display_manager_get_playing_screen(void) {
    return manager.screen_playing;
}

/**
 * @brief Get queue screen object (public API)
 */
lv_obj_t* display_manager_get_queue_screen(void) {
    return manager.screen_queued;
}

/**
 * @brief Update status bar (public API)
 */
void display_manager_update_status(const idle_status_t *status) {
    if (!status) return;
    
    // Save status
    manager.status = *status;
    
    // Update visible screen
    if (manager.screen_idle) {
        screen_idle_update_status(manager.screen_idle, &manager.status);
    }
    if (manager.screen_recording) {
        screen_recording_update_status(manager.screen_recording, &manager.status);
    }
    if (manager.screen_uploading) {
        screen_uploading_update_status(manager.screen_uploading, &manager.status);
    }
    if (manager.screen_downloading) {
        screen_downloading_update_status(manager.screen_downloading, &manager.status);
    }
    if (manager.screen_playing) {
        screen_playing_update_status(manager.screen_playing, &manager.status);
    }
    if (manager.screen_queued) {
        screen_queue_update_status(manager.screen_queued, &manager.status);
    }
}

/**
 * @brief Update screen data (public API)
 */
void display_manager_update_data(void *data) {
    if (!data) return;
    
    switch (manager.current_state) {
        case DISPLAY_STATE_BOOT:
            // Update boot loading progress
            if (manager.screen_boot) {
                int progress = *(int*)data;
                screen_boot_update_progress(manager.screen_boot, progress);
            }
            break;
            
        case DISPLAY_STATE_SLEEP:
            // Update sleep screen clock
            // data format: char[2][32] - time_str, date_str
            if (manager.screen_sleep) {
                char (*strs)[32] = (char (*)[32])data;
                screen_sleep_update_time(manager.screen_sleep, strs[0], strs[1]);
            }
            break;
            
        case DISPLAY_STATE_RECORDING:
            // Update recording duration
            // data format: int - seconds (0-60)
            if (manager.screen_recording) {
                int seconds = *(int*)data;
                screen_recording_update_duration(manager.screen_recording, seconds);
                
                // Reset watchdog during recording
                esp_task_wdt_reset();
            }
            break;
            
        case DISPLAY_STATE_UPLOADING:
            // Update upload progress
            // data format: int[2] - sent_kb, total_kb
            if (manager.screen_uploading) {
                int *kb_data = (int*)data;
                screen_uploading_update_progress(manager.screen_uploading, kb_data[0], kb_data[1]);
            }
            break;
            
        case DISPLAY_STATE_DOWNLOADING:
            // Update download progress
            // data format: int[2] - received_kb, total_kb
            if (manager.screen_downloading) {
                int *kb_data = (int*)data;
                screen_downloading_update_progress(manager.screen_downloading, kb_data[0], kb_data[1]);
            }
            break;
            
        case DISPLAY_STATE_PLAYING:
        case DISPLAY_STATE_PLAYING_ASK:
        case DISPLAY_STATE_PLAYING_CONFIRM:
            // Update playback time
            // data format: int[2] - current_sec, total_sec
            if (manager.screen_playing) {
                int *time_data = (int*)data;
                screen_playing_update_time(manager.screen_playing, time_data[0], time_data[1]);
            }
            break;
            
        case DISPLAY_STATE_SETTINGS:
            // Set initial settings values
            // data format: int[3] - brightness, volume, mic_gain
            if (manager.screen_settings) {
                int *values = (int*)data;
                screen_settings_set_values(manager.screen_settings, values[0], values[1], values[2]);
            }
            break;
            
        default:
            // Other screens: TODO
            break;
    }
}

/**
 * @brief Helper: Update sleep screen clock (public API)
 */
void display_manager_update_sleep_time(const char* time_str, const char* date_str) {
    if (manager.screen_sleep && manager.current_state == DISPLAY_STATE_SLEEP) {
        screen_sleep_update_time(manager.screen_sleep, time_str, date_str);
    }
}

/**
 * @brief Helper: Set initial settings values (public API)
 */
void display_manager_set_settings_values(int brightness, int volume, int mic_gain) {
    if (manager.screen_settings && manager.current_state == DISPLAY_STATE_SETTINGS) {
        screen_settings_set_values(manager.screen_settings, brightness, volume, mic_gain);
    }
}

/**
 * @brief Helper: Update queue screen filenames list (public API)
 */
void display_manager_update_queue_filenames(const char* filenames) {
    if (manager.screen_queued && manager.current_state == DISPLAY_STATE_QUEUED) {
        screen_queue_update_filenames(manager.screen_queued, filenames);
    }
}

/**
 * @brief Helper: Update queue screen WiFi state (public API)
 */
void display_manager_update_queue_wifi(bool wifi_connected) {
    if (manager.screen_queued && manager.current_state == DISPLAY_STATE_QUEUED) {
        screen_queue_update_wifi(manager.screen_queued, wifi_connected);
    }
}
