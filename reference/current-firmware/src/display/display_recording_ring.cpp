/**
 * @file display_recording_ring.cpp
 * @brief Recording ring animation - DIRECT PIXEL UPDATES for RECORDING screen
 * 
 * Bypasses LVGL for star rendering - writes pixels directly to display.
 * Stars orbit within a 50px wide ring around the mic icon and respond to audio.
 * 
 * Features:
 * - Stars confined to ring around mic icon
 * - Audio-reactive orbit expansion
 * - Direct pixel rendering for blazing performance
 * - Automatically destroyed when leaving RECORDING state
 */

#include "display/display_recording_ring.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

// Configuration
#ifndef RING_STAR_COUNT
#define RING_STAR_COUNT 200  // Stars in the ring (visualizer mode)
#endif

// Star structure - orbits within ring
typedef struct {
    // Orbit parameters
    float angle;          // Current angle on orbit (radians)
    float speed;          // Angular velocity (radians/frame)
    float base_radius;    // Base distance from center (within ring bounds)
    
    // Visual properties
    uint16_t base_color;  // Star base color
    float brightness;     // Brightness phase (0-2π for pulsing)
    float brightness_speed; // How fast brightness oscillates
    
    // Position tracking for direct pixel updates
    int16_t x, y;         // Current position
    int16_t prev_x, prev_y;  // Previous position (for erasing)
    bool visible;         // Is this star currently visible?
} ring_star_t;

// Recording ring state
typedef struct {
    ring_star_t stars[RING_STAR_COUNT];  // Stars array
    int cx, cy;           // Center coordinates (mic icon center)
    int inner_r;          // Inner radius of ring
    int outer_r;          // Outer radius of ring
    float audio_level;    // Current audio level (0.0-1.0)
    lv_timer_t *timer;    // Animation timer
} recording_ring_t;

// Global ring instance (needed for direct updates)
static recording_ring_t *g_ring = nullptr;

// UI exclusion zone checker (copied from galaxy)
static bool is_in_ui_zone(int x, int y, int screen_w, int screen_h) {
    // Top status bar: y < 30
    if (y < 30) return true;
    
    // Bottom button area: y > screen_h - 80
    if (y > screen_h - 80) return true;
    
    return false;
}

// Apply brightness to color (copied from galaxy)
static uint16_t apply_brightness(uint16_t color, float brightness_phase) {
    float brightness = 0.5f + 0.5f * sinf(brightness_phase);
    brightness = 0.3f + brightness * 0.7f;  // Range: 0.3 to 1.0
    
    uint8_t r = ((color >> 11) & 0x1F);
    uint8_t g = ((color >> 5) & 0x3F);
    uint8_t b = (color & 0x1F);
    
    r = (uint8_t)(r * brightness);
    g = (uint8_t)(g * brightness);
    b = (uint8_t)(b * brightness);
    
    return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
}

/**
 * @brief Update star positions (physics simulation)
 */
static void ring_update_positions(recording_ring_t *r) {
    if (!r) return;
    
    // Audio-reactive expansion: 0.0 = base radius, 1.0 = expand by 200px (aggressive visualizer)
    float expansion = r->audio_level * 200.0f;
    
    for (int i = 0; i < RING_STAR_COUNT; i++) {
        ring_star_t *s = &r->stars[i];
        
        // Save previous position
        s->prev_x = s->x;
        s->prev_y = s->y;
        
        // Update angle
        s->angle += s->speed;
        if (s->angle > 6.28318f) s->angle -= 6.28318f;  // Wrap at 2π
        
        // Calculate current radius (base + audio expansion)
        float current_radius = s->base_radius + expansion;
        
        // Calculate position on circular orbit
        s->x = r->cx + (int16_t)(current_radius * cosf(s->angle));
        s->y = r->cy + (int16_t)(current_radius * sinf(s->angle));
        
        // Update brightness phase
        s->brightness += s->brightness_speed;
        if (s->brightness > 6.28318f) s->brightness -= 6.28318f;  // Wrap at 2π
        
        // Mark as visible if within screen bounds
        s->visible = (s->x >= 0 && s->x < 368 && s->y >= 0 && s->y < 448);
    }
}

/**
 * @brief Render stars via direct pixel updates
 */
void recording_ring_render_direct(Arduino_GFX *gfx) {
    if (!g_ring || !gfx) return;
    
    gfx->startWrite();
    
    // Render all stars
    for (int i = 0; i < RING_STAR_COUNT; i++) {
        ring_star_t *s = &g_ring->stars[i];
        
        if (!s->visible) continue;
        
        // Erase old pixel (if different from new)
        if ((s->prev_x != s->x || s->prev_y != s->y) &&
            s->prev_x >= 0 && s->prev_x < 368 && 
            s->prev_y >= 0 && s->prev_y < 448) {
            if (!is_in_ui_zone(s->prev_x, s->prev_y, 368, 448)) {
                gfx->writePixel(s->prev_x, s->prev_y, 0x0000);
            }
        }
        
        // Draw new pixel
        if (s->x >= 0 && s->x < 368 && s->y >= 0 && s->y < 448) {
            if (!is_in_ui_zone(s->x, s->y, 368, 448)) {
                uint16_t color = (s->brightness_speed > 0)
                    ? apply_brightness(s->base_color, s->brightness)
                    : s->base_color;
                gfx->writePixel(s->x, s->y, color);
            }
        }
    }
    
    gfx->endWrite();
}

/**
 * @brief Timer callback - update physics only
 */
static void ring_timer_cb(lv_timer_t *t) {
    recording_ring_t *r = (recording_ring_t*)t->user_data;
    if (!r) return;
    
    ring_update_positions(r);
    // Note: Actual rendering happens in recording_ring_render_direct() after LVGL flush
}

/**
 * @brief Cleanup callback when widget is deleted
 */
static void ring_delete_cb(lv_event_t *e) {
    lv_obj_t *container = lv_event_get_target(e);
    recording_ring_t *r = (recording_ring_t*)lv_obj_get_user_data(container);
    
    if (r) {
        // Stop timer
        if (r->timer) {
            lv_timer_del(r->timer);
            r->timer = NULL;
        }
        
        // Clear global pointer
        if (g_ring == r) {
            g_ring = nullptr;
        }
        
        // Free memory
        lv_mem_free(r);
    }
}

/**
 * @brief Create recording ring widget (public API)
 */
lv_obj_t* recording_ring_create(lv_obj_t *parent, int center_x, int center_y, 
                                int inner_radius, int outer_radius, uint32_t fps) {
    // Create invisible container (just for lifecycle management)
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);  // Invisible - we render directly
    lv_obj_set_size(container, 368, 448);  // Full screen size
    
    // Allocate ring state
    recording_ring_t *r = (recording_ring_t*)lv_mem_alloc(sizeof(recording_ring_t));
    LV_ASSERT_MALLOC(r);
    memset(r, 0, sizeof(recording_ring_t));
    
    r->cx = center_x;
    r->cy = center_y;
    r->inner_r = inner_radius;
    r->outer_r = outer_radius;
    r->audio_level = 0.0f;
    
    // Initialize stars with random orbits within ring
    float ring_width = outer_radius - inner_radius;
    
    for (int i = 0; i < RING_STAR_COUNT; i++) {
        ring_star_t *s = &r->stars[i];
        
        // Random position within ring
        float radial_pos = (float)lv_rand(0, 1000) / 1000.0f;  // 0.0 to 1.0
        s->base_radius = inner_radius + (radial_pos * ring_width);
        
        // Random starting angle
        s->angle = (lv_rand(0, 3600)) / 572.96f;
        
        // Random speed: -0.03 to 0.03 rad/frame (mix of CW and CCW)
        int speed_sign = (lv_rand(0, 100) < 50) ? 1 : -1;
        s->speed = speed_sign * (0.005f + (lv_rand(0, 250)) / 10000.0f);
        
        // Color variation: mostly white, some yellowish, some blueish
        int color_choice = lv_rand(0, 100);
        if (color_choice < 60) {
            s->base_color = 0xFFFF;  // White
        } else if (color_choice < 85) {
            s->base_color = 0xFFE0;  // Yellowish
        } else {
            s->base_color = 0xADFF;  // Blueish
        }
        
        // Brightness pulsing: 50% static, 50% pulsing
        if (lv_rand(0, 100) < 50) {
            s->brightness = (lv_rand(0, 628)) / 100.0f;  // Random phase
            s->brightness_speed = 0.02f + (lv_rand(0, 300)) / 10000.0f;  // 0.02-0.05 rad/frame
        } else {
            s->brightness = 0.0f;
            s->brightness_speed = 0.0f;  // Static brightness
        }
        
        // Initialize position
        s->x = center_x + (int16_t)(s->base_radius * cosf(s->angle));
        s->y = center_y + (int16_t)(s->base_radius * sinf(s->angle));
        s->prev_x = s->x;
        s->prev_y = s->y;
        s->visible = true;
    }
    
    // Store state in container
    lv_obj_set_user_data(container, r);
    
    // Register cleanup callback
    lv_obj_add_event_cb(container, ring_delete_cb, LV_EVENT_DELETE, r);
    
    // Create animation timer
    r->timer = lv_timer_create(ring_timer_cb, 1000 / fps, r);
    
    // Set as global instance
    g_ring = r;
    
    return container;
}

/**
 * @brief Set GFX instance for direct rendering (no-op, kept for API compatibility)
 */
void recording_ring_set_gfx(Arduino_GFX *gfx) {
    // No-op: gfx is passed directly to recording_ring_render_direct()
    (void)gfx;
}

/**
 * @brief Set audio amplitude for reactive orbit expansion
 */
void recording_ring_set_audio(float audio_level) {
    if (!g_ring) return;
    
    // Clamp and smooth the amplitude
    audio_level = LV_CLAMP(0.0f, audio_level, 1.0f);
    
    // Smooth transition (exponential moving average) - fast response for visualizer
    const float smoothing = 0.7f;  // Higher = faster response (visualizer needs responsiveness)
    g_ring->audio_level = g_ring->audio_level * (1.0f - smoothing) + audio_level * smoothing;
}
