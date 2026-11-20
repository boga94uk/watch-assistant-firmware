/**
 * @file display_galaxy.h
 * @brief Galaxy animation widget for LVGL v8.x
 * 
 * Creates a mesmerizing rotating galaxy effect with depth and perspective.
 * Dedicated to IDLE screen only - direct pixel rendering with IMU parallax.
 */

#ifndef DISPLAY_GALAXY_H
#define DISPLAY_GALAXY_H

#include <lvgl.h>

// Forward declaration for Arduino_GFX
class Arduino_GFX;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a full-screen galaxy animation widget
 * 
 * @param parent Parent object (usually lv_scr_act())
 * @param w Width in pixels
 * @param h Height in pixels
 * @param fps Target frame rate (recommended: 30-60)
 * @return lv_obj_t* Galaxy widget object
 * 
 * @note Widget automatically cleans up on delete
 */
lv_obj_t* galaxy_create(lv_obj_t *parent, int w, int h, uint32_t fps);


/**
 * @brief Set GFX instance for direct pixel rendering
 * 
 * @param gfx Arduino_GFX display instance
 * @note Must be called before galaxy renders anything
 */
void galaxy_set_gfx(Arduino_GFX *gfx);

/**
 * @brief Render stars via direct pixel updates (bypass LVGL)
 * 
 * @param gfx Arduino_GFX display instance
 * @note Call this AFTER lv_timer_handler() in main loop
 */
void galaxy_render_direct(Arduino_GFX *gfx);


/**
 * @brief Set IMU-based 3D parallax offset
 * 
 * Creates a subtle 3D depth effect where the galaxy shifts based on device tilt,
 * simulating parallax as if looking through a window into deep space.
 * 
 * @param accel_x Accelerometer X reading in g (-1.0 to 1.0, left-right tilt)
 * @param accel_y Accelerometer Y reading in g (-1.0 to 1.0, forward-back tilt)
 * @param accel_z Accelerometer Z reading in g (typically ~1.0 when upright)
 * 
 * @note Call this every frame with fresh IMU data for smooth parallax effect
 */
void galaxy_set_imu_offset(float accel_x, float accel_y, float accel_z);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_GALAXY_H
