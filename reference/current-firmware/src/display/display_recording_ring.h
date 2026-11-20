/**
 * @file display_recording_ring.h
 * @brief Recording ring animation widget for LVGL v8.x
 * 
 * Creates a ring of orbiting stars around the mic icon during recording.
 * Stars react to audio input by expanding their orbits.
 * Dedicated to RECORDING screen only - direct pixel rendering.
 */

#ifndef DISPLAY_RECORDING_RING_H
#define DISPLAY_RECORDING_RING_H

#include <lvgl.h>

// Forward declaration for Arduino_GFX
class Arduino_GFX;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create recording ring animation widget
 * 
 * @param parent Parent object (usually the recording screen)
 * @param center_x Center X coordinate (mic icon center)
 * @param center_y Center Y coordinate (mic icon center)
 * @param inner_radius Inner radius of ring (pixels)
 * @param outer_radius Outer radius of ring (pixels)
 * @param fps Target frame rate (recommended: 30-60)
 * @return lv_obj_t* Recording ring widget object
 * 
 * @note Widget automatically cleans up on delete
 */
lv_obj_t* recording_ring_create(lv_obj_t *parent, int center_x, int center_y, 
                                int inner_radius, int outer_radius, uint32_t fps);

/**
 * @brief Set GFX instance for direct pixel rendering
 * 
 * @param gfx Arduino_GFX display instance
 * @note Must be called before ring renders anything
 */
void recording_ring_set_gfx(Arduino_GFX *gfx);

/**
 * @brief Render ring stars via direct pixel updates (bypass LVGL)
 * 
 * @param gfx Arduino_GFX display instance
 * @note Call this AFTER lv_timer_handler() in main loop
 */
void recording_ring_render_direct(Arduino_GFX *gfx);

/**
 * @brief Set audio amplitude for reactive orbit expansion
 * 
 * Stars expand their orbits based on audio input level.
 * 
 * @param audio_level Audio input level (0.0 = silence, 1.0 = max)
 * 
 * @note Call this every frame with fresh audio data for smooth reactivity
 */
void recording_ring_set_audio(float audio_level);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_RECORDING_RING_H
