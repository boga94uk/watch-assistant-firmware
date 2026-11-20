/**
 * @file display_screens.h
 * @brief All LVGL screen declarations
 * 
 * Single header for all screens to minimize file count.
 * Includes: IDLE, RECORDING, PLAYING, SETTINGS, SLEEP, etc.
 */

#ifndef DISPLAY_SCREENS_H
#define DISPLAY_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
#include <Arduino.h>  // For String type in C++ functions
extern "C" {
#endif

/**
 * @brief Callback events from IDLE screen
 */
typedef enum {
    IDLE_EVENT_NONE = 0,
    IDLE_EVENT_MIC_CLICKED,    // User clicked mic button
    IDLE_EVENT_POWER_CLICKED   // User clicked power button
} idle_event_t;

/**
 * @brief Status bar data
 */
typedef struct {
    int battery_percent;   // 0-100
    bool battery_charging;
    int wifi_bars;         // 0-4 (0=no signal, 4=full)
    bool wifi_connected;
} idle_status_t;

/**
 * @brief Callback function type for IDLE events
 */
typedef void (*idle_event_cb_t)(idle_event_t event);

/**
 * @brief Create IDLE screen
 * 
 * @param parent Parent object (usually NULL for full screen)
 * @param event_cb Callback for button events (can be NULL)
 * @return lv_obj_t* IDLE screen object
 */
lv_obj_t* screen_idle_create(lv_obj_t *parent, idle_event_cb_t event_cb);

/**
 * @brief Update status bar
 * 
 * @param screen IDLE screen object (from screen_idle_create)
 * @param status Status data
 */
void screen_idle_update_status(lv_obj_t *screen, const idle_status_t *status);

/**
 * @brief Show/hide IDLE screen
 * 
 * @param screen IDLE screen object
 * @param visible true to show, false to hide
 */
void screen_idle_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// RECORDING SCREEN
// ============================================

/**
 * @brief Callback events from RECORDING screen
 */
typedef enum {
    RECORDING_EVENT_NONE = 0,
    RECORDING_EVENT_STOP_CLICKED,   // User clicked stop/cancel button
    RECORDING_EVENT_POWER_CLICKED   // User clicked power button
} recording_event_t;

/**
 * @brief Callback function type for RECORDING events
 */
typedef void (*recording_event_cb_t)(recording_event_t event);

/**
 * @brief Create RECORDING screen
 * 
 * @param parent Parent object (usually NULL for full screen)
 * @param event_cb Callback for button events (can be NULL)
 * @return lv_obj_t* RECORDING screen object
 */
lv_obj_t* screen_recording_create(lv_obj_t *parent, recording_event_cb_t event_cb);

/**
 * @brief Update status bar
 * 
 * @param screen RECORDING screen object
 * @param status Status data (same as IDLE)
 */
void screen_recording_update_status(lv_obj_t *screen, const idle_status_t *status);

/**
 * @brief Update recording duration
 * 
 * @param screen RECORDING screen object
 * @param seconds Current recording duration in seconds (0-60)
 */
void screen_recording_update_duration(lv_obj_t *screen, int seconds);

/**
 * @brief Show/hide RECORDING screen
 * 
 * @param screen RECORDING screen object
 * @param visible true to show, false to hide
 */
void screen_recording_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// UPLOADING SCREEN
// ============================================

/**
 * @brief Callback events from UPLOADING screen
 */
typedef enum {
    UPLOADING_EVENT_NONE = 0,
    UPLOADING_EVENT_CANCEL_CLICKED,   // User clicked cancel button
    UPLOADING_EVENT_POWER_CLICKED     // User clicked power button
} uploading_event_t;

/**
 * @brief Callback function type for UPLOADING events
 */
typedef void (*uploading_event_cb_t)(uploading_event_t event);

/**
 * @brief Create UPLOADING screen
 * 
 * @param parent Parent object (usually NULL for full screen)
 * @param event_cb Callback for button events (can be NULL)
 * @return lv_obj_t* UPLOADING screen object
 */
lv_obj_t* screen_uploading_create(lv_obj_t *parent, uploading_event_cb_t event_cb);

/**
 * @brief Update status bar
 * 
 * @param screen UPLOADING screen object
 * @param status Status data (same as IDLE)
 */
void screen_uploading_update_status(lv_obj_t *screen, const idle_status_t *status);

/**
 * @brief Update upload progress
 * 
 * @param screen UPLOADING screen object
 * @param sent_kb Kilobytes sent
 * @param total_kb Total kilobytes to send
 */
void screen_uploading_update_progress(lv_obj_t *screen, int sent_kb, int total_kb);

/**
 * @brief Show/hide UPLOADING screen
 * 
 * @param screen UPLOADING screen object
 * @param visible true to show, false to hide
 */
void screen_uploading_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// DOWNLOADING SCREEN
// ============================================

/**
 * @brief Create DOWNLOADING screen (reuses uploading_event_cb_t)
 * 
 * @param parent Parent object (usually NULL for full screen)
 * @param event_cb Callback for button events (can be NULL)
 * @return lv_obj_t* DOWNLOADING screen object
 */
lv_obj_t* screen_downloading_create(lv_obj_t *parent, uploading_event_cb_t event_cb);

/**
 * @brief Update status bar
 * 
 * @param screen DOWNLOADING screen object
 * @param status Status data (same as IDLE)
 */
void screen_downloading_update_status(lv_obj_t *screen, const idle_status_t *status);

/**
 * @brief Update download progress
 * 
 * @param screen DOWNLOADING screen object
 * @param received_kb Kilobytes received
 * @param total_kb Total kilobytes to receive
 */
void screen_downloading_update_progress(lv_obj_t *screen, int received_kb, int total_kb);

/**
 * @brief Show/hide DOWNLOADING screen
 * 
 * @param screen DOWNLOADING screen object
 * @param visible true to show, false to hide
 */
void screen_downloading_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// PLAYING SCREEN
// ============================================

/**
 * @brief Create PLAYING screen (reuses uploading_event_cb_t)
 * 
 * @param parent Parent object (usually NULL for full screen)
 * @param event_cb Callback for button events (can be NULL)
 * @return lv_obj_t* PLAYING screen object
 */
lv_obj_t* screen_playing_create(lv_obj_t *parent, uploading_event_cb_t event_cb);

/**
 * @brief Update status bar
 * 
 * @param screen PLAYING screen object
 * @param status Status data (same as IDLE)
 */
void screen_playing_update_status(lv_obj_t *screen, const idle_status_t *status);

/**
 * @brief Update playback time
 * 
 * @param screen PLAYING screen object
 * @param current_sec Current playback position in seconds
 * @param total_sec Total audio duration in seconds
 */
void screen_playing_update_time(lv_obj_t *screen, int current_sec, int total_sec);

/**
 * @brief Update text box with server response
 * 
 * @param screen PLAYING screen object
 * @param text Server response text (text being read back)
 */
void screen_playing_update_text(lv_obj_t *screen, const char* text);

/**
 * @brief Set PLAYING screen mode (changes colors and icons)
 * 
 * @param screen PLAYING screen object
 * @param mode 0=SAY (green, no icons), 1=ASK (yellow, mic only), 2=CONFIRM (purple, mic+checkmark)
 */
void screen_playing_set_mode(lv_obj_t *screen, int mode);

/**
 * @brief Show/hide PLAYING screen
 * 
 * @param screen PLAYING screen object
 * @param visible true to show, false to hide
 */
void screen_playing_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// BOOT SCREEN
// ============================================

/**
 * @brief Create BOOT screen with loading bar
 * 
 * @param parent Parent object (usually NULL for full screen)
 * @return lv_obj_t* BOOT screen object
 */
lv_obj_t* screen_boot_create(lv_obj_t *parent);

/**
 * @brief Update boot screen loading progress
 * 
 * @param screen BOOT screen object
 * @param progress Progress value 0-100
 */
void screen_boot_update_progress(lv_obj_t *screen, int progress);

/**
 * @brief Show/hide BOOT screen
 * 
 * @param screen BOOT screen object
 * @param visible true to show, false to hide
 */
void screen_boot_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// TEST MODE SCREENS
// ============================================

/**
 * @brief Test mode screen states
 */
typedef enum {
    TEST_MODE_WAIT = 0,     // Waiting for WiFi connection
    TEST_MODE_LOCKED,       // Connected but locked
    TEST_MODE_UNLOCKED      // Unlocked (rotating gear)
} test_mode_state_t;

/**
 * @brief Create test mode screen
 * @param parent Parent object (usually NULL for full screen)
 * @return lv_obj_t* Test mode screen object
 */
lv_obj_t* screen_testmode_create(lv_obj_t *parent);

/**
 * @brief Update test mode screen state
 * @param screen Test mode screen object
 * @param state Current state (WAIT/LOCKED/UNLOCKED)
 */
void screen_testmode_update_state(lv_obj_t *screen, test_mode_state_t state);

/**
 * @brief Show/hide test mode screen
 * @param screen Test mode screen object
 * @param visible true to show, false to hide
 */
void screen_testmode_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// SLEEP SCREEN
// ============================================

/**
 * @brief Create sleep screen with clock display
 * @param parent Parent object
 * @return lv_obj_t* Sleep screen object
 */
lv_obj_t* screen_sleep_create(lv_obj_t *parent);

/**
 * @brief Update sleep screen clock display
 * @param screen Sleep screen object
 * @param time_str Time string (HH:MM or HH:MM:SS)
 * @param date_str Date string (optional, can be NULL)
 */
void screen_sleep_update_time(lv_obj_t *screen, const char* time_str, const char* date_str);

/**
 * @brief Show/hide sleep screen
 * @param screen Sleep screen object
 * @param visible true to show, false to hide
 */
void screen_sleep_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// SETTINGS SCREEN
// ============================================

/**
 * @brief Settings event types
 */
typedef enum {
    SETTINGS_EVENT_CLOSE = 0,       // User closed settings
    SETTINGS_EVENT_BRIGHTNESS,      // Brightness changed
    SETTINGS_EVENT_VOLUME,          // Volume changed
    SETTINGS_EVENT_MIC_GAIN         // Mic gain changed
} settings_event_t;

/**
 * @brief Settings event data
 */
typedef struct {
    settings_event_t event;
    int value;  // New value (0-100 for brightness/volume, 0-7 for mic gain)
} settings_event_data_t;

/**
 * @brief Callback function type for settings events
 */
typedef void (*settings_event_cb_t)(settings_event_data_t event_data);

/**
 * @brief Create settings screen with sliders
 * @param parent Parent object
 * @param event_cb Callback for slider changes (can be NULL)
 * @return lv_obj_t* Settings screen object
 */
lv_obj_t* screen_settings_create(lv_obj_t *parent, settings_event_cb_t event_cb);

/**
 * @brief Set initial values for settings sliders
 * @param screen Settings screen object
 * @param brightness Initial brightness (0-100)
 * @param volume Initial volume (0-100)
 * @param mic_gain Initial mic gain (0-7)
 */
void screen_settings_set_values(lv_obj_t *screen, int brightness, int volume, int mic_gain);

/**
 * @brief Show/hide settings screen
 * @param screen Settings screen object
 * @param visible true to show, false to hide
 */
void screen_settings_set_visible(lv_obj_t *screen, bool visible);

// ============================================
// QUEUE SCREEN
// ============================================

/**
 * @brief Queue event types
 */
typedef enum {
    QUEUE_EVENT_PROCESS = 0,    // Process oldest recording (upload)
    QUEUE_EVENT_DELETE_ALL,     // Delete all queued recordings
    QUEUE_EVENT_PLAY,           // Play selected recording
    QUEUE_EVENT_STOP            // Stop playback
} queue_event_t;

/**
 * @brief Callback function type for queue events
 */
typedef void (*queue_event_cb_t)(queue_event_t event);

/**
 * @brief Create queue screen (brown themed)
 * @param parent Parent object
 * @param event_cb Callback for button events (can be NULL)
 * @return lv_obj_t* Queue screen object
 */
lv_obj_t* screen_queue_create(lv_obj_t *parent, queue_event_cb_t event_cb);

/**
 * @brief Update status bar
 * @param screen Queue screen object
 * @param status Status data (battery, WiFi, etc.)
 */
void screen_queue_update_status(lv_obj_t *screen, const idle_status_t *status);

/**
 * @brief Update queue filenames list in text box
 * @param screen Queue screen object
 * @param filenames Newline-separated list of filenames
 */
void screen_queue_update_filenames(lv_obj_t *screen, const char* filenames);

/**
 * @brief Update WiFi connection state (shows/hides process button)
 * @param screen Queue screen object
 * @param wifi_connected true if WiFi is connected
 */
void screen_queue_update_wifi(lv_obj_t *screen, bool wifi_connected);

/**
 * @brief Update playback progress bar
 * @param screen Queue screen object
 * @param percent Progress percentage (0-100)
 */
void screen_queue_update_progress(lv_obj_t *screen, int percent);

/**
 * @brief Show/hide playback progress bar
 * @param screen Queue screen object
 * @param visible true to show, false to hide
 */
void screen_queue_show_progress(lv_obj_t *screen, bool visible);

/**
 * @brief Reset play button to play state (not stop)
 * @param screen Queue screen object
 */
void screen_queue_reset_play_button(lv_obj_t *screen);

/**
 * @brief Show/hide queue screen
 * @param screen Queue screen object
 * @param visible true to show, false to hide
 */
void screen_queue_set_visible(lv_obj_t *screen, bool visible);

/**
 * @brief Get selected index
 * @param screen Queue screen object
 * @return Selected index (0="All", 1+=recordings)
 */
int screen_queue_get_selected_index(lv_obj_t *screen);

/**
 * @brief Set selected index programmatically (for sequential playback)
 * @param screen Queue screen object
 * @param index Index to select (0="Select All", 1+=recordings)
 */
void screen_queue_set_selected_index(lv_obj_t *screen, int index);

/**
 * @brief Get selected filename
 * @param screen Queue screen object
 * @return Filename of selected recording, or empty if "All" selected
 */
String screen_queue_get_selected_filename(lv_obj_t *screen);

// ============================================
// PLACEHOLDER SCREENS (Simple text labels)
// ============================================

/**
 * @brief Create placeholder screen (simple centered text)
 * @param parent Parent object
 * @param title Screen title text
 * @param subtitle Optional subtitle text (can be NULL)
 * @return lv_obj_t* Screen object
 */
lv_obj_t* screen_placeholder_create(lv_obj_t *parent, const char* title, const char* subtitle);

/**
 * @brief Update placeholder screen text
 * @param screen Screen object
 * @param title New title text
 * @param subtitle New subtitle text (can be NULL)
 */
void screen_placeholder_update(lv_obj_t *screen, const char* title, const char* subtitle);

/**
 * @brief Show/hide placeholder screen
 * @param screen Screen object
 * @param visible true to show, false to hide
 */
void screen_placeholder_set_visible(lv_obj_t *screen, bool visible);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_SCREENS_H
