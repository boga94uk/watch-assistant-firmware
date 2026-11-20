#ifndef SLEEP_MANAGER_H
#define SLEEP_MANAGER_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "button_manager.h"
#include "state_machine.h"

// ============================================
// SLEEP MANAGER
// ============================================
// Handles sleep mode, wake detection, and clock updates
// - Auto-sleep after inactivity timeout
// - Shake-to-wake detection (IMU)
// - Button wake detection
// - Sleep screen clock updates
// - Brightness control

/**
 * @brief Initialize sleep manager
 * @param gfx_ptr Pointer to display object for brightness control
 * @param initial_brightness Initial brightness value (0-100)
 */
void sleep_manager_init(Arduino_GFX *gfx_ptr, int initial_brightness);

/**
 * @brief Update sleep manager (call from main loop)
 * Handles inactivity timeout, shake detection, clock updates
 */
void sleep_manager_update();

/**
 * @brief Check if device is currently sleeping
 * @return true if in sleep mode
 */
bool sleep_manager_is_sleeping();

/**
 * @brief Handle button press during sleep
 * @param button Which button was pressed
 * @param event Button event type
 * @return true if button was handled (device woke up)
 */
bool sleep_manager_handle_button(button_id_t button, button_event_t event);

/**
 * @brief Enter sleep mode manually
 * Called from power button hold or touch event
 */
void sleep_manager_enter_sleep();

/**
 * @brief Update saved brightness value
 * @param brightness New brightness value (0-100)
 */
void sleep_manager_set_brightness(int brightness);

/**
 * @brief Get current saved brightness
 * @return Current brightness value (0-100)
 */
int sleep_manager_get_brightness();

#endif // SLEEP_MANAGER_H
