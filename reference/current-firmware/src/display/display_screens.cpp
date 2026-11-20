/**
 * @file display_screens.cpp
 * @brief All LVGL screen implementations
 * 
 * This file contains ALL screens (IDLE, RECORDING, PLAYING, etc.)
 * to minimize file count for easy integration.
 */

#include <Arduino.h>
#include "display/display_screens.h"
#include "display/display_galaxy.h"
#include "display/display_recording_ring.h"
#include "display/display_icons.h"
#include <stdio.h>  // For snprintf

// UI component references (stored in screen user data)
typedef struct {
    lv_obj_t *galaxy;           // Galaxy animation
    lv_obj_t *status_bar;       // Status bar container
    lv_obj_t *battery_bar;      // Battery indicator
    lv_obj_t *battery_tip;      // Battery tip
    lv_obj_t *battery_percent_label; // Battery percentage text
    lv_obj_t *charging_icon;    // Charging indicator (lightning bolt)
    lv_obj_t *wifi_bars[4];     // WiFi signal bars
    lv_obj_t *title_label;      // "IDLE" text
    lv_obj_t *subtitle_label;   // "Ready for command"
    lv_obj_t *power_btn;        // Power button
    lv_obj_t *mic_btn;          // Mic button
    lv_obj_t *settings_btn;     // Settings button
    lv_obj_t *separator_bar;    // Separator bar
    idle_event_cb_t event_cb;   // Event callback
} idle_screen_t;

// Screen dimensions (from pin_config.h)
#define SCREEN_WIDTH  368
#define SCREEN_HEIGHT 448

/**
 * @brief Create status bar with battery + WiFi
 */
static void create_status_bar(idle_screen_t *screen, lv_obj_t *parent) {
    // Status bar container (35px height, top of screen)
    screen->status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(screen->status_bar);
    lv_obj_set_size(screen->status_bar, SCREEN_WIDTH, 35);
    lv_obj_align(screen->status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen->status_bar, LV_OPA_TRANSP, 0);
    
    // === BATTERY INDICATOR ===
    // Battery bar (horizontal bar) - moved 15px right for rounded corners
    screen->battery_bar = lv_bar_create(screen->status_bar);
    lv_obj_set_size(screen->battery_bar, 30, 15);
    lv_obj_align(screen->battery_bar, LV_ALIGN_LEFT_MID, 25, 0);
    lv_bar_set_range(screen->battery_bar, 0, 100);
    lv_bar_set_value(screen->battery_bar, 75, LV_ANIM_OFF);
    
    // Style battery
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(screen->battery_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_bar, 1, 0);
    lv_obj_set_style_pad_all(screen->battery_bar, 2, 0);
    
    // Battery tip (right bump)
    screen->battery_tip = lv_obj_create(screen->status_bar);
    lv_obj_set_size(screen->battery_tip, 3, 8);
    lv_obj_align_to(screen->battery_tip, screen->battery_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(screen->battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_tip, 0, 0);
    lv_obj_set_style_radius(screen->battery_tip, 0, 0);
    
    // Battery percentage label (next to battery icon)
    screen->battery_percent_label = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->battery_percent_label, "100%");
    lv_obj_set_style_text_color(screen->battery_percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(screen->battery_percent_label, screen->battery_tip, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    // Charging indicator (lightning bolt) - overlaid on battery bar
    screen->charging_icon = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->charging_icon, LV_SYMBOL_CHARGE);  // ⚡ lightning bolt
    lv_obj_set_style_text_color(screen->charging_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->charging_icon, &lv_font_montserrat_16, 0);
    lv_obj_align_to(screen->charging_icon, screen->battery_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    
    // === WiFi SIGNAL BARS ===
    const int bar_heights[] = {4, 8, 12, 16};
    const int bar_width = 3;
    const int bar_spacing = 2;
    
    for (int i = 0; i < 4; i++) {
        screen->wifi_bars[i] = lv_obj_create(screen->status_bar);
        lv_obj_set_size(screen->wifi_bars[i], bar_width, bar_heights[i]);
        lv_obj_align(screen->wifi_bars[i], LV_ALIGN_RIGHT_MID, 
                     -25 - (3 - i) * (bar_width + bar_spacing), 
                     (16 - bar_heights[i]) / 2);
        lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_width(screen->wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(screen->wifi_bars[i], 0, 0);
    }
}

/**
 * @brief Mic button click event
 */
static void mic_btn_event_cb(lv_event_t *e) {
    idle_screen_t *screen = (idle_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(IDLE_EVENT_MIC_CLICKED);
    }
}

/**
 * @brief Power button click event
 */
static void power_btn_event_cb(lv_event_t *e) {
    idle_screen_t *screen = (idle_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(IDLE_EVENT_POWER_CLICKED);
    }
}

/**
 * @brief Create UI elements
 */
static void create_ui_elements(idle_screen_t *screen, lv_obj_t *parent) {
    // === TITLE LABEL (moved to bottom-left) ===
    screen->title_label = lv_label_create(parent);
    lv_label_set_text(screen->title_label, "IDLE");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0x00FF00), 0);  // Green
    lv_obj_align(screen->title_label, LV_ALIGN_BOTTOM_LEFT, 25, -50);  // Bottom-left, same margin as battery
    
    // === SUBTITLE LABEL (below IDLE text) ===
    screen->subtitle_label = lv_label_create(parent);
    lv_label_set_text(screen->subtitle_label, "Ready for command");
    lv_obj_set_style_text_font(screen->subtitle_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(screen->subtitle_label, lv_color_white(), 0);
    lv_obj_align(screen->subtitle_label, LV_ALIGN_BOTTOM_LEFT, 25, -20);  // Below IDLE text
    
    // === POWER BUTTON (Small, bottom-right) ===
    screen->power_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->power_btn, 20, 20);
    lv_obj_align(screen->power_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -60);  // Moved up 5px, right 5px
    lv_obj_set_style_radius(screen->power_btn, 10, 0);  // Circular (half of 20)
    lv_obj_set_style_bg_color(screen->power_btn, lv_color_hex(0xFF0000), 0);  // Red
    lv_obj_set_style_shadow_width(screen->power_btn, 0, 0);  // No shadow for small button
    lv_obj_add_event_cb(screen->power_btn, power_btn_event_cb, LV_EVENT_CLICKED, screen);
    
    // Power icon (smaller font)
    lv_obj_t *power_label = lv_label_create(screen->power_btn);
    lv_label_set_text(power_label, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_14, 0);  // Smaller font
    lv_obj_set_style_text_color(power_label, lv_color_white(), 0);
    lv_obj_center(power_label);
    
    // === SETTINGS BUTTON (Small, top-right) ===
    screen->settings_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->settings_btn, 20, 20);
    lv_obj_align(screen->settings_btn, LV_ALIGN_TOP_RIGHT, -5, 60);  // 60px from top
    lv_obj_set_style_radius(screen->settings_btn, 10, 0);  // Circular
    lv_obj_set_style_bg_color(screen->settings_btn, lv_color_hex(0x404040), 0);  // Dark gray
    lv_obj_set_style_shadow_width(screen->settings_btn, 0, 0);  // No shadow
    
    // Settings icon
    lv_obj_t *settings_label = lv_label_create(screen->settings_btn);
    lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settings_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(settings_label, lv_color_white(), 0);
    lv_obj_center(settings_label);
    
    // === SEPARATOR BAR (1px white with 2px margins) ===
    screen->separator_bar = lv_obj_create(parent);
    lv_obj_set_size(screen->separator_bar, 20, 1);  // 1px height, 20px width
    lv_obj_align(screen->separator_bar, LV_ALIGN_TOP_RIGHT, -5, 84);  // 2px below settings (60+20+2+2)
    lv_obj_set_style_bg_color(screen->separator_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->separator_bar, 0, 0);  // No border
    lv_obj_set_style_pad_all(screen->separator_bar, 0, 0);  // No padding
    
    // === MIC BUTTON (Small, below separator bar) ===
    screen->mic_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->mic_btn, 20, 20);
    lv_obj_align(screen->mic_btn, LV_ALIGN_TOP_RIGHT, -5, 89);  // 2px below separator (60+20+2+1+2+2)
    lv_obj_set_style_radius(screen->mic_btn, 10, 0);  // Circular
    lv_obj_set_style_bg_color(screen->mic_btn, lv_color_hex(0x0080FF), 0);  // Blue
    lv_obj_set_style_shadow_width(screen->mic_btn, 0, 0);  // No shadow
    
    // Custom mic icon (small, 20x20)
    lv_obj_t *mic_icon = icon_create_mic_small(screen->mic_btn, lv_color_white());
    lv_obj_center(mic_icon);
    
    // === REMOVED: Add star button and count (500 stars always visible) ===
}

/**
 * @brief Screen delete callback (cleanup)
 */
static void screen_idle_delete_cb(lv_event_t *e) {
    idle_screen_t *screen = (idle_screen_t*)lv_event_get_user_data(e);
    if (screen) {
        lv_mem_free(screen);
    }
}

/**
 * @brief Create IDLE screen (public API)
 */
lv_obj_t* screen_idle_create(lv_obj_t *parent, idle_event_cb_t event_cb) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Allocate screen state
    idle_screen_t *screen = (idle_screen_t*)lv_mem_alloc(sizeof(idle_screen_t));
    LV_ASSERT_MALLOC(screen);
    memset(screen, 0, sizeof(idle_screen_t));
    screen->event_cb = event_cb;
    lv_obj_set_user_data(screen_obj, screen);
    
    // Register cleanup callback
    lv_obj_add_event_cb(screen_obj, screen_idle_delete_cb, LV_EVENT_DELETE, screen);
    
    // === GALAXY BACKGROUND (full-screen with direct pixel rendering) ===
    screen->galaxy = galaxy_create(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT, 30);  // 30 FPS target
    lv_obj_align(screen->galaxy, LV_ALIGN_CENTER, 0, 0);
    
    // === STATUS BAR ===
    create_status_bar(screen, screen_obj);
    
    // === UI ELEMENTS ===
    create_ui_elements(screen, screen_obj);
    
    return screen_obj;
}

/**
 * @brief Update status bar (public API)
 */
void screen_idle_update_status(lv_obj_t *screen_obj, const idle_status_t *status) {
    idle_screen_t *screen = (idle_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !status) return;
    
    // Update battery
    lv_bar_set_value(screen->battery_bar, status->battery_percent, LV_ANIM_OFF);
    
    // Update battery percentage text
    char battery_text[8];
    snprintf(battery_text, sizeof(battery_text), "%d%%", status->battery_percent);
    lv_label_set_text(screen->battery_percent_label, battery_text);
    
    // Battery color based on level
    if (status->battery_percent > 50) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);  // Green
    } else if (status->battery_percent > 20) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);  // Yellow
    } else {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);  // Red
    }
    
    // Show/hide charging indicator
    if (status->battery_charging) {
        lv_obj_clear_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);  // Show ⚡
    } else {
        lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);    // Hide
    }
    
    // Update WiFi bars
    for (int i = 0; i < 4; i++) {
        if (status->wifi_connected && i < status->wifi_bars) {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_white(), 0);  // Active
        } else {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);  // Inactive
        }
    }
}

/**
 * @brief Show/hide screen (public API)
 */
void screen_idle_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// RECORDING SCREEN
// ============================================

/**
 * @brief Recording screen structure (same layout as IDLE)
 */
typedef struct {
    // Status bar elements (same as IDLE)
    lv_obj_t *status_bar;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_tip;
    lv_obj_t *battery_percent_label;
    lv_obj_t *charging_icon;
    lv_obj_t *wifi_bars[4];
    
    // UI elements
    lv_obj_t *title_label;       // "Recording" text (vivid red)
    lv_obj_t *duration_label;    // "Duration: x/60" combined text
    lv_obj_t *spinner;           // Red spinner next to duration
    lv_obj_t *mic_icon_large;    // Large mic icon in center
    lv_obj_t *recording_ring;    // Recording ring animation (stars around mic)
    lv_obj_t *power_btn;         // Power button
    lv_obj_t *stop_btn;          // Stop button (green tick)
    
    // Event callback
    recording_event_cb_t event_cb;
} recording_screen_t;

/**
 * @brief Stop button event callback
 */
static void stop_btn_event_cb(lv_event_t *e) {
    recording_screen_t *screen = (recording_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(RECORDING_EVENT_STOP_CLICKED);
    }
}

/**
 * @brief Power button event callback (recording screen)
 */
static void recording_power_btn_event_cb(lv_event_t *e) {
    recording_screen_t *screen = (recording_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(RECORDING_EVENT_POWER_CLICKED);
    }
}

/**
 * @brief Create status bar for recording screen (EXACT copy from IDLE)
 */
static void create_recording_status_bar(recording_screen_t *screen, lv_obj_t *parent) {
    // Status bar container (35px height, top of screen)
    screen->status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(screen->status_bar);
    lv_obj_set_size(screen->status_bar, SCREEN_WIDTH, 35);
    lv_obj_align(screen->status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen->status_bar, LV_OPA_TRANSP, 0);
    
    // === BATTERY INDICATOR ===
    // Battery bar (horizontal bar) - moved 15px right for rounded corners
    screen->battery_bar = lv_bar_create(screen->status_bar);
    lv_obj_set_size(screen->battery_bar, 30, 15);
    lv_obj_align(screen->battery_bar, LV_ALIGN_LEFT_MID, 25, 0);
    lv_bar_set_range(screen->battery_bar, 0, 100);
    lv_bar_set_value(screen->battery_bar, 75, LV_ANIM_OFF);
    
    // Style battery
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(screen->battery_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_bar, 1, 0);
    lv_obj_set_style_pad_all(screen->battery_bar, 2, 0);
    
    // Battery tip (right bump)
    screen->battery_tip = lv_obj_create(screen->status_bar);
    lv_obj_set_size(screen->battery_tip, 3, 8);
    lv_obj_align_to(screen->battery_tip, screen->battery_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(screen->battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_tip, 0, 0);
    lv_obj_set_style_radius(screen->battery_tip, 0, 0);
    
    // Battery percentage label (next to battery icon)
    screen->battery_percent_label = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->battery_percent_label, "100%");
    lv_obj_set_style_text_color(screen->battery_percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(screen->battery_percent_label, screen->battery_tip, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    // Charging indicator (lightning bolt) - overlaid on battery bar
    screen->charging_icon = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->charging_icon, LV_SYMBOL_CHARGE);  // ⚡ lightning bolt
    lv_obj_set_style_text_color(screen->charging_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->charging_icon, &lv_font_montserrat_16, 0);
    lv_obj_align_to(screen->charging_icon, screen->battery_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    
    // === WiFi SIGNAL BARS ===
    const int bar_heights[] = {4, 8, 12, 16};
    const int bar_width = 3;
    const int bar_spacing = 2;
    
    for (int i = 0; i < 4; i++) {
        screen->wifi_bars[i] = lv_obj_create(screen->status_bar);
        lv_obj_set_size(screen->wifi_bars[i], bar_width, bar_heights[i]);
        lv_obj_align(screen->wifi_bars[i], LV_ALIGN_RIGHT_MID, 
                     -25 - (3 - i) * (bar_width + bar_spacing), 
                     (16 - bar_heights[i]) / 2);
        lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_width(screen->wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(screen->wifi_bars[i], 0, 0);
    }
}

/**
 * @brief Create UI elements for recording screen
 */
static void create_recording_ui_elements(recording_screen_t *screen, lv_obj_t *parent) {
    // === TITLE LABEL "Recording" (EXACT match to IDLE) ===
    screen->title_label = lv_label_create(parent);
    lv_label_set_text(screen->title_label, "Recording");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0xFF0000), 0);  // Vivid red (same as IDLE but red)
    lv_obj_align(screen->title_label, LV_ALIGN_BOTTOM_LEFT, 25, -50);  // EXACT match to IDLE position
    
    // === DURATION LABEL with timer (EXACT match to "Ready for command" position) ===
    screen->duration_label = lv_label_create(parent);
    lv_label_set_text(screen->duration_label, "Duration: 0/60");
    lv_obj_set_style_text_font(screen->duration_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(screen->duration_label, lv_color_white(), 0);
    lv_obj_align(screen->duration_label, LV_ALIGN_BOTTOM_LEFT, 25, -20);  // EXACT match to subtitle position
    
    // === RED SPINNER REMOVED (was causing fuzzy/incomplete recordings) ===
    // Spinner animation consumed resources and interfered with audio recording
    
    // === LARGE MIC ICON (Center of screen) ===
    screen->mic_icon_large = icon_create_mic_large(parent, lv_color_hex(0xFF0000));  // Red mic
    lv_obj_align(screen->mic_icon_large, LV_ALIGN_CENTER, 0, -20);  // Slightly above center
    
    // === CANCEL BUTTON (Small, bottom-right, black bg with red X) ===
    screen->power_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->power_btn, 20, 20);
    lv_obj_align(screen->power_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -60);
    lv_obj_set_style_radius(screen->power_btn, 0, 0);  // Square, not round
    lv_obj_set_style_bg_color(screen->power_btn, lv_color_hex(0x000000), 0);  // Black background
    lv_obj_set_style_shadow_width(screen->power_btn, 0, 0);
    lv_obj_add_event_cb(screen->power_btn, recording_power_btn_event_cb, LV_EVENT_CLICKED, screen);
    
    lv_obj_t *power_label = lv_label_create(screen->power_btn);
    lv_label_set_text(power_label, LV_SYMBOL_CLOSE);  // X icon
    lv_obj_set_style_text_font(power_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(power_label, lv_color_hex(0xFF0000), 0);  // Red X
    lv_obj_center(power_label);
    
    // === STOP BUTTON (Black bg with green tick, centered between settings and old stop position) ===
    screen->stop_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->stop_btn, 20, 20);
    // Centered: settings at 60, old stop at 89, midpoint = (60+89)/2 = 74.5 ≈ 75
    lv_obj_align(screen->stop_btn, LV_ALIGN_TOP_RIGHT, -5, 75);
    lv_obj_set_style_radius(screen->stop_btn, 0, 0);  // Square, not round
    lv_obj_set_style_bg_color(screen->stop_btn, lv_color_hex(0x000000), 0);  // Black background
    lv_obj_set_style_shadow_width(screen->stop_btn, 0, 0);
    lv_obj_add_event_cb(screen->stop_btn, stop_btn_event_cb, LV_EVENT_CLICKED, screen);
    
    lv_obj_t *stop_label = lv_label_create(screen->stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_OK);  // Checkmark
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0x00FF00), 0);  // Green tick
    lv_obj_center(stop_label);
}

/**
 * @brief Screen delete callback
 */
static void screen_recording_delete_cb(lv_event_t *e) {
    recording_screen_t *screen = (recording_screen_t*)lv_event_get_user_data(e);
    if (screen) {
        lv_mem_free(screen);
    }
}

/**
 * @brief Create RECORDING screen (public API)
 */
lv_obj_t* screen_recording_create(lv_obj_t *parent, recording_event_cb_t event_cb) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Allocate screen state
    recording_screen_t *screen = (recording_screen_t*)lv_mem_alloc(sizeof(recording_screen_t));
    LV_ASSERT_MALLOC(screen);
    memset(screen, 0, sizeof(recording_screen_t));
    screen->event_cb = event_cb;
    lv_obj_set_user_data(screen_obj, screen);
    
    // Register cleanup callback
    lv_obj_add_event_cb(screen_obj, screen_recording_delete_cb, LV_EVENT_DELETE, screen);
    
    // === STATUS BAR ===
    create_recording_status_bar(screen, screen_obj);
    
    // === UI ELEMENTS ===
    create_recording_ui_elements(screen, screen_obj);
    
    // === RECORDING RING ANIMATION (stars around mic icon) ===
    // Mic icon is at center (184, 224) offset by -20px vertically
    int mic_center_x = SCREEN_WIDTH / 2;  // 184
    int mic_center_y = (SCREEN_HEIGHT / 2) - 20;  // 224 - 20 = 204
    
    // Create ring: 50px wide ring around mic (inner radius ~70px, outer ~120px for mic icon size)
    screen->recording_ring = recording_ring_create(screen_obj, mic_center_x, mic_center_y, 
                                                    70, 120, 30);  // 30 FPS
    
    return screen_obj;
}

/**
 * @brief Update status bar (public API)
 */
void screen_recording_update_status(lv_obj_t *screen_obj, const idle_status_t *status) {
    recording_screen_t *screen = (recording_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !status) return;
    
    // Update battery
    lv_bar_set_value(screen->battery_bar, status->battery_percent, LV_ANIM_OFF);
    
    char battery_text[8];
    snprintf(battery_text, sizeof(battery_text), "%d%%", status->battery_percent);
    lv_label_set_text(screen->battery_percent_label, battery_text);
    
    // Battery color
    if (status->battery_percent > 50) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    } else if (status->battery_percent > 20) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }
    
    // Charging indicator
    if (status->battery_charging) {
        lv_obj_clear_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    }
    
    // WiFi bars
    for (int i = 0; i < 4; i++) {
        if (status->wifi_connected && i < status->wifi_bars) {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        }
    }
}

/**
 * @brief Update recording duration (public API)
 */
void screen_recording_update_duration(lv_obj_t *screen_obj, int seconds) {
    recording_screen_t *screen = (recording_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Clamp to 0-60
    if (seconds < 0) seconds = 0;
    if (seconds > 60) seconds = 60;
    
    // Update combined "Duration: x/60" label
    char timer_text[32];
    snprintf(timer_text, sizeof(timer_text), "Duration: %d/60", seconds);
    lv_label_set_text(screen->duration_label, timer_text);
    
    // Spinner removed - was causing audio recording interference
}

/**
 * @brief Show/hide RECORDING screen (public API)
 */
void screen_recording_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// UPLOADING SCREEN
// ============================================

/**
 * @brief Uploading screen structure (same layout as RECORDING)
 */
typedef struct {
    // Status bar elements (same as IDLE)
    lv_obj_t *status_bar;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_tip;
    lv_obj_t *battery_percent_label;
    lv_obj_t *charging_icon;
    lv_obj_t *wifi_bars[4];
    
    // UI elements
    lv_obj_t *title_label;       // "Uploading" text (vivid orange)
    lv_obj_t *progress_label;    // "x kb / x kb" text
    lv_obj_t *spinner;           // Orange spinner
    lv_obj_t *power_btn;         // Power button (no icon)
    lv_obj_t *cancel_btn;        // Cancel button
    
    // Event callback
    uploading_event_cb_t event_cb;
} uploading_screen_t;

/**
 * @brief Cancel button event callback
 */
static void uploading_cancel_btn_event_cb(lv_event_t *e) {
    uploading_screen_t *screen = (uploading_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(UPLOADING_EVENT_CANCEL_CLICKED);
    }
}

/**
 * @brief Power button event callback (uploading screen)
 */
static void uploading_power_btn_event_cb(lv_event_t *e) {
    uploading_screen_t *screen = (uploading_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(UPLOADING_EVENT_POWER_CLICKED);
    }
}

/**
 * @brief Create status bar for uploading screen (EXACT copy from RECORDING)
 */
static void create_uploading_status_bar(uploading_screen_t *screen, lv_obj_t *parent) {
    // Status bar container (35px height, top of screen)
    screen->status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(screen->status_bar);
    lv_obj_set_size(screen->status_bar, SCREEN_WIDTH, 35);
    lv_obj_align(screen->status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen->status_bar, LV_OPA_TRANSP, 0);
    
    // === BATTERY INDICATOR ===
    screen->battery_bar = lv_bar_create(screen->status_bar);
    lv_obj_set_size(screen->battery_bar, 30, 15);
    lv_obj_align(screen->battery_bar, LV_ALIGN_LEFT_MID, 25, 0);
    lv_bar_set_range(screen->battery_bar, 0, 100);
    lv_bar_set_value(screen->battery_bar, 75, LV_ANIM_OFF);
    
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(screen->battery_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_bar, 1, 0);
    lv_obj_set_style_pad_all(screen->battery_bar, 2, 0);
    
    screen->battery_tip = lv_obj_create(screen->status_bar);
    lv_obj_set_size(screen->battery_tip, 3, 8);
    lv_obj_align_to(screen->battery_tip, screen->battery_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(screen->battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_tip, 0, 0);
    lv_obj_set_style_radius(screen->battery_tip, 0, 0);
    
    screen->battery_percent_label = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->battery_percent_label, "100%");
    lv_obj_set_style_text_color(screen->battery_percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(screen->battery_percent_label, screen->battery_tip, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    screen->charging_icon = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->charging_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(screen->charging_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->charging_icon, &lv_font_montserrat_16, 0);
    lv_obj_align_to(screen->charging_icon, screen->battery_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    
    // === WiFi SIGNAL BARS ===
    const int bar_heights[] = {4, 8, 12, 16};
    const int bar_width = 3;
    const int bar_spacing = 2;
    
    for (int i = 0; i < 4; i++) {
        screen->wifi_bars[i] = lv_obj_create(screen->status_bar);
        lv_obj_set_size(screen->wifi_bars[i], bar_width, bar_heights[i]);
        lv_obj_align(screen->wifi_bars[i], LV_ALIGN_RIGHT_MID, 
                     -25 - (3 - i) * (bar_width + bar_spacing), 
                     (16 - bar_heights[i]) / 2);
        lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_width(screen->wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(screen->wifi_bars[i], 0, 0);
    }
}

/**
 * @brief Create UI elements for uploading screen
 */
static void create_uploading_ui_elements(uploading_screen_t *screen, lv_obj_t *parent) {
    // === TITLE LABEL "Uploading" (EXACT match to IDLE position, BRIGHT ORANGE) ===
    screen->title_label = lv_label_create(parent);
    lv_label_set_text(screen->title_label, "Uploading");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0xFFA500), 0);  // Bright orange
    lv_obj_align(screen->title_label, LV_ALIGN_BOTTOM_LEFT, 25, -50);
    
    // === PROGRESS LABEL "x kb / x kb" (EXACT match to subtitle position) ===
    screen->progress_label = lv_label_create(parent);
    lv_label_set_text(screen->progress_label, "0 kb / 0 kb");
    lv_obj_set_style_text_font(screen->progress_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(screen->progress_label, lv_color_white(), 0);
    lv_obj_align(screen->progress_label, LV_ALIGN_BOTTOM_LEFT, 25, -20);
    
    // === SPINNER (Bright Orange, centered mid-screen, thick) ===
    screen->spinner = lv_spinner_create(parent, 1000, 90);  // Create on parent for proper cleanup
    lv_obj_set_size(screen->spinner, 120, 120);  // Large spinner
    lv_obj_align(screen->spinner, LV_ALIGN_CENTER, 0, 0);  // Center relative to parent
    lv_obj_set_style_arc_color(screen->spinner, lv_color_hex(0xFFA500), LV_PART_INDICATOR);  // Bright orange
    lv_obj_set_style_arc_width(screen->spinner, 10, LV_PART_INDICATOR);  // Thicker
    lv_obj_set_style_arc_color(screen->spinner, lv_color_hex(0x404040), LV_PART_MAIN);  // Dark gray background
    lv_obj_set_style_arc_width(screen->spinner, 10, LV_PART_MAIN);  // Thicker
    
    // === NO TOP BUTTON ===
    // Removed - no power button
    
    // === CANCEL BUTTON (Bottom, black bg with red X) ===
    screen->cancel_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->cancel_btn, 20, 20);
    lv_obj_align(screen->cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -60);  // Bottom position
    lv_obj_set_style_radius(screen->cancel_btn, 0, 0);  // Square, not round
    lv_obj_set_style_bg_color(screen->cancel_btn, lv_color_hex(0x000000), 0);  // Black background
    lv_obj_set_style_shadow_width(screen->cancel_btn, 0, 0);
    lv_obj_add_event_cb(screen->cancel_btn, uploading_cancel_btn_event_cb, LV_EVENT_CLICKED, screen);
    
    lv_obj_t *cancel_label = lv_label_create(screen->cancel_btn);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE);  // X icon
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xFF0000), 0);  // Red X
    lv_obj_center(cancel_label);
}

/**
 * @brief Screen delete callback
 */
static void screen_uploading_delete_cb(lv_event_t *e) {
    uploading_screen_t *screen = (uploading_screen_t*)lv_event_get_user_data(e);
    if (screen) {
        lv_mem_free(screen);
    }
}

/**
 * @brief Create UPLOADING screen (public API)
 */
lv_obj_t* screen_uploading_create(lv_obj_t *parent, uploading_event_cb_t event_cb) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Allocate screen state
    uploading_screen_t *screen = (uploading_screen_t*)lv_mem_alloc(sizeof(uploading_screen_t));
    LV_ASSERT_MALLOC(screen);
    memset(screen, 0, sizeof(uploading_screen_t));
    screen->event_cb = event_cb;
    lv_obj_set_user_data(screen_obj, screen);
    
    // Register cleanup callback
    lv_obj_add_event_cb(screen_obj, screen_uploading_delete_cb, LV_EVENT_DELETE, screen);
    
    // === NO GALAXY BACKGROUND ===
    
    // === STATUS BAR ===
    create_uploading_status_bar(screen, screen_obj);
    
    // === UI ELEMENTS ===
    create_uploading_ui_elements(screen, screen_obj);
    
    return screen_obj;
}

/**
 * @brief Update status bar (public API)
 */
void screen_uploading_update_status(lv_obj_t *screen_obj, const idle_status_t *status) {
    uploading_screen_t *screen = (uploading_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !status) return;
    
    lv_bar_set_value(screen->battery_bar, status->battery_percent, LV_ANIM_OFF);
    
    char battery_text[8];
    snprintf(battery_text, sizeof(battery_text), "%d%%", status->battery_percent);
    lv_label_set_text(screen->battery_percent_label, battery_text);
    
    if (status->battery_percent > 50) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    } else if (status->battery_percent > 20) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }
    
    if (status->battery_charging) {
        lv_obj_clear_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    }
    
    for (int i = 0; i < 4; i++) {
        if (status->wifi_connected && i < status->wifi_bars) {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        }
    }
}

/**
 * @brief Update upload progress (public API)
 */
void screen_uploading_update_progress(lv_obj_t *screen_obj, int sent_kb, int total_kb) {
    uploading_screen_t *screen = (uploading_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) {
        Serial.println("⚠️  screen_uploading_update_progress: screen is NULL!");
        return;
    }
    
    if (!screen->progress_label) {
        Serial.println("⚠️  screen_uploading_update_progress: progress_label is NULL!");
        return;
    }
    
    // Update "x kb / x kb" label
    char progress_text[32];
    snprintf(progress_text, sizeof(progress_text), "%d kb / %d kb", sent_kb, total_kb);
    lv_label_set_text(screen->progress_label, progress_text);
    
    // Keep spinner centered mid-screen (no repositioning based on text)
    
    // Note: lv_refr_now() is called at 24fps in the upload loop itself
}

/**
 * @brief Show/hide UPLOADING screen (public API)
 */
void screen_uploading_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// DOWNLOADING SCREEN
// ============================================

/**
 * @brief Downloading screen structure (same as UPLOADING but yellow)
 */
typedef struct {
    // Status bar elements (same as IDLE)
    lv_obj_t *status_bar;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_tip;
    lv_obj_t *battery_percent_label;
    lv_obj_t *charging_icon;
    lv_obj_t *wifi_bars[4];
    
    // UI elements
    lv_obj_t *title_label;       // "Downloading" text (bright yellow)
    lv_obj_t *progress_label;    // "x kb / x kb" text
    lv_obj_t *spinner;           // Yellow spinner
    lv_obj_t *cancel_btn;        // Cancel button
    
    // Event callback
    uploading_event_cb_t event_cb;  // Reuse uploading event type
} downloading_screen_t;

/**
 * @brief Cancel button event callback
 */
static void downloading_cancel_btn_event_cb(lv_event_t *e) {
    downloading_screen_t *screen = (downloading_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(UPLOADING_EVENT_CANCEL_CLICKED);
    }
}

/**
 * @brief Create status bar for downloading screen
 */
static void create_downloading_status_bar(downloading_screen_t *screen, lv_obj_t *parent) {
    // Status bar container (35px height, top of screen)
    screen->status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(screen->status_bar);
    lv_obj_set_size(screen->status_bar, SCREEN_WIDTH, 35);
    lv_obj_align(screen->status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen->status_bar, LV_OPA_TRANSP, 0);
    
    // === BATTERY INDICATOR ===
    screen->battery_bar = lv_bar_create(screen->status_bar);
    lv_obj_set_size(screen->battery_bar, 30, 15);
    lv_obj_align(screen->battery_bar, LV_ALIGN_LEFT_MID, 25, 0);
    lv_bar_set_range(screen->battery_bar, 0, 100);
    lv_bar_set_value(screen->battery_bar, 75, LV_ANIM_OFF);
    
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(screen->battery_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_bar, 1, 0);
    lv_obj_set_style_pad_all(screen->battery_bar, 2, 0);
    
    screen->battery_tip = lv_obj_create(screen->status_bar);
    lv_obj_set_size(screen->battery_tip, 3, 8);
    lv_obj_align_to(screen->battery_tip, screen->battery_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(screen->battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_tip, 0, 0);
    lv_obj_set_style_radius(screen->battery_tip, 0, 0);
    
    screen->battery_percent_label = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->battery_percent_label, "100%");
    lv_obj_set_style_text_color(screen->battery_percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(screen->battery_percent_label, screen->battery_tip, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    screen->charging_icon = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->charging_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(screen->charging_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->charging_icon, &lv_font_montserrat_16, 0);
    lv_obj_align_to(screen->charging_icon, screen->battery_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    
    // === WiFi SIGNAL BARS ===
    const int bar_heights[] = {4, 8, 12, 16};
    const int bar_width = 3;
    const int bar_spacing = 2;
    
    for (int i = 0; i < 4; i++) {
        screen->wifi_bars[i] = lv_obj_create(screen->status_bar);
        lv_obj_set_size(screen->wifi_bars[i], bar_width, bar_heights[i]);
        lv_obj_align(screen->wifi_bars[i], LV_ALIGN_RIGHT_MID, 
                     -25 - (3 - i) * (bar_width + bar_spacing), 
                     (16 - bar_heights[i]) / 2);
        lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_width(screen->wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(screen->wifi_bars[i], 0, 0);
    }
}

/**
 * @brief Create UI elements for downloading screen
 */
static void create_downloading_ui_elements(downloading_screen_t *screen, lv_obj_t *parent) {
    // Check heap integrity before creating widgets
    heap_caps_check_integrity_all(true);
    
    // === TITLE LABEL "Downloading" (BRIGHT YELLOW) ===
    screen->title_label = lv_label_create(parent);
    lv_label_set_text(screen->title_label, "Downloading");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0xFFFF00), 0);  // Bright yellow
    lv_obj_align(screen->title_label, LV_ALIGN_BOTTOM_LEFT, 25, -50);
    
    // === PROGRESS LABEL "x kb / x kb" ===
    screen->progress_label = lv_label_create(parent);
    lv_label_set_text(screen->progress_label, "0 kb / 0 kb");
    lv_obj_set_style_text_font(screen->progress_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(screen->progress_label, lv_color_white(), 0);
    lv_obj_align(screen->progress_label, LV_ALIGN_BOTTOM_LEFT, 25, -20);
    
    // === SPINNER (Bright Yellow, centered mid-screen, thick) ===
    screen->spinner = lv_spinner_create(parent, 1000, 90);  // Create on parent for proper cleanup
    lv_obj_set_size(screen->spinner, 120, 120);  // Large spinner
    lv_obj_align(screen->spinner, LV_ALIGN_CENTER, 0, 0);  // Center relative to parent
    lv_obj_set_style_arc_color(screen->spinner, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);  // Bright yellow
    lv_obj_set_style_arc_width(screen->spinner, 10, LV_PART_INDICATOR);  // Thicker
    lv_obj_set_style_arc_color(screen->spinner, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(screen->spinner, 10, LV_PART_MAIN);  // Thicker
    
    // === CANCEL BUTTON (Bottom, black bg with red X) ===
    screen->cancel_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->cancel_btn, 20, 20);
    lv_obj_align(screen->cancel_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -60);
    lv_obj_set_style_radius(screen->cancel_btn, 0, 0);  // Square, not round
    lv_obj_set_style_bg_color(screen->cancel_btn, lv_color_hex(0x000000), 0);  // Black background
    lv_obj_set_style_shadow_width(screen->cancel_btn, 0, 0);
    lv_obj_add_event_cb(screen->cancel_btn, downloading_cancel_btn_event_cb, LV_EVENT_CLICKED, screen);
    
    lv_obj_t *cancel_label = lv_label_create(screen->cancel_btn);
    lv_label_set_text(cancel_label, LV_SYMBOL_CLOSE);  // X icon
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0xFF0000), 0);  // Red X
    lv_obj_center(cancel_label);
}

/**
 * @brief Screen delete callback
 */
static void screen_downloading_delete_cb(lv_event_t *e) {
    downloading_screen_t *screen = (downloading_screen_t*)lv_event_get_user_data(e);
    if (screen) {
        lv_mem_free(screen);
    }
}

/**
 * @brief Create DOWNLOADING screen (public API)
 */
lv_obj_t* screen_downloading_create(lv_obj_t *parent, uploading_event_cb_t event_cb) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_center(screen_obj);
    lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);  // Force opaque black background
    
    // Allocate screen state
    downloading_screen_t *screen = (downloading_screen_t*)lv_mem_alloc(sizeof(downloading_screen_t));
    LV_ASSERT_MALLOC(screen);
    memset(screen, 0, sizeof(downloading_screen_t));
    lv_obj_set_user_data(screen_obj, screen);
    
    // Register cleanup callback
    lv_obj_add_event_cb(screen_obj, screen_downloading_delete_cb, LV_EVENT_DELETE, screen);
    
    // Create UI elements
    create_downloading_status_bar(screen, screen_obj);
    create_downloading_ui_elements(screen, screen_obj);
    
    Serial.println("📥 DOWNLOADING screen...✓");
    return screen_obj;
}

/**
 * @brief Update status bar (public API)
 */
void screen_downloading_update_status(lv_obj_t *screen_obj, const idle_status_t *status) {
    downloading_screen_t *screen = (downloading_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !status) return;
    
    lv_bar_set_value(screen->battery_bar, status->battery_percent, LV_ANIM_OFF);
    
    char battery_text[8];
    snprintf(battery_text, sizeof(battery_text), "%d%%", status->battery_percent);
    lv_label_set_text(screen->battery_percent_label, battery_text);
    
    if (status->battery_percent > 50) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    } else if (status->battery_percent > 20) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }
    
    if (status->battery_charging) {
        lv_obj_clear_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    }
    
    for (int i = 0; i < 4; i++) {
        if (status->wifi_connected && i < status->wifi_bars) {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        }
    }
}

/**
 * @brief Update download progress (public API)
 */
void screen_downloading_update_progress(lv_obj_t *screen_obj, int received_kb, int total_kb) {
    downloading_screen_t *screen = (downloading_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    if (!screen->progress_label) return;
    
    // Update "x kb / x kb" label
    char progress_text[32];
    snprintf(progress_text, sizeof(progress_text), "%d kb / %d kb", received_kb, total_kb);
    lv_label_set_text(screen->progress_label, progress_text);
    
    // Keep spinner centered mid-screen (no repositioning based on text)
}

/**
 * @brief Show/hide DOWNLOADING screen (public API)
 */
void screen_downloading_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// PLAYING SCREEN
// ============================================

/**
 * @brief Playing screen structure
 */
typedef struct {
    // Status bar elements (same as IDLE)
    lv_obj_t *status_bar;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_tip;
    lv_obj_t *battery_percent_label;
    lv_obj_t *charging_icon;
    lv_obj_t *wifi_bars[4];
    
    // UI elements
    lv_obj_t *text_box_container; // Container for text box (has background)
    lv_obj_t *text_box;          // Server response text (large, dynamic size)
    lv_obj_t *confirm_icon;      // Checkmark icon (top right, for CONFIRM mode)
    lv_obj_t *mic_icon;          // Microphone icon (top button area, for ASK/CONFIRM modes)
    lv_obj_t *title_label;       // "Playing" text (color changes per mode)
    lv_obj_t *time_label;        // "x / x Seconds" text
    lv_obj_t *spinner;           // Spinner (color changes per mode)
    lv_obj_t *stop_btn;          // Stop button (bottom right)
    
    // Event callback
    uploading_event_cb_t event_cb;  // Reuse uploading event type
} playing_screen_t;

/**
 * @brief Stop button event callback
 */
static void playing_stop_btn_event_cb(lv_event_t *e) {
    playing_screen_t *screen = (playing_screen_t*)lv_event_get_user_data(e);
    if (screen && screen->event_cb) {
        screen->event_cb(UPLOADING_EVENT_CANCEL_CLICKED);  // Reuse cancel event
    }
}

/**
 * @brief Create status bar for playing screen
 */
static void create_playing_status_bar(playing_screen_t *screen, lv_obj_t *parent) {
    // Status bar container (35px height, top of screen)
    screen->status_bar = lv_obj_create(parent);
    lv_obj_remove_style_all(screen->status_bar);
    lv_obj_set_size(screen->status_bar, SCREEN_WIDTH, 35);
    lv_obj_align(screen->status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(screen->status_bar, LV_OPA_TRANSP, 0);
    
    // === BATTERY INDICATOR ===
    screen->battery_bar = lv_bar_create(screen->status_bar);
    lv_obj_set_size(screen->battery_bar, 30, 15);
    lv_obj_align(screen->battery_bar, LV_ALIGN_LEFT_MID, 25, 0);
    lv_bar_set_range(screen->battery_bar, 0, 100);
    lv_bar_set_value(screen->battery_bar, 75, LV_ANIM_OFF);
    
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(screen->battery_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_bar, 1, 0);
    lv_obj_set_style_pad_all(screen->battery_bar, 2, 0);
    
    screen->battery_tip = lv_obj_create(screen->status_bar);
    lv_obj_set_size(screen->battery_tip, 3, 8);
    lv_obj_align_to(screen->battery_tip, screen->battery_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(screen->battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_tip, 0, 0);
    lv_obj_set_style_radius(screen->battery_tip, 0, 0);
    
    screen->battery_percent_label = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->battery_percent_label, "100%");
    lv_obj_set_style_text_color(screen->battery_percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(screen->battery_percent_label, screen->battery_tip, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    screen->charging_icon = lv_label_create(screen->status_bar);
    lv_label_set_text(screen->charging_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(screen->charging_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->charging_icon, &lv_font_montserrat_16, 0);
    lv_obj_align_to(screen->charging_icon, screen->battery_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    
    // === WiFi SIGNAL BARS ===
    const int bar_heights[] = {4, 8, 12, 16};
    const int bar_width = 3;
    const int bar_spacing = 2;
    
    for (int i = 0; i < 4; i++) {
        screen->wifi_bars[i] = lv_obj_create(screen->status_bar);
        lv_obj_set_size(screen->wifi_bars[i], bar_width, bar_heights[i]);
        lv_obj_align(screen->wifi_bars[i], LV_ALIGN_RIGHT_MID, 
                     -25 - (3 - i) * (bar_width + bar_spacing), 
                     (16 - bar_heights[i]) / 2);
        lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_width(screen->wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(screen->wifi_bars[i], 0, 0);
    }
}

/**
 * @brief Create UI elements for playing screen
 */
static void create_playing_ui_elements(playing_screen_t *screen, lv_obj_t *parent) {
    // === TEXT BOX CONTAINER (dark background box, centered on screen) ===
    screen->text_box_container = lv_obj_create(parent);
    lv_obj_set_size(screen->text_box_container, SCREEN_WIDTH - 40, LV_SIZE_CONTENT);  // Width: full - margins, height: auto
    lv_obj_set_style_bg_color(screen->text_box_container, lv_color_hex(0x1A1A1A), 0);  // Dark gray background
    lv_obj_set_style_bg_opa(screen->text_box_container, LV_OPA_90, 0);  // 90% opacity (semi-transparent)
    lv_obj_set_style_border_color(screen->text_box_container, lv_color_hex(0x404040), 0);  // Subtle border
    lv_obj_set_style_border_width(screen->text_box_container, 1, 0);
    lv_obj_set_style_radius(screen->text_box_container, 8, 0);  // Rounded corners
    lv_obj_set_style_pad_all(screen->text_box_container, 15, 0);  // 15px padding inside
    lv_obj_align(screen->text_box_container, LV_ALIGN_CENTER, 0, -10);  // Centered, slight upward offset
    
    // === TEXT BOX (Server response, inside container) ===
    screen->text_box = lv_label_create(screen->text_box_container);
    lv_label_set_text(screen->text_box, "");  // Empty initially
    lv_label_set_long_mode(screen->text_box, LV_LABEL_LONG_WRAP);  // Wrap text
    lv_obj_set_width(screen->text_box, SCREEN_WIDTH - 70);  // Slightly narrower than container for padding
    lv_obj_set_style_text_align(screen->text_box, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(screen->text_box, lv_color_white(), 0);
    // Start with largest font, will auto-scale down if needed
    lv_obj_set_style_text_font(screen->text_box, &lv_font_montserrat_48, 0);
    lv_obj_center(screen->text_box);  // Centered within container
    
    // Disable scrolling on label and container
    lv_obj_clear_flag(screen->text_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(screen->text_box_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // === MIC ICON (Top right button indicator, for ASK/CONFIRM modes, hidden by default) ===
    screen->mic_icon = icon_create_mic_small(parent, lv_color_white());
    lv_obj_align(screen->mic_icon, LV_ALIGN_TOP_RIGHT, -15, 45);  // Top right, below status bar
    lv_obj_add_flag(screen->mic_icon, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    
    // === CONFIRMATION ICON (Green checkmark, same position as recording screen, for CONFIRM mode, hidden by default) ===
    screen->confirm_icon = lv_label_create(parent);
    lv_label_set_text(screen->confirm_icon, LV_SYMBOL_OK);  // Checkmark
    lv_obj_set_style_text_font(screen->confirm_icon, &lv_font_montserrat_14, 0);  // Same size as recording screen
    lv_obj_set_style_text_color(screen->confirm_icon, lv_color_hex(0x00FF00), 0);  // Green (always green)
    lv_obj_align(screen->confirm_icon, LV_ALIGN_TOP_RIGHT, -5, 75);  // EXACT match to recording screen stop button
    lv_obj_add_flag(screen->confirm_icon, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    
    // === TITLE LABEL "Playing" (BRIGHTER GREEN) ===
    screen->title_label = lv_label_create(parent);
    lv_label_set_text(screen->title_label, "Playing");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0x00FF00), 0);  // Bright green (not turquoise)
    lv_obj_align(screen->title_label, LV_ALIGN_BOTTOM_LEFT, 25, -50);
    
    // === TIME LABEL "x / x Seconds" ===
    screen->time_label = lv_label_create(parent);
    lv_label_set_text(screen->time_label, "0 / 0 Seconds");
    lv_obj_set_style_text_font(screen->time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(screen->time_label, lv_color_white(), 0);
    lv_obj_align(screen->time_label, LV_ALIGN_BOTTOM_LEFT, 25, -20);
    
    // === SPINNER (Bright Green, dynamic position based on text width) ===
    screen->spinner = lv_spinner_create(parent, 1000, 90);
    lv_obj_set_size(screen->spinner, 40, 40);
    // Initial position: 20px spacing from label edge
    lv_obj_align_to(screen->spinner, screen->time_label, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    lv_obj_set_style_arc_color(screen->spinner, lv_color_hex(0x00FF00), LV_PART_INDICATOR);  // Bright green
    lv_obj_set_style_arc_width(screen->spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(screen->spinner, lv_color_hex(0x404040), LV_PART_MAIN);
    lv_obj_set_style_arc_width(screen->spinner, 4, LV_PART_MAIN);
    
    // === STOP BUTTON (Bottom right, black bg with red X) ===
    screen->stop_btn = lv_btn_create(parent);
    lv_obj_set_size(screen->stop_btn, 20, 20);
    lv_obj_align(screen->stop_btn, LV_ALIGN_BOTTOM_RIGHT, -5, -60);
    lv_obj_set_style_radius(screen->stop_btn, 0, 0);  // Square, not round
    lv_obj_set_style_bg_color(screen->stop_btn, lv_color_hex(0x000000), 0);  // Black background
    lv_obj_set_style_shadow_width(screen->stop_btn, 0, 0);
    lv_obj_add_event_cb(screen->stop_btn, playing_stop_btn_event_cb, LV_EVENT_CLICKED, screen);
    
    lv_obj_t *stop_label = lv_label_create(screen->stop_btn);
    lv_label_set_text(stop_label, LV_SYMBOL_CLOSE);  // X icon (stop playback)
    lv_obj_set_style_text_font(stop_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stop_label, lv_color_hex(0xFF0000), 0);  // Red X
    lv_obj_center(stop_label);
}

/**
 * @brief Screen delete callback
 */
static void screen_playing_delete_cb(lv_event_t *e) {
    playing_screen_t *screen = (playing_screen_t*)lv_event_get_user_data(e);
    if (screen) {
        lv_mem_free(screen);
    }
}

/**
 * @brief Create PLAYING screen (public API)
 */
lv_obj_t* screen_playing_create(lv_obj_t *parent, uploading_event_cb_t event_cb) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Allocate screen state
    playing_screen_t *screen = (playing_screen_t*)lv_mem_alloc(sizeof(playing_screen_t));
    LV_ASSERT_MALLOC(screen);
    memset(screen, 0, sizeof(playing_screen_t));
    screen->event_cb = event_cb;
    lv_obj_set_user_data(screen_obj, screen);
    
    // Register cleanup callback
    lv_obj_add_event_cb(screen_obj, screen_playing_delete_cb, LV_EVENT_DELETE, screen);
    
    // === STATUS BAR ===
    create_playing_status_bar(screen, screen_obj);
    
    // === UI ELEMENTS ===
    create_playing_ui_elements(screen, screen_obj);
    
    // === IMMEDIATE TEXT SET (covers blocking download timing) ===
    // If pending_response_text is available, set it immediately on screen creation
    extern String pending_response_text;
    // Set initial text if available (from pending_response_text global)
    if (pending_response_text.length() > 0) {
        screen_playing_update_text(screen_obj, pending_response_text.c_str());
    }
    
    return screen_obj;
}

/**
 * @brief Update status bar (public API)
 */
void screen_playing_update_status(lv_obj_t *screen_obj, const idle_status_t *status) {
    playing_screen_t *screen = (playing_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !status) return;
    
    lv_bar_set_value(screen->battery_bar, status->battery_percent, LV_ANIM_OFF);
    
    char battery_text[8];
    snprintf(battery_text, sizeof(battery_text), "%d%%", status->battery_percent);
    lv_label_set_text(screen->battery_percent_label, battery_text);
    
    if (status->battery_percent > 50) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    } else if (status->battery_percent > 20) {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFFFF00), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0xFF0000), LV_PART_INDICATOR);
    }
    
    if (status->battery_charging) {
        lv_obj_clear_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    }
    
    for (int i = 0; i < 4; i++) {
        if (status->wifi_connected && i < status->wifi_bars) {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_white(), 0);
        } else {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        }
    }
}

/**
 * @brief Update playback time (public API)
 */
void screen_playing_update_time(lv_obj_t *screen_obj, int current_sec, int total_sec) {
    playing_screen_t *screen = (playing_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    if (!screen->time_label) return;
    
    // Update "x / x Seconds" label (FIXED: was showing 0 due to format)
    char time_text[32];
    snprintf(time_text, sizeof(time_text), "%d / %d Seconds", current_sec, total_sec);
    lv_label_set_text(screen->time_label, time_text);
    
    // Reposition spinner dynamically based on text width (20px spacing)
    if (screen->spinner) {
        lv_obj_align_to(screen->spinner, screen->time_label, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
    }
}

/**
 * @brief Update text box with server response (public API)
 * 
 * @param screen_obj PLAYING screen object
 * @param text Server response text to display
 */
void screen_playing_update_text(lv_obj_t *screen_obj, const char* text) {
    playing_screen_t *screen = (playing_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !text) {
        Serial.printf("⚠️  screen_playing_update_text: NULL screen=%p text=%p\n", screen, text);
        return;
    }
    
    if (!screen->text_box) {
        Serial.println("⚠️  screen_playing_update_text: text_box is NULL!");
        return;
    }
    
    // Set text and auto-scale font
    lv_label_set_text(screen->text_box, text);
    
    size_t text_len = strlen(text);
    const lv_font_t* font;
    if (text_len > 150) {
        font = &lv_font_montserrat_16;
    } else if (text_len > 80) {
        font = &lv_font_montserrat_20;
    } else if (text_len > 40) {
        font = &lv_font_montserrat_32;
    } else {
        font = &lv_font_montserrat_48;
    }
    lv_obj_set_style_text_font(screen->text_box, font, 0);
    
    // Force refresh
    lv_obj_invalidate(screen->text_box);
    lv_obj_invalidate(screen->text_box_container);
}

/**
 * @brief Set playing screen mode (SAY/ASK/CONFIRM) with colors and icons
 * 
 * @param screen_obj PLAYING screen object
 * @param mode 0=SAY (blue), 1=ASK (yellow, mic only), 2=CONFIRM (purple, mic+checkmark)
 */
void screen_playing_set_mode(lv_obj_t *screen_obj, int mode) {
    playing_screen_t *screen = (playing_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    lv_color_t title_color;
    lv_color_t spinner_color;
    const char* title_text;
    
    // Mode: 0=SAY, 1=ASK, 2=CONFIRM
    switch (mode) {
        case 1:  // ASK (yellow, mic only)
            title_color = lv_color_hex(0xFFDD00);    // Bright yellow
            spinner_color = lv_color_hex(0xFFDD00);
            title_text = "Question";
            // Show mic icon only
            if (screen->mic_icon) lv_obj_clear_flag(screen->mic_icon, LV_OBJ_FLAG_HIDDEN);
            if (screen->confirm_icon) lv_obj_add_flag(screen->confirm_icon, LV_OBJ_FLAG_HIDDEN);
            break;
            
        case 2:  // CONFIRM (purple, mic + checkmark)
            title_color = lv_color_hex(0xAA00FF);    // Purple
            spinner_color = lv_color_hex(0xAA00FF);
            title_text = "Confirm";
            // Show both icons
            if (screen->mic_icon) lv_obj_clear_flag(screen->mic_icon, LV_OBJ_FLAG_HIDDEN);
            if (screen->confirm_icon) lv_obj_clear_flag(screen->confirm_icon, LV_OBJ_FLAG_HIDDEN);
            break;
            
        default:  // SAY (blue/green, no icons)
            title_color = lv_color_hex(0x00FF00);    // Bright green
            spinner_color = lv_color_hex(0x00FF00);
            title_text = "Playing";
            // Hide both icons
            if (screen->mic_icon) lv_obj_add_flag(screen->mic_icon, LV_OBJ_FLAG_HIDDEN);
            if (screen->confirm_icon) lv_obj_add_flag(screen->confirm_icon, LV_OBJ_FLAG_HIDDEN);
            break;
    }
    
    // Update title label
    if (screen->title_label) {
        lv_label_set_text(screen->title_label, title_text);
        lv_obj_set_style_text_color(screen->title_label, title_color, 0);
    }
    
    // Update spinner color
    if (screen->spinner) {
        lv_obj_set_style_arc_color(screen->spinner, spinner_color, LV_PART_INDICATOR);
    }
    
    // Update mic icon color to match theme
    if (screen->mic_icon) {
        lv_obj_set_style_text_color(screen->mic_icon, title_color, 0);
    }
    // Confirm icon stays green always (set at creation)
}

/**
 * @brief Show/hide PLAYING screen (public API)
 */
void screen_playing_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// BOOT SCREEN
// ============================================

typedef struct {
    lv_obj_t *loading_bar;
    lv_obj_t *bottom_text;
} boot_screen_t;

/**
 * @brief Create boot screen with loading bar
 */
lv_obj_t* screen_boot_create(lv_obj_t *parent) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Allocate screen data
    boot_screen_t *screen = (boot_screen_t*)lv_mem_alloc(sizeof(boot_screen_t));
    lv_obj_set_user_data(screen_obj, screen);
    
    // === LOGO/TITLE (top center) ===
    lv_obj_t *title = lv_label_create(screen_obj);
    lv_label_set_text(title, "ESP32-S3");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00AAFF), 0);  // Cyan blue
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);
    
    // === SUBTITLE ===
    lv_obj_t *subtitle = lv_label_create(screen_obj);
    lv_label_set_text(subtitle, "Touch AMOLED");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x808080), 0);  // Gray
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 105);
    
    // === LOADING BAR (center) ===
    screen->loading_bar = lv_bar_create(screen_obj);
    lv_obj_set_size(screen->loading_bar, 280, 8);
    lv_obj_center(screen->loading_bar);
    lv_bar_set_range(screen->loading_bar, 0, 100);
    lv_bar_set_value(screen->loading_bar, 0, LV_ANIM_OFF);
    
    // Style loading bar background
    lv_obj_set_style_bg_color(screen->loading_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen->loading_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(screen->loading_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(screen->loading_bar, 4, LV_PART_MAIN);
    
    // Style loading bar indicator (progress)
    lv_obj_set_style_bg_color(screen->loading_bar, lv_color_hex(0x00AAFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(screen->loading_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(screen->loading_bar, 4, LV_PART_INDICATOR);
    
    // === LOADING TEXT (below bar) ===
    lv_obj_t *loading_text = lv_label_create(screen_obj);
    lv_label_set_text(loading_text, "Initializing...");
    lv_obj_set_style_text_font(loading_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(loading_text, lv_color_hex(0x606060), 0);
    lv_obj_align_to(loading_text, screen->loading_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    
    // === BOTTOM TEXT (instruction) ===
    screen->bottom_text = lv_label_create(screen_obj);
    lv_label_set_text(screen->bottom_text, "hold for test / config ->");
    lv_obj_set_style_text_font(screen->bottom_text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(screen->bottom_text, lv_color_hex(0x808080), 0);
    lv_obj_align(screen->bottom_text, LV_ALIGN_BOTTOM_RIGHT, -30, -70);  // Moved up 50px
    
    // === VERSION TEXT (bottom left) ===
    lv_obj_t *version = lv_label_create(screen_obj);
    lv_label_set_text(version, "v1.0.0");
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(0x404040), 0);
    lv_obj_align(version, LV_ALIGN_BOTTOM_LEFT, 30, -70);  // Moved up 50px
    
    return screen_obj;
}

/**
 * @brief Update boot screen loading progress
 */
void screen_boot_update_progress(lv_obj_t *screen_obj, int progress) {
    boot_screen_t *screen = (boot_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Clamp progress to 0-100
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;
    
    lv_bar_set_value(screen->loading_bar, progress, LV_ANIM_ON);
}

/**
 * @brief Show/hide boot screen
 */
void screen_boot_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// SLEEP SCREEN
// ============================================

/**
 * @brief Sleep screen structure
 */
typedef struct {
    lv_obj_t *time_label;    // Big clock display (HH:MM)
    lv_obj_t *date_label;    // Date below clock
    lv_obj_t *wake_label;    // "Shake to wake" instruction
} sleep_screen_t;

/**
 * @brief Create sleep screen with clock
 */
lv_obj_t* screen_sleep_create(lv_obj_t *parent) {
    // Allocate screen data
    sleep_screen_t *screen = (sleep_screen_t*)lv_mem_alloc(sizeof(sleep_screen_t));
    if (!screen) return NULL;
    memset(screen, 0, sizeof(sleep_screen_t));
    
    // Create container
    lv_obj_t *screen_obj = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_set_user_data(screen_obj, screen);
    
    // Time label (big stylish clock with seconds) - centered
    screen->time_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->time_label, "00:00:00");
    lv_obj_set_style_text_font(screen->time_label, &lv_font_montserrat_48, 0);  // 48pt - large and visible
    lv_obj_set_style_text_color(screen->time_label, lv_color_white(), 0);
    lv_obj_align(screen->time_label, LV_ALIGN_CENTER, 0, -60);  // Moved up more for date space
    
    // Date label (below clock, same size as time)
    screen->date_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->date_label, "");
    lv_obj_set_style_text_font(screen->date_label, &lv_font_montserrat_48, 0);  // 48pt - same as time
    lv_obj_set_style_text_color(screen->date_label, lv_color_hex(0x808080), 0);  // Gray
    lv_obj_align(screen->date_label, LV_ALIGN_CENTER, 0, 20);  // Closer to time
    
    // "Shake to wake" instruction
    screen->wake_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->wake_label, "Shake to wake");
    lv_obj_set_style_text_font(screen->wake_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(screen->wake_label, lv_color_hex(0x606060), 0);  // Dim gray
    lv_obj_align(screen->wake_label, LV_ALIGN_BOTTOM_MID, 0, -50);
    
    return screen_obj;
}

/**
 * @brief Update sleep screen clock
 */
void screen_sleep_update_time(lv_obj_t *screen_obj, const char* time_str, const char* date_str) {
    sleep_screen_t *screen = (sleep_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Update time
    if (time_str) {
        lv_label_set_text(screen->time_label, time_str);
    }
    
    // Update date (optional)
    if (date_str && screen->date_label) {
        lv_label_set_text(screen->date_label, date_str);
    }
}

/**
 * @brief Show/hide sleep screen
 */
void screen_sleep_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// SETTINGS SCREEN
// ============================================

/**
 * @brief Settings screen structure
 */
typedef struct {
    lv_obj_t *title_label;          // "Settings" title
    lv_obj_t *brightness_slider;    // Brightness slider
    lv_obj_t *brightness_label;     // Brightness label
    lv_obj_t *volume_slider;        // Volume slider
    lv_obj_t *volume_label;         // Volume label
    lv_obj_t *mic_gain_slider;      // Mic gain slider
    lv_obj_t *mic_gain_label;       // Mic gain label
    lv_obj_t *close_btn;            // Red X close button
    settings_event_cb_t event_cb;   // Event callback
} settings_screen_t;

/**
 * @brief Slider event callback
 */
static void settings_slider_event_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    lv_obj_t *screen_obj = (lv_obj_t*)lv_event_get_user_data(e);
    settings_screen_t *screen = (settings_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !screen->event_cb) return;
    
    int value = lv_slider_get_value(slider);
    settings_event_data_t event_data;
    event_data.value = value;
    
    // Determine which slider
    if (slider == screen->brightness_slider) {
        event_data.event = SETTINGS_EVENT_BRIGHTNESS;
    } else if (slider == screen->volume_slider) {
        event_data.event = SETTINGS_EVENT_VOLUME;
    } else if (slider == screen->mic_gain_slider) {
        event_data.event = SETTINGS_EVENT_MIC_GAIN;
    } else {
        return;
    }
    
    screen->event_cb(event_data);
}

/**
 * @brief Close button event callback
 */
static void settings_close_btn_event_cb(lv_event_t *e) {
    lv_obj_t *screen_obj = (lv_obj_t*)lv_event_get_user_data(e);
    settings_screen_t *screen = (settings_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !screen->event_cb) return;
    
    settings_event_data_t event_data;
    event_data.event = SETTINGS_EVENT_CLOSE;
    event_data.value = 0;
    screen->event_cb(event_data);
}

/**
 * @brief Create settings screen with sliders
 */
lv_obj_t* screen_settings_create(lv_obj_t *parent, settings_event_cb_t event_cb) {
    // Allocate screen data
    settings_screen_t *screen = (settings_screen_t*)lv_mem_alloc(sizeof(settings_screen_t));
    if (!screen) return NULL;
    memset(screen, 0, sizeof(settings_screen_t));
    screen->event_cb = event_cb;
    
    // Create container
    lv_obj_t *screen_obj = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_set_user_data(screen_obj, screen);
    
    // Title "Settings" (top center)
    screen->title_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->title_label, "Settings");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_white(), 0);
    lv_obj_align(screen->title_label, LV_ALIGN_TOP_MID, 0, 40);
    
    // Boot menu blue color (#667eea) for sliders
    lv_color_t slider_color = lv_color_hex(0x667eea);
    
    // === BRIGHTNESS SLIDER ===
    screen->brightness_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->brightness_label, "Brightness");
    lv_obj_set_style_text_font(screen->brightness_label, &lv_font_montserrat_16, 0);  // Use 16pt (18 not available)
    lv_obj_set_style_text_color(screen->brightness_label, lv_color_white(), 0);
    lv_obj_align(screen->brightness_label, LV_ALIGN_TOP_LEFT, 40, 120);
    
    screen->brightness_slider = lv_slider_create(screen_obj);
    lv_obj_set_size(screen->brightness_slider, 280, 20);
    lv_slider_set_range(screen->brightness_slider, 0, 100);
    lv_slider_set_value(screen->brightness_slider, 70, LV_ANIM_OFF);
    lv_obj_align(screen->brightness_slider, LV_ALIGN_TOP_MID, 0, 155);
    lv_obj_set_style_bg_color(screen->brightness_slider, slider_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(screen->brightness_slider, slider_color, LV_PART_KNOB);
    lv_obj_add_event_cb(screen->brightness_slider, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, screen_obj);
    
    // === VOLUME SLIDER ===
    screen->volume_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->volume_label, "Volume");
    lv_obj_set_style_text_font(screen->volume_label, &lv_font_montserrat_16, 0);  // Use 16pt (18 not available)
    lv_obj_set_style_text_color(screen->volume_label, lv_color_white(), 0);
    lv_obj_align(screen->volume_label, LV_ALIGN_TOP_LEFT, 40, 210);
    
    screen->volume_slider = lv_slider_create(screen_obj);
    lv_obj_set_size(screen->volume_slider, 280, 20);
    lv_slider_set_range(screen->volume_slider, 0, 100);
    lv_slider_set_value(screen->volume_slider, 50, LV_ANIM_OFF);
    lv_obj_align(screen->volume_slider, LV_ALIGN_TOP_MID, 0, 245);
    lv_obj_set_style_bg_color(screen->volume_slider, slider_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(screen->volume_slider, slider_color, LV_PART_KNOB);
    lv_obj_add_event_cb(screen->volume_slider, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, screen_obj);
    
    // === MIC GAIN SLIDER ===
    screen->mic_gain_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->mic_gain_label, "Mic Gain");
    lv_obj_set_style_text_font(screen->mic_gain_label, &lv_font_montserrat_16, 0);  // Use 16pt (18 not available)
    lv_obj_set_style_text_color(screen->mic_gain_label, lv_color_white(), 0);
    lv_obj_align(screen->mic_gain_label, LV_ALIGN_TOP_LEFT, 40, 300);
    
    screen->mic_gain_slider = lv_slider_create(screen_obj);
    lv_obj_set_size(screen->mic_gain_slider, 280, 20);
    lv_slider_set_range(screen->mic_gain_slider, 0, 7);  // 0-7 range
    lv_slider_set_value(screen->mic_gain_slider, 4, LV_ANIM_OFF);
    lv_obj_align(screen->mic_gain_slider, LV_ALIGN_TOP_MID, 0, 335);
    lv_obj_set_style_bg_color(screen->mic_gain_slider, slider_color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(screen->mic_gain_slider, slider_color, LV_PART_KNOB);
    lv_obj_add_event_cb(screen->mic_gain_slider, settings_slider_event_cb, LV_EVENT_VALUE_CHANGED, screen_obj);
    
    // === CLOSE BUTTON (Red X, bottom-right, above FPS counter) ===
    screen->close_btn = lv_btn_create(screen_obj);
    lv_obj_set_size(screen->close_btn, 30, 30);
    lv_obj_align(screen->close_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -55);  // Above FPS counter
    lv_obj_set_style_bg_color(screen->close_btn, lv_color_hex(0xFF0000), 0);  // Red
    lv_obj_set_style_radius(screen->close_btn, 15, 0);  // Circular
    lv_obj_add_event_cb(screen->close_btn, settings_close_btn_event_cb, LV_EVENT_CLICKED, screen_obj);
    
    // X label on button
    lv_obj_t *x_label = lv_label_create(screen->close_btn);
    lv_label_set_text(x_label, LV_SYMBOL_CLOSE);  // X symbol
    lv_obj_set_style_text_color(x_label, lv_color_white(), 0);
    lv_obj_center(x_label);
    
    return screen_obj;
}

/**
 * @brief Set initial values for settings sliders
 */
void screen_settings_set_values(lv_obj_t *screen_obj, int brightness, int volume, int mic_gain) {
    settings_screen_t *screen = (settings_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Set slider values (without triggering events)
    lv_slider_set_value(screen->brightness_slider, brightness, LV_ANIM_OFF);
    lv_slider_set_value(screen->volume_slider, volume, LV_ANIM_OFF);
    lv_slider_set_value(screen->mic_gain_slider, mic_gain, LV_ANIM_OFF);
}

/**
 * @brief Show/hide settings screen
 */
void screen_settings_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================
// PLACEHOLDER SCREENS
// ============================================

/**
 * @brief Placeholder screen structure
 */
typedef struct {
    lv_obj_t *title_label;
    lv_obj_t *subtitle_label;
} placeholder_screen_t;

/**
 * @brief Create simple placeholder screen
 */
lv_obj_t* screen_placeholder_create(lv_obj_t *parent, const char* title, const char* subtitle) {
    // Allocate screen data
    placeholder_screen_t *screen = (placeholder_screen_t*)lv_mem_alloc(sizeof(placeholder_screen_t));
    if (!screen) return NULL;
    memset(screen, 0, sizeof(placeholder_screen_t));
    
    // Create container
    lv_obj_t *screen_obj = lv_obj_create(parent ? parent : lv_scr_act());
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(screen_obj, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_set_user_data(screen_obj, screen);
    
    // Title label (large, centered)
    screen->title_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->title_label, title);
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0x00FF00), 0);  // Green
    lv_obj_align(screen->title_label, LV_ALIGN_CENTER, 0, -30);
    
    // Subtitle label (smaller, below title)
    if (subtitle && strlen(subtitle) > 0) {
        screen->subtitle_label = lv_label_create(screen_obj);
        lv_label_set_text(screen->subtitle_label, subtitle);
        lv_obj_set_style_text_font(screen->subtitle_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(screen->subtitle_label, lv_color_hex(0x808080), 0);  // Gray
        lv_obj_align(screen->subtitle_label, LV_ALIGN_CENTER, 0, 20);
    }
    
    return screen_obj;
}

/**
 * @brief Update placeholder screen text
 */
void screen_placeholder_update(lv_obj_t *screen_obj, const char* title, const char* subtitle) {
    placeholder_screen_t *screen = (placeholder_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    if (title) {
        lv_label_set_text(screen->title_label, title);
    }
    
    if (subtitle && screen->subtitle_label) {
        lv_label_set_text(screen->subtitle_label, subtitle);
    }
}

/**
 * @brief Show/hide placeholder screen
 */
void screen_placeholder_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}
