/**
 * @file test_mode_screen.cpp
 * @brief Test mode screen implementation with WiFi/Lock/Gear animations
 */

#include "display/display_screens.h"
#include <math.h>

// Test mode screen UI components
typedef struct {
    lv_obj_t *header_label;      // "Test / Config mode"
    lv_obj_t *footer_label;      // "Hold to power off ->"
    lv_obj_t *content_area;      // Main content area for animations
    lv_obj_t *wifi_container;    // WiFi icon + text container
    lv_obj_t *lock_container;    // Padlock container
    lv_obj_t *gear_container;    // Gear container
    test_mode_state_t current_state;
    uint16_t gear_rotation;      // Gear rotation angle (0-359)
    lv_timer_t *anim_timer;      // Animation timer
} testmode_screen_t;

// Forward declarations
static void draw_wifi_icon(lv_obj_t *parent);
static void draw_padlock(lv_obj_t *parent);
static void draw_gear(lv_obj_t *parent, uint16_t rotation);
static void testmode_anim_callback(lv_timer_t *timer);

/**
 * @brief Create test mode screen
 */
lv_obj_t* screen_testmode_create(lv_obj_t *parent) {
    // Allocate screen data
    testmode_screen_t *screen = (testmode_screen_t*)lv_mem_alloc(sizeof(testmode_screen_t));
    if (!screen) return NULL;
    
    memset(screen, 0, sizeof(testmode_screen_t));
    screen->current_state = TEST_MODE_WAIT;
    screen->gear_rotation = 0;
    
    // Create main screen object
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, 368, 448);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Store screen data
    lv_obj_set_user_data(screen_obj, screen);
    
    // === HEADER: "Test / Config mode" ===
    screen->header_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->header_label, "Test / Config mode");
    lv_obj_set_style_text_font(screen->header_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(screen->header_label, lv_color_hex(0x00FF00), 0); // Green (matches WiFi)
    lv_obj_align(screen->header_label, LV_ALIGN_TOP_MID, 0, 15);
    
    // === FOOTER: "Hold to power off ->" ===
    screen->footer_label = lv_label_create(screen_obj);
    lv_label_set_text(screen->footer_label, "Hold to power off ->");
    lv_obj_set_style_text_font(screen->footer_label, &lv_font_montserrat_20, 0); // 30% bigger (16->20)
    lv_obj_set_style_text_color(screen->footer_label, lv_color_hex(0xFF0000), 0); // Vivid red
    lv_obj_align(screen->footer_label, LV_ALIGN_BOTTOM_MID, 0, -35); // 20px up (-15 -> -35)
    
    // === CONTENT AREA (for WiFi/Lock/Gear) ===
    screen->content_area = lv_obj_create(screen_obj);
    lv_obj_remove_style_all(screen->content_area);
    lv_obj_set_size(screen->content_area, 300, 280);
    lv_obj_center(screen->content_area);
    lv_obj_set_style_bg_opa(screen->content_area, LV_OPA_TRANSP, 0);
    
    // Create containers for each state (initially hidden)
    screen->wifi_container = lv_obj_create(screen->content_area);
    lv_obj_remove_style_all(screen->wifi_container);
    lv_obj_set_size(screen->wifi_container, 300, 280);
    lv_obj_center(screen->wifi_container);
    
    screen->lock_container = lv_obj_create(screen->content_area);
    lv_obj_remove_style_all(screen->lock_container);
    lv_obj_set_size(screen->lock_container, 300, 280);
    lv_obj_center(screen->lock_container);
    lv_obj_add_flag(screen->lock_container, LV_OBJ_FLAG_HIDDEN);
    
    screen->gear_container = lv_obj_create(screen->content_area);
    lv_obj_remove_style_all(screen->gear_container);
    lv_obj_set_size(screen->gear_container, 300, 280);
    lv_obj_center(screen->gear_container);
    lv_obj_add_flag(screen->gear_container, LV_OBJ_FLAG_HIDDEN);
    
    // Draw initial state (WiFi waiting)
    draw_wifi_icon(screen->wifi_container);
    draw_padlock(screen->lock_container);
    
    // Create animation timer (for gear rotation, 33ms = ~30 FPS)
    screen->anim_timer = lv_timer_create(testmode_anim_callback, 33, screen_obj);
    
    return screen_obj;
}

/**
 * @brief Update test mode screen state
 */
void screen_testmode_update_state(lv_obj_t *screen_obj, test_mode_state_t state) {
    testmode_screen_t *screen = (testmode_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || screen->current_state == state) return;
    
    screen->current_state = state;
    
    // Hide all containers
    lv_obj_add_flag(screen->wifi_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen->lock_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen->gear_container, LV_OBJ_FLAG_HIDDEN);
    
    // Show appropriate container
    switch (state) {
        case TEST_MODE_WAIT:
            lv_obj_clear_flag(screen->wifi_container, LV_OBJ_FLAG_HIDDEN);
            break;
        case TEST_MODE_LOCKED:
            lv_obj_clear_flag(screen->lock_container, LV_OBJ_FLAG_HIDDEN);
            break;
        case TEST_MODE_UNLOCKED:
            lv_obj_clear_flag(screen->gear_container, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

/**
 * @brief Show/hide test mode screen
 */
void screen_testmode_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Draw WiFi icon (waiting for connection screen)
 */
static void draw_wifi_icon(lv_obj_t *parent) {
    // "Connect to AP" text
    lv_obj_t *label1 = lv_label_create(parent);
    lv_label_set_text(label1, "Connect to AP");
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label1, lv_color_white(), 0);
    lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 20);
    
    // WiFi icon using arcs (3 nested quarter-circle arcs)
    lv_obj_t *wifi_arcs[3];
    int radii[] = {25, 40, 55};
    
    for (int i = 0; i < 3; i++) {
        wifi_arcs[i] = lv_arc_create(parent);
        lv_obj_remove_style_all(wifi_arcs[i]);
        lv_obj_set_size(wifi_arcs[i], radii[i] * 2, radii[i] * 2);
        lv_obj_center(wifi_arcs[i]);
        // Quarter circle: 225° to 315° (bottom center arc, like WiFi symbol)
        lv_arc_set_bg_angles(wifi_arcs[i], 225, 315);
        lv_arc_set_value(wifi_arcs[i], 100);
        lv_arc_set_range(wifi_arcs[i], 0, 100);
        lv_obj_set_style_arc_color(wifi_arcs[i], lv_color_hex(0x00FF00), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(wifi_arcs[i], 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(wifi_arcs[i], true, LV_PART_INDICATOR);
        lv_obj_remove_style(wifi_arcs[i], NULL, LV_PART_KNOB);
        lv_obj_clear_flag(wifi_arcs[i], LV_OBJ_FLAG_CLICKABLE);
    }
    
    // WiFi dot (bottom center)
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, 10); // 20px up (30 -> 10)
    
    // "ESP ASSISTANT" text (large, green)
    lv_obj_t *label2 = lv_label_create(parent);
    lv_label_set_text(label2, "ESP ASSISTANT");
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label2, lv_color_hex(0x00FF00), 0); // Green
    lv_obj_align(label2, LV_ALIGN_BOTTOM_MID, 0, -35); // 15px up (-20 -> -35)
}

/**
 * @brief Draw padlock (locked screen) - improved realistic look
 */
static void draw_padlock(lv_obj_t *parent) {
    // "Click to unlock ->" text
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "Click to unlock    ->");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFF00), 0); // Yellow
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
    
    // Padlock body (rectangle with better proportions) - DRAW FIRST
    lv_obj_t *lock_body = lv_obj_create(parent);
    lv_obj_remove_style_all(lock_body);
    lv_obj_set_size(lock_body, 80, 90);
    lv_obj_set_style_bg_color(lock_body, lv_color_hex(0xFFD700), 0); // Gold
    lv_obj_set_style_bg_opa(lock_body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lock_body, 10, 0);
    lv_obj_set_style_border_color(lock_body, lv_color_hex(0xDAA520), 0); // Darker gold
    lv_obj_set_style_border_width(lock_body, 3, 0);
    lv_obj_align(lock_body, LV_ALIGN_CENTER, 0, 20);
    
    // Shackle attachment points (where shackle enters body)
    lv_obj_t *attach_left = lv_obj_create(lock_body);
    lv_obj_remove_style_all(attach_left);
    lv_obj_set_size(attach_left, 12, 20);
    lv_obj_set_style_bg_color(attach_left, lv_color_hex(0xDAA520), 0);
    lv_obj_set_style_bg_opa(attach_left, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(attach_left, 3, 0);
    lv_obj_align(attach_left, LV_ALIGN_TOP_LEFT, 10, 0);
    
    lv_obj_t *attach_right = lv_obj_create(lock_body);
    lv_obj_remove_style_all(attach_right);
    lv_obj_set_size(attach_right, 12, 20);
    lv_obj_set_style_bg_color(attach_right, lv_color_hex(0xDAA520), 0);
    lv_obj_set_style_bg_opa(attach_right, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(attach_right, 3, 0);
    lv_obj_align(attach_right, LV_ALIGN_TOP_RIGHT, -10, 0);
    
    // Keyhole (larger circle + wider slot)
    lv_obj_t *keyhole = lv_obj_create(lock_body);
    lv_obj_remove_style_all(keyhole);
    lv_obj_set_size(keyhole, 12, 12);
    lv_obj_set_style_bg_color(keyhole, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(keyhole, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(keyhole, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(keyhole, LV_ALIGN_CENTER, 0, -5);
    
    lv_obj_t *keyhole_slot = lv_obj_create(lock_body);
    lv_obj_remove_style_all(keyhole_slot);
    lv_obj_set_size(keyhole_slot, 6, 20);
    lv_obj_set_style_bg_color(keyhole_slot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(keyhole_slot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(keyhole_slot, 2, 0);
    lv_obj_align(keyhole_slot, LV_ALIGN_CENTER, 0, 10);
    
    // Padlock shackle (arc - upper part of lock) - DRAW LAST so it appears on top
    lv_obj_t *shackle = lv_arc_create(parent);
    lv_obj_remove_style_all(shackle);
    lv_obj_set_size(shackle, 70, 70);
    lv_obj_align(shackle, LV_ALIGN_CENTER, 0, -20);  // Position above body
    lv_arc_set_bg_angles(shackle, 180, 360);  // Top half (correct orientation)
    lv_arc_set_value(shackle, 100);
    lv_arc_set_range(shackle, 0, 100);
    lv_obj_set_style_arc_color(shackle, lv_color_hex(0xFFD700), LV_PART_INDICATOR); // Gold
    lv_obj_set_style_arc_width(shackle, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(shackle, true, LV_PART_INDICATOR);
    lv_obj_remove_style(shackle, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(shackle, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief Draw rotating gear (unlocked screen) - solid white teeth
 */
static void draw_gear(lv_obj_t *parent, uint16_t rotation) {
    // Clear previous gear
    lv_obj_clean(parent);
    
    // Gear parameters
    int gearX = 150;
    int gearY = 140;
    int innerR = 45;   // Inner radius (base of teeth)
    int outerR = 75;   // Outer radius (tips of teeth)
    int teethCount = 12;
    int holeR = 18;    // Center hole radius
    int boltR = 30;    // Bolt hole radius
    
    // Draw main gear body (inner circle)
    lv_obj_t *gear_body = lv_obj_create(parent);
    lv_obj_remove_style_all(gear_body);
    lv_obj_set_size(gear_body, innerR * 2, innerR * 2);
    lv_obj_set_style_bg_color(gear_body, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(gear_body, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(gear_body, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(gear_body, gearX - innerR, gearY - innerR);
    
    // Draw 12 solid white teeth extending from inner circle
    for (int i = 0; i < teethCount; i++) {
        float baseAngle = rotation + i * (360.0f / teethCount);
        float angleRad = baseAngle * 0.0174533f;
        
        // Create solid white tooth (rectangle pointing outward)
        lv_obj_t *tooth = lv_obj_create(parent);
        lv_obj_remove_style_all(tooth);
        
        // Tooth dimensions
        int tooth_width = 12;  // Width of tooth
        int tooth_length = outerR - innerR + 5;  // Length from inner edge to outer edge
        lv_obj_set_size(tooth, tooth_width, tooth_length);
        lv_obj_set_style_bg_color(tooth, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(tooth, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tooth, 0, 0);  // Sharp edges
        
        // Position tooth at inner edge, pointing outward
        int xPos = gearX + (int)(cosf(angleRad) * innerR);
        int yPos = gearY + (int)(sinf(angleRad) * innerR);
        lv_obj_set_pos(tooth, xPos - tooth_width/2, yPos - tooth_length/2);
        
        // Rotate tooth to point radially outward
        lv_obj_set_style_transform_angle(tooth, (int)(baseAngle * 10), 0);  // 0.1° units
        lv_obj_set_style_transform_pivot_x(tooth, tooth_width/2, 0);
        lv_obj_set_style_transform_pivot_y(tooth, tooth_length/2, 0);
    }
    
    // Center hole (black circle with gray outline)
    lv_obj_t *hole = lv_obj_create(parent);
    lv_obj_remove_style_all(hole);
    lv_obj_set_size(hole, holeR * 2, holeR * 2);
    lv_obj_set_style_bg_color(hole, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(hole, lv_color_hex(0x808080), 0);
    lv_obj_set_style_border_width(hole, 2, 0);
    lv_obj_set_pos(hole, gearX - holeR, gearY - holeR);
    
    // Draw 6 bolt holes (inside gear body at radius 30)
    for (int i = 0; i < 6; i++) {
        float angle = (rotation + i * 60) * 0.0174533f;
        int x = gearX + (int)(cosf(angle) * boltR);
        int y = gearY + (int)(sinf(angle) * boltR);
        
        lv_obj_t *bolt = lv_obj_create(parent);
        lv_obj_remove_style_all(bolt);
        lv_obj_set_size(bolt, 9, 9);
        lv_obj_set_style_bg_color(bolt, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(bolt, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bolt, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(bolt, lv_color_hex(0x808080), 0);
        lv_obj_set_style_border_width(bolt, 2, 0);
        lv_obj_set_pos(bolt, x - 4, y - 4);
    }
}

/**
 * @brief Animation timer callback (for gear rotation)
 */
static void testmode_anim_callback(lv_timer_t *timer) {
    lv_obj_t *screen_obj = (lv_obj_t*)timer->user_data;
    testmode_screen_t *screen = (testmode_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Only animate gear if in unlocked state
    if (screen->current_state == TEST_MODE_UNLOCKED) {
        screen->gear_rotation = (screen->gear_rotation + 3) % 360;  // Rotate 3° per frame
        draw_gear(screen->gear_container, screen->gear_rotation);
    }
}
