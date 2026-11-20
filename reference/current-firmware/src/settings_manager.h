#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

// ============================================
// SETTINGS MANAGER
// ============================================
// Manages user settings (brightness, volume, mic gain)
// - Load/save from NVS
// - Apply settings to hardware
// - Handle settings screen events

/**
 * @brief Initialize settings manager
 * @param gfx_ptr Pointer to display object for brightness control
 */
void settings_manager_init(Arduino_GFX *gfx_ptr);

/**
 * @brief Load settings from NVS
 * @return true if loaded successfully, false if using defaults
 */
bool settings_manager_load_from_nvs();

/**
 * @brief Handle settings screen event (from display callback)
 * @param event_data Encoded event (type*100 + value)
 * @return true if event was handled
 */
bool settings_manager_handle_event(int event_data);

/**
 * @brief Get current brightness value
 * @return Brightness (0-100)
 */
int settings_manager_get_brightness();

/**
 * @brief Get current volume value
 * @return Volume (0-100)
 */
int settings_manager_get_volume();

/**
 * @brief Get current mic gain value
 * @return Mic gain (0-7)
 */
int settings_manager_get_mic_gain();

/**
 * @brief Set brightness and apply to hardware
 * @param brightness Brightness value (0-100)
 * @param save_to_nvs Save to NVS if true
 */
void settings_manager_set_brightness(int brightness, bool save_to_nvs = true);

/**
 * @brief Set volume and apply to hardware
 * @param volume Volume value (0-100)
 * @param save_to_nvs Save to NVS if true
 */
void settings_manager_set_volume(int volume, bool save_to_nvs = true);

/**
 * @brief Set mic gain and apply to hardware
 * @param mic_gain Mic gain value (0-7)
 * @param save_to_nvs Save to NVS if true
 */
void settings_manager_set_mic_gain(int mic_gain, bool save_to_nvs = true);

/**
 * @brief Get device secret (for HTTPS auth)
 * @return Device secret string (hex)
 */
String settings_manager_get_device_secret();

/**
 * @brief Set device secret and save to NVS
 * @param secret Device secret string (hex, 64 chars)
 * @param save_to_nvs Save to NVS if true
 */
void settings_manager_set_device_secret(const char* secret, bool save_to_nvs = true);

/**
 * @brief Get timezone offset in hours
 * @return Timezone offset (-12 to +14)
 */
int settings_manager_get_timezone();

/**
 * @brief Set timezone offset
 * @param timezone_hours Timezone offset in hours (-12 to +14)
 * @param save_to_nvs Save to NVS if true
 */
void settings_manager_set_timezone(int timezone_hours, bool save_to_nvs = true);

#endif // SETTINGS_MANAGER_H
