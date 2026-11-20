/**
 * @file display_galaxy.cpp
 * @brief 3D Galaxy animation - DIRECT PIXEL UPDATES for IDLE screen
 * 
 * Bypasses LVGL for star rendering - writes pixels directly to display.
 * Only updates changed pixels (erase old + draw new) = ~400 pixels/frame instead of 330KB!
 * 
 * Features:
 * - 500 stars in 3D elliptical orbits
 * - IMU-based parallax effect (tilting device shifts galaxy)
 * - Direct pixel rendering for blazing performance
 * - Automatically destroyed when leaving IDLE state
 */

#include "display/display_galaxy.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <math.h>

// Configuration
#ifndef GALAXY_N
#define GALAXY_N 500  // 500 stars - direct pixel updates are blazing fast!
#endif

// Star structure - 3D elliptical orbit with position tracking
typedef struct {
    // Orbit parameters
    float orbit_a;    // ellipse semi-major axis
    float orbit_b;    // ellipse semi-minor axis
    float cos_tilt;   // pre-calculated cos(tilt)
    float sin_tilt;   // pre-calculated sin(tilt)
    float angle;      // current angle on orbit (radians)
    float speed;      // angular velocity (radians/frame)
    
    // Visual properties
    uint16_t base_color;  // Star base color (white, yellowish, blueish)
    float brightness;     // Brightness phase (0-2π for pulsing)
    float brightness_speed; // How fast brightness oscillates
    
    // Position tracking for direct pixel updates
    int16_t x, y;         // Current position
    int16_t prev_x, prev_y;  // Previous position (for erasing)
    bool visible;     // Is this star currently visible?
} star_t;

// Galaxy state
typedef struct {
    star_t s[GALAXY_N];    // Stars array
    int w, h;              // Widget dimensions
    int cx, cy;            // Center coordinates
    float radius_px;       // Orbit radius
    int visible_count;     // Number of visible stars (always GALAXY_N for IDLE)
    lv_timer_t *timer;     // Animation timer
    
    // IMU-based 3D parallax effect
    float imu_offset_x;          // Parallax X offset in pixels
    float imu_offset_y;          // Parallax Y offset in pixels
    float imu_smoothed_x;        // Smoothed offset X (for damping)
    float imu_smoothed_y;        // Smoothed offset Y (for damping)
} galaxy_t;

// Global galaxy instance (needed for direct updates)
static galaxy_t *g_galaxy = nullptr;

/**
 * @brief Update star physics and positions
 */
static void galaxy_update_positions(galaxy_t *g) {
    for (int i = 0; i < g->visible_count; i++) {
        star_t *s = &g->s[i];
        
        // Save previous position
        s->prev_x = s->x;
        s->prev_y = s->y;
        
        // Advance angle
        s->angle += s->speed;
        if (s->angle > 6.2832f) s->angle -= 6.2832f;
        
        // Advance brightness phase
        if (s->brightness_speed > 0.0f) {
            s->brightness += s->brightness_speed;
            if (s->brightness > 6.2832f) s->brightness -= 6.2832f;
        }
        
        // Calculate 3D position
        float cos_angle = cosf(s->angle);
        float sin_angle = sinf(s->angle);
        
        float orbit_x = s->orbit_a * cos_angle;
        float orbit_y = s->orbit_b * sin_angle;
        
        float x3d = orbit_x * s->cos_tilt - orbit_y * s->sin_tilt;
        float y3d = orbit_x * s->sin_tilt + orbit_y * s->cos_tilt;
        
        // Apply global IMU-based parallax offset (same for ALL stars - no depth scaling)
        // This creates a simple panning effect when device is tilted
        float parallax_x = g->imu_smoothed_x;
        float parallax_y = g->imu_smoothed_y;
        
        // Project to screen with parallax
        s->x = g->cx + (int16_t)(x3d * g->radius_px + parallax_x);
        s->y = g->cy + (int16_t)(y3d * g->radius_px + parallax_y);
    }
}

/**
 * @brief Apply brightness modulation to base color
 */
static uint16_t apply_brightness(uint16_t base_color, float brightness_phase) {
    // Calculate brightness (0.2 to 1.0 range for MORE dramatic pulsing)
    float brightness = 0.2f + 0.8f * (1.0f + cosf(brightness_phase)) / 2.0f;
    
    // Extract RGB565 components
    uint8_t r = (base_color >> 11) & 0x1F;
    uint8_t g = (base_color >> 5) & 0x3F;
    uint8_t b = base_color & 0x1F;
    
    // Apply brightness
    r = (uint8_t)(r * brightness);
    g = (uint8_t)(g * brightness);
    b = (uint8_t)(b * brightness);
    
    // Repack RGB565
    return (r << 11) | (g << 5) | b;
}

/**
 * @brief Check if position is in UI exclusion zone
 */
static bool is_in_ui_zone(int16_t x, int16_t y, int w, int h) {
    // Top left (battery): x < 80, y < 30
    if (x < 80 && y < 30) return true;
    
    // Top right (WiFi): reduced by 30px from left -> x > w-50, y < 30
    if (x > w - 50 && y < 30) return true;
    
    // Top right (settings button): x > w-30, y between 60-85
    if (x > w - 30 && y > 60 && y < 85) return true;
    
    // Top right (mic button - moved down): x > w-30, y between 89-114
    if (x > w - 30 && y > 89 && y < 114) return true;
    
    // Bottom left - "IDLE" text only: x < 100, y > h-80, y < h-45
    if (x < 100 && y > h - 80 && y < h - 45) return true;
    
    // Bottom left - "Ready for command" text: x < 193, y > h-45, y < h-15 (extended 3px right)
    if (x < 193 && y > h - 45 && y < h - 15) return true;
    
    // Bottom right (power button - moved up 5px, right 5px): x > w-30, y > h-85, y < h-55
    if (x > w - 30 && y > h - 85 && y < h - 55) return true;
    
    return false;
}

/**
 * @brief Render stars via direct pixel updates (called after LVGL flush)
 */
void galaxy_render_direct(Arduino_GFX *gfx) {
    if (!g_galaxy) return;
    
    
    gfx->startWrite();  // Begin direct access
    
    for (int i = 0; i < g_galaxy->visible_count; i++) {
        star_t *s = &g_galaxy->s[i];
        
        // Erase old position (only if it moved and not in UI zone)
        if (s->prev_x != s->x || s->prev_y != s->y) {
            if (s->prev_x >= 0 && s->prev_x < g_galaxy->w && 
                s->prev_y >= 0 && s->prev_y < g_galaxy->h) {
                if (!is_in_ui_zone(s->prev_x, s->prev_y, g_galaxy->w, g_galaxy->h)) {
                    gfx->writePixel(s->prev_x, s->prev_y, 0x0000);  // Erase to black
                }
            }
        }
        
        // Draw new position (only if not in UI zone)
        if (s->x >= 0 && s->x < g_galaxy->w && 
            s->y >= 0 && s->y < g_galaxy->h) {
            if (!is_in_ui_zone(s->x, s->y, g_galaxy->w, g_galaxy->h)) {
                uint16_t color = (s->brightness_speed > 0.0f) 
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
static void galaxy_timer_cb(lv_timer_t *t) {
    galaxy_t *g = (galaxy_t*)t->user_data;
    if (!g) return;
    
    galaxy_update_positions(g);
    // Note: Actual rendering happens in galaxy_render_direct() after LVGL flush
}

/**
 * @brief Delete callback (cleanup)
 */
static void galaxy_del_cb(lv_event_t *e) {
    galaxy_t *g = (galaxy_t*)lv_event_get_user_data(e);
    if (g) {
        if (g->timer) lv_timer_del(g->timer);
        if (g == g_galaxy) g_galaxy = nullptr;
        lv_mem_free(g);
    }
}

/**
 * @brief Create galaxy widget (public API)
 */
lv_obj_t* galaxy_create(lv_obj_t *parent, int w, int h, uint32_t fps) {
    // Create invisible container (just for lifecycle management)
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_add_flag(container, LV_OBJ_FLAG_HIDDEN);  // Invisible - we render directly
    lv_obj_set_size(container, w, h);
    
    // Allocate galaxy state
    galaxy_t *g = (galaxy_t*)lv_mem_alloc(sizeof(galaxy_t));
    LV_ASSERT_MALLOC(g);
    memset(g, 0, sizeof(galaxy_t));
    
    g->w = w;
    g->h = h;
    g->cx = w / 2;
    g->cy = (h / 2) - 10;  // Render 10px higher
    g->radius_px = (LV_MIN(w, h) * 0.55f);  // Larger radius so some orbits extend beyond screen
    g->visible_count = GALAXY_N;  // All 500 stars visible
    
    // Initialize IMU parallax offset
    g->imu_offset_x = 0.0f;
    g->imu_offset_y = 0.0f;
    g->imu_smoothed_x = 0.0f;
    g->imu_smoothed_y = 0.0f;
    
    // Initialize stars with random orbits
    for (int i = 0; i < GALAXY_N; i++) {
        star_t *s = &g->s[i];
        
        // Orbit size: 0.1 to 1.3 (some beyond screen!)
        float dist = 0.1f + (lv_rand(0, 1200)) / 1000.0f;
        
        // More elliptical orbits - 70% get high eccentricity (0-0.9)
        float eccentricity;
        if (lv_rand(0, 100) < 70) {
            // 70% highly elliptical
            eccentricity = 0.4f + (lv_rand(0, 500)) / 1000.0f;  // 0.4-0.9
        } else {
            // 30% more circular
            eccentricity = (lv_rand(0, 400)) / 1000.0f;  // 0-0.4
        }
        s->orbit_a = dist;
        s->orbit_b = dist * (1.0f - eccentricity * 0.85f);
        
        // 3D tilt
        float tilt = (lv_rand(0, 3600)) / 572.96f;
        s->cos_tilt = cosf(tilt);
        s->sin_tilt = sinf(tilt);
        
        // Random starting angle
        s->angle = (lv_rand(0, 3600)) / 572.96f;
        
        // Variable speed based on distance (Kepler's laws - inner orbits faster!)
        // Base speed inversely proportional to distance
        float base_speed = 0.012f / (dist + 0.2f);  // Closer = faster
        s->speed = base_speed + (lv_rand(0, 100)) / 50000.0f;  // Add variation
        
        // Star color variety (RGB565 format) - more blue, with red and purple!
        int color_type = lv_rand(0, 100);
        if (color_type < 40) {
            // 40% Pure white
            s->base_color = 0xFFFF;
        } else if (color_type < 65) {
            // 25% Cool blue-white
            s->base_color = 0xBDFF;  // RGB(184, 223, 255)
        } else if (color_type < 78) {
            // 13% Warm yellow-white
            s->base_color = 0xFFF0;  // RGB(255, 254, 0)
        } else if (color_type < 86) {
            // 8% Orange-white
            s->base_color = 0xFE60;  // RGB(255, 204, 0)
        } else if (color_type < 96) {
            // 10% Pale gray-white
            s->base_color = 0xCE59;  // RGB(200, 200, 200)
        } else if (color_type < 99) {
            // 3% Purple-white (much whiter, subtle purple tint)
            s->base_color = 0xFDFF;  // RGB(248, 224, 255) very pale purple
        } else {
            // 1% RED stars! (~5 stars, much whiter with red tint)
            s->base_color = 0xFDF0;  // RGB(248, 224, 192) pale red-orange
        }
        
        // More stars pulse - 60% instead of 40%
        s->brightness = (lv_rand(0, 628)) / 100.0f;  // Random phase 0-2π
        s->brightness_speed = (lv_rand(0, 100) < 60) ? (lv_rand(5, 25) / 10000.0f) : 0.0f;  // 60% pulse
        
        s->x = s->y = -1;  // Off-screen initially
        s->prev_x = s->prev_y = -1;
        s->visible = (i < g->visible_count);
    }
    
    // Do initial position calculation
    galaxy_update_positions(g);
    
    // Set as global instance
    g_galaxy = g;
    
    lv_obj_set_user_data(container, g);
    lv_obj_add_event_cb(container, galaxy_del_cb, LV_EVENT_DELETE, g);
    
    // Create animation timer (30ms = ~30 FPS physics)
    uint32_t period = LV_MAX(16, 1000 / LV_CLAMP(1, fps, 60));
    g->timer = lv_timer_create(galaxy_timer_cb, period, g);
    
    return container;
}

/**
 * @brief Set GFX instance for direct rendering (no-op, kept for API compatibility)
 */
void galaxy_set_gfx(Arduino_GFX *gfx) {
    // No-op: gfx is passed directly to galaxy_render_direct()
    (void)gfx;
}


/**
 * @brief Set IMU-based 3D parallax offset
 * 
 * Creates a subtle 3D depth effect where the galaxy shifts based on device tilt.
 * The effect simulates looking through a window into deep space - when you tilt
 * the device, the galaxy shifts in the opposite direction (like parallax).
 * 
 * Far stars (smaller orbits) move less than near stars (larger orbits),
 * creating a convincing depth effect.
 */
void galaxy_set_imu_offset(float accel_x, float accel_y, float accel_z) {
    if (!g_galaxy) return;
    
    // Configuration
    const float MAX_OFFSET = 20.0f;      // Maximum parallax offset in pixels (subtle effect)
    const float SMOOTHING = 0.20f;        // Smoothing factor (0.0-1.0, lower = smoother)
    const float DEADZONE = 0.02f;         // Ignore small movements (noise reduction)
    
    // Calculate tilt angles from accelerometer
    // Correct axis mapping for natural parallax:
    // - Tilt right (positive accel_x) → stars shift left (negative X offset)
    // - Tilt down (positive accel_y) → stars shift down (positive Y offset)
    // Note: Axes were swapped - accel_y was controlling X, accel_x was controlling Y
    float tilt_x = -accel_y;  // Y accel controls horizontal shift (inverted)
    float tilt_y = accel_x;   // X accel controls vertical shift (not inverted)
    
    // Apply deadzone to prevent jitter
    if (fabsf(tilt_x) < DEADZONE) tilt_x = 0.0f;
    if (fabsf(tilt_y) < DEADZONE) tilt_y = 0.0f;
    
    // Clamp to reasonable tilt range (±1.0g)
    tilt_x = LV_CLAMP(-1.0f, tilt_x, 1.0f);
    tilt_y = LV_CLAMP(-1.0f, tilt_y, 1.0f);
    
    // Calculate target offset (linear mapping: ±1.0g → ±MAX_OFFSET pixels)
    float target_x = tilt_x * MAX_OFFSET;
    float target_y = tilt_y * MAX_OFFSET;
    
    // Smooth the offset using exponential moving average
    // This prevents jittery motion and makes the effect feel more natural
    g_galaxy->imu_smoothed_x += (target_x - g_galaxy->imu_smoothed_x) * SMOOTHING;
    g_galaxy->imu_smoothed_y += (target_y - g_galaxy->imu_smoothed_y) * SMOOTHING;
    
    
    // The actual parallax is applied in galaxy_update_positions()
    // where each star's offset is scaled by its depth_factor
}
