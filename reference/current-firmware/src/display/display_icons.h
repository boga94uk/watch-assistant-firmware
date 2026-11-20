/**
 * @file display_icons.h
 * @brief Custom icon drawing utilities
 * 
 * Provides canvas-based icon drawing for:
 * - Microphone icons (small and large versions)
 * - Other custom UI icons
 */

#ifndef DISPLAY_ICONS_H
#define DISPLAY_ICONS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a small microphone icon (20x20px)
 * 
 * @param parent Parent object
 * @param color Icon color (RGB565)
 * @return lv_obj_t* Canvas object with mic icon
 */
lv_obj_t* icon_create_mic_small(lv_obj_t *parent, lv_color_t color);

/**
 * @brief Create a large microphone icon (80x100px for center of screen)
 * 
 * @param parent Parent object
 * @param color Icon color (RGB565)
 * @return lv_obj_t* Canvas object with large mic icon
 */
lv_obj_t* icon_create_mic_large(lv_obj_t *parent, lv_color_t color);

/**
 * @brief Update icon color
 * 
 * @param icon Icon canvas object
 * @param color New color
 */
void icon_set_color(lv_obj_t *icon, lv_color_t color);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_ICONS_H
