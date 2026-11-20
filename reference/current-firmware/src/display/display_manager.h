/**
 * @file display_manager.h
 * @brief Display manager - coordinates all LVGL screens
 * 
 * Single interface for managing all screens:
 * - IDLE (galaxy animation)
 * - RECORDING (progress bars)
 * - UPLOADING/PROCESSING/DOWNLOADING (spinner)
 * - PLAYING (VU meter)
 * - SETTINGS (sliders)
 * - SLEEP (clock)
 * - etc.
 */

#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <lvgl.h>
#include "display_screens.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display states (screens)
 */
typedef enum {
    DISPLAY_STATE_IDLE = 0,
    DISPLAY_STATE_RECORDING,
    DISPLAY_STATE_UPLOADING,
    DISPLAY_STATE_PROCESSING,
    DISPLAY_STATE_DOWNLOADING,
    DISPLAY_STATE_PLAYING,         // SAY response - blue VU meter
    DISPLAY_STATE_PLAYING_ASK,     // ASK response - green VU meter (wait for audio)
    DISPLAY_STATE_PLAYING_CONFIRM, // CONFIRM response - yellow VU meter (wait for button)
    DISPLAY_STATE_SETTINGS,
    DISPLAY_STATE_SLEEP,
    DISPLAY_STATE_BOOT,
    DISPLAY_STATE_ERROR,
    DISPLAY_STATE_CONFIRMATION,
    DISPLAY_STATE_QUEUED,
    DISPLAY_STATE_TEST_CONFIG
} display_state_t;

/**
 * @brief Display event callback
 */
typedef void (*display_event_cb_t)(display_state_t from_state, int event_data);

/**
 * @brief Initialize display manager
 * 
 * @param event_cb Event callback for button clicks (can be NULL)
 * @return true if successful
 */
bool display_manager_init(display_event_cb_t event_cb);

/**
 * @brief Switch to a different screen
 * 
 * @param state Target state/screen
 */
void display_manager_set_state(display_state_t state);

/**
 * @brief Get current screen state
 * 
 * @return display_state_t Current state
 */
display_state_t display_manager_get_state(void);

/**
 * @brief Get playing screen object for updating text
 * @return lv_obj_t* Playing screen object or NULL if not created
 */
lv_obj_t* display_manager_get_playing_screen(void);

/**
 * @brief Get queue screen object for updating progress bar
 * @return lv_obj_t* Queue screen object or NULL if not created
 */
lv_obj_t* display_manager_get_queue_screen(void);

/**
 * @brief Update status bar (battery + WiFi)
 * 
 * @param status Status data
 */
void display_manager_update_status(const idle_status_t *status);

/**
 * @brief Update screen-specific data
 * 
 * Example: Recording progress, volume level, etc.
 * 
 * @param data Pointer to data (type depends on current screen)
 */
void display_manager_update_data(void *data);

/**
 * @brief Update sleep screen clock display
 * 
 * @param time_str Time string (e.g. "14:30" or "14:30:25")
 * @param date_str Date string (e.g. "2025-10-20", can be NULL)
 */
void display_manager_update_sleep_time(const char* time_str, const char* date_str);

/**
 * @brief Load and set initial settings values
 * 
 * Call when entering settings screen to load current values from NVS
 * 
 * @param brightness Display brightness 0-100
 * @param volume Audio volume 0-100
 * @param mic_gain Microphone gain 0-7
 */
void display_manager_set_settings_values(int brightness, int volume, int mic_gain);

/**
 * @brief Helper: Update queue screen filenames list (public API)
 */
void display_manager_update_queue_filenames(const char* filenames);

/**
 * @brief Helper: Update queue screen WiFi state (public API)
 */
void display_manager_update_queue_wifi(bool wifi_connected);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_MANAGER_H
