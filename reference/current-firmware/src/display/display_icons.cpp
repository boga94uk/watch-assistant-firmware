/**
 * @file display_icons.cpp
 * @brief Custom icon drawing implementation
 * 
 * Canvas-based icon rendering inspired by Device_Rom_Complete project
 */

#include "display_icons.h"
#include <stdlib.h>

// ============================================
// SMALL MIC ICON (20x20px) - For button indicators
// ============================================
/**
 * Original geometry from Device_Rom_Complete:
 * - Circle at (x, y-5), radius 8
 * - Rect from (x-8, y-5) to (x+8, y+7)  → 16w × 12h
 * - Stem from (x-2, y+7) to (x+2, y+15) → 4w × 8h
 * - Base from (x-6, y+15) to (x+6, y+18) → 12w × 3h
 * 
 * Scaled for 20x20 canvas with center at (10, 10):
 */
lv_obj_t* icon_create_mic_small(lv_obj_t *parent, lv_color_t color) {
    // Create canvas (20x20 pixels, ARGB8888 for transparency)
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_obj_set_size(canvas, 20, 20);
    
    // Allocate buffer (20 × 20 × 4 bytes = 1600 bytes)
    static lv_color_t cbuf[20 * 20];
    lv_canvas_set_buffer(canvas, cbuf, 20, 20, LV_IMG_CF_TRUE_COLOR);
    
    // Clear to transparent
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);
    
    // Set draw color
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = color;
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;
    
    // Mic capsule top (circle)
    rect_dsc.radius = 8;
    lv_canvas_draw_rect(canvas, 2, 0, 16, 16, &rect_dsc);
    
    // Mic capsule body (rounded rectangle)
    rect_dsc.radius = 0;
    lv_canvas_draw_rect(canvas, 2, 6, 16, 10, &rect_dsc);
    
    // Mic stem (vertical bar)
    lv_canvas_draw_rect(canvas, 8, 12, 4, 6, &rect_dsc);
    
    // Mic base (horizontal bar)
    lv_canvas_draw_rect(canvas, 4, 18, 12, 2, &rect_dsc);
    
    return canvas;
}

// ============================================
// LARGE MIC ICON (80x100px) - For center of screen
// ============================================
/**
 * Large microphone for recording screen center
 * Scaled 4× from small version
 */
lv_obj_t* icon_create_mic_large(lv_obj_t *parent, lv_color_t color) {
    // Create canvas (80x100 pixels)
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_obj_set_size(canvas, 80, 100);
    
    // Allocate buffer (80 × 100 × 4 bytes = 32000 bytes = 31.25KB)
    static lv_color_t cbuf_large[80 * 100];
    lv_canvas_set_buffer(canvas, cbuf_large, 80, 100, LV_IMG_CF_TRUE_COLOR);
    
    // Clear to transparent
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);
    
    // Set draw color
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = color;
    rect_dsc.bg_opa = LV_OPA_COVER;
    rect_dsc.border_width = 0;
    
    // Mic capsule top (rounded rectangle, acts like circle)
    rect_dsc.radius = 30;
    lv_canvas_draw_rect(canvas, 10, 0, 60, 60, &rect_dsc);
    
    // Mic capsule body (rounded rectangle)
    rect_dsc.radius = 15;
    lv_canvas_draw_rect(canvas, 10, 20, 60, 45, &rect_dsc);
    
    // Mic stem (vertical bar)
    rect_dsc.radius = 0;
    lv_canvas_draw_rect(canvas, 30, 50, 20, 30, &rect_dsc);
    
    // Mic base (horizontal bar)
    lv_canvas_draw_rect(canvas, 16, 80, 48, 8, &rect_dsc);
    
    return canvas;
}

// ============================================
// UPDATE ICON COLOR
// ============================================
void icon_set_color(lv_obj_t *icon, lv_color_t color) {
    // Redraw canvas with new color
    // Note: This is a simplified version - for proper color change,
    // we'd need to redraw all shapes with new color
    // For now, just changing the canvas background tint
    lv_obj_set_style_img_recolor(icon, color, 0);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_50, 0);
}
