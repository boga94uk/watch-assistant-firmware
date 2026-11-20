/**
 * @file screen_queue.cpp
 * @brief Queue screen implementation - Brown themed screen for queued recordings
 */

#include "display_screens.h"
#include <Arduino.h>
#include "display/display_icons.h"
#include <vector>
#include "../upload_queue.h"  // For QueueItemInfo and queue_get_items_info()

// Screen dimensions
#define SCREEN_WIDTH 368
#define SCREEN_HEIGHT 448

// Queue screen structure
typedef struct {
    // Status bar
    lv_obj_t *battery_bar;
    lv_obj_t *battery_percent_label;
    lv_obj_t *charging_icon;
    lv_obj_t *wifi_bars[4];
    
    // UI elements
    lv_obj_t *title_label;           // "QUEUE" title
    lv_obj_t *list_container;        // Container for scrollable list
    lv_obj_t *list_labels[10];       // Text labels for each row (max 10 items)
    lv_obj_t *selection_pointer;     // Brown ► pointer
    lv_obj_t *scrollbar_indicator;   // Visual scrollbar on right side
    
    // Touch buttons (redesigned)
    lv_obj_t *btn_up;                // Up arrow button
    lv_obj_t *btn_play;              // Play/Stop button
    lv_obj_t *btn_down;              // Down arrow button
    lv_obj_t *icon_up;               // ↑ icon
    lv_obj_t *icon_play;             // ▶/⏸ icon
    lv_obj_t *icon_down;             // ↓ icon
    
    // Physical button indicators (not touchable)
    lv_obj_t *top_button_indicator;  // Record + Upload icons (stacked)
    lv_obj_t *bottom_button_indicator; // Delete icon
    
    lv_obj_t *progress_bar;          // Playback progress bar (thin brown, below buttons)
    
    queue_event_cb_t event_cb;
    bool wifi_connected;             // Track WiFi state for button visibility
    int selected_index;              // Currently selected item (0="All", 1+=recordings)
    int item_count;                  // Total number of items in list
    int window_offset;               // First visible item in window (for scrolling)
    bool is_playing;                 // Track playback state for Play/Stop icon
    std::vector<QueueItemInfo> items; // Store queue items for selection
} queue_screen_t;

/**
 * @brief Forward declarations
 */
static void update_queue_display_window(queue_screen_t *screen);

/**
 * @brief Cleanup callback when screen is deleted
 */
static void screen_queue_delete_cb(lv_event_t *e) {
    queue_screen_t *screen = (queue_screen_t*)lv_event_get_user_data(e);
    if (screen) {
        lv_mem_free(screen);
    }
}

/**
 * @brief Create status bar (EXACT COPY of IDLE screen)
 */
static void create_queue_status_bar(queue_screen_t *screen, lv_obj_t *parent) {
    // === BATTERY INDICATOR (EXACT COPY) ===
    // Battery bar (horizontal bar) - 15px from left edge (25px - 10px offset)
    screen->battery_bar = lv_bar_create(parent);
    lv_obj_set_size(screen->battery_bar, 30, 15);
    lv_obj_align(screen->battery_bar, LV_ALIGN_TOP_LEFT, 25, 10);
    lv_bar_set_range(screen->battery_bar, 0, 100);
    lv_bar_set_value(screen->battery_bar, 75, LV_ANIM_OFF);
    
    // Style battery (EXACT COPY)
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x202020), LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->battery_bar, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(screen->battery_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(screen->battery_bar, 1, 0);
    lv_obj_set_style_pad_all(screen->battery_bar, 2, 0);
    
    // Battery tip (right bump) - Create dummy tip object (stored in wifi_bars[3] for compatibility)
    lv_obj_t *battery_tip = lv_obj_create(parent);
    lv_obj_set_size(battery_tip, 3, 8);
    lv_obj_align_to(battery_tip, screen->battery_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(battery_tip, lv_color_white(), 0);
    lv_obj_set_style_border_width(battery_tip, 0, 0);
    lv_obj_set_style_radius(battery_tip, 0, 0);
    screen->wifi_bars[3] = battery_tip;  // Store for later updates
    
    // Battery percentage label (next to battery icon)
    screen->battery_percent_label = lv_label_create(parent);
    lv_label_set_text(screen->battery_percent_label, "100%");
    lv_obj_set_style_text_color(screen->battery_percent_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->battery_percent_label, &lv_font_montserrat_14, 0);
    lv_obj_align_to(screen->battery_percent_label, battery_tip, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    
    // Charging indicator (lightning bolt) - overlaid on battery bar
    screen->charging_icon = lv_label_create(parent);
    lv_label_set_text(screen->charging_icon, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(screen->charging_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(screen->charging_icon, &lv_font_montserrat_16, 0);
    lv_obj_align_to(screen->charging_icon, screen->battery_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(screen->charging_icon, LV_OBJ_FLAG_HIDDEN);
    
    // === WiFi SIGNAL BARS (EXACT COPY) ===
    const int bar_heights[] = {4, 8, 12, 16};
    const int bar_width = 3;
    const int bar_spacing = 2;
    const int max_bar_height = 16;  // Tallest bar height
    
    for (int i = 0; i < 4; i++) {  // ALL 4 bars
        screen->wifi_bars[i] = lv_obj_create(parent);
        lv_obj_set_size(screen->wifi_bars[i], bar_width, bar_heights[i]);
        // Align all bars at the BOTTOM (not center)
        int y_offset = 10 + (max_bar_height - bar_heights[i]);  // Bottom-align by offsetting shorter bars down
        lv_obj_align(screen->wifi_bars[i], LV_ALIGN_TOP_RIGHT, 
                     -25 - (3 - i) * (bar_width + bar_spacing), 
                     y_offset);
        lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);
        lv_obj_set_style_border_width(screen->wifi_bars[i], 0, 0);
        lv_obj_set_style_radius(screen->wifi_bars[i], 0, 0);
    }
    
    // Store battery tip reference (was using wifi_bars[3] earlier, now need separate storage)
    // Actually, we don't use battery_tip reference after creation, so no need to store it
}

/**
 * @brief Button event callback
 */
static void queue_button_event_cb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *screen_obj = (lv_obj_t*)lv_event_get_user_data(e);
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !screen->event_cb) return;
    
    // Touch is activity!
    extern void state_machine_update_activity();
    state_machine_update_activity();
    
    // Determine which button (Up, Play, Down only - Upload/Delete are physical buttons)
    if (btn == screen->btn_up) {
        // Move selection up (with wrapping)
        screen->selected_index--;
        if (screen->selected_index < 0) {
            screen->selected_index = screen->item_count;  // Wrap to last item
            // Position window to show last item
            if (screen->item_count > 8) {
                screen->window_offset = screen->item_count - 8;  // Show last 8 items
            } else {
                screen->window_offset = 0;
            }
        } else if (screen->selected_index > 0) {
            // Scroll window up if needed (only for recordings, not "Select All")
            if (screen->selected_index < screen->window_offset + 1) {
                screen->window_offset = screen->selected_index - 1;
                if (screen->window_offset < 0) screen->window_offset = 0;
            }
        } else {
            // selected_index = 0 ("Select All") - reset window to top
            screen->window_offset = 0;
        }
        
        update_queue_display_window(screen);
        
    } else if (btn == screen->btn_down) {
        // Move selection down (with wrapping)
        screen->selected_index++;
        if (screen->selected_index > screen->item_count) {
            screen->selected_index = 0;  // Wrap to "All recordings"
            screen->window_offset = 0;
        } else if (screen->selected_index > 0) {
            // Scroll window down if needed (only for recordings)
            if (screen->selected_index > screen->window_offset + 8) {
                screen->window_offset = screen->selected_index - 8;
            }
        }
        
        update_queue_display_window(screen);
        
    } else if (btn == screen->btn_play) {
        // Toggle Play/Stop
        screen->is_playing = !screen->is_playing;
        if (screen->is_playing) {
            lv_label_set_text(screen->icon_play, LV_SYMBOL_STOP);  // Change to STOP
            lv_obj_set_style_text_color(screen->icon_play, lv_color_hex(0xFF0000), 0);  // Red
            // Trigger play event
            screen->event_cb(QUEUE_EVENT_PLAY);
        } else {
            lv_label_set_text(screen->icon_play, LV_SYMBOL_PLAY);   // Change back to Play
            lv_obj_set_style_text_color(screen->icon_play, lv_color_hex(0x00FF00), 0);  // Green
            // Trigger stop event
            screen->event_cb(QUEUE_EVENT_STOP);
        }
    }
}

/**
 * @brief Create UI elements
 */
static void create_queue_ui_elements(queue_screen_t *screen, lv_obj_t *parent) {
    // Title "QUEUE" (top center, dark vivid brown)
    screen->title_label = lv_label_create(parent);
    lv_label_set_text(screen->title_label, "QUEUE");
    lv_obj_set_style_text_font(screen->title_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(screen->title_label, lv_color_hex(0x8B4513), 0);  // Saddle brown
    lv_obj_align(screen->title_label, LV_ALIGN_TOP_MID, 0, 40);
    
    // === LIST CONTAINER (polished with subtle borders) ===
    // Max 8 items visible (8 * 26px = 208px) + "All" row at top + extra space at bottom
    screen->list_container = lv_obj_create(parent);
    lv_obj_remove_style_all(screen->list_container);
    lv_obj_set_size(screen->list_container, 285, 241);  // 285px wide (room for scrollbar), 241px tall (+7px)
    lv_obj_align(screen->list_container, LV_ALIGN_TOP_MID, -15, 85);  // Shift left to make room
    lv_obj_set_style_bg_color(screen->list_container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen->list_container, LV_OPA_COVER, 0);
    
    // Add subtle border for polish
    lv_obj_set_style_border_color(screen->list_container, lv_color_hex(0x404040), 0);  // Dark gray border
    lv_obj_set_style_border_width(screen->list_container, 1, 0);
    lv_obj_set_style_radius(screen->list_container, 4, 0);  // Rounded corners
    lv_obj_set_style_pad_all(screen->list_container, 4, 0);  // Inner padding
    
    // Disable scrolling (we handle display windowing manually)
    lv_obj_clear_flag(screen->list_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create list item containers with hover effect
    for (int i = 0; i < 10; i++) {
        // Create item background container
        lv_obj_t *item_bg = lv_obj_create(screen->list_container);
        lv_obj_remove_style_all(item_bg);
        lv_obj_set_size(item_bg, 258, 24);  // Narrower to fit in 285px container (leave room for scrollbar)
        lv_obj_align(item_bg, LV_ALIGN_TOP_LEFT, 4, 6 + (i * 26));
        lv_obj_set_style_bg_opa(item_bg, LV_OPA_TRANSP, 0);  // Transparent by default
        lv_obj_set_style_radius(item_bg, 3, 0);
        
        // Create text label inside
        screen->list_labels[i] = lv_label_create(item_bg);
        lv_obj_set_style_text_font(screen->list_labels[i], &lv_font_montserrat_14, 0);  // Slightly smaller for better fit
        lv_obj_set_style_text_color(screen->list_labels[i], lv_color_white(), 0);
        lv_obj_align(screen->list_labels[i], LV_ALIGN_LEFT_MID, 20, 0);  // Indent for selection indicator
        lv_obj_add_flag(screen->list_labels[i], LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    }
    
    // Selection highlight (brown background highlight instead of pointer)
    screen->selection_pointer = lv_obj_create(screen->list_container);
    lv_obj_remove_style_all(screen->selection_pointer);
    lv_obj_set_size(screen->selection_pointer, 258, 24);  // Match item width
    lv_obj_align(screen->selection_pointer, LV_ALIGN_TOP_LEFT, 4, 6);
    lv_obj_set_style_bg_color(screen->selection_pointer, lv_color_hex(0x8B4513), 0);  // Saddle brown
    lv_obj_set_style_bg_opa(screen->selection_pointer, LV_OPA_30, 0);  // Semi-transparent
    lv_obj_set_style_radius(screen->selection_pointer, 3, 0);
    lv_obj_set_style_border_color(screen->selection_pointer, lv_color_hex(0x8B4513), 0);
    lv_obj_set_style_border_width(screen->selection_pointer, 1, 0);
    
    // Scrollbar indicator (brown bar inside container on right edge, shown when >8 items)
    screen->scrollbar_indicator = lv_obj_create(screen->list_container);
    lv_obj_set_size(screen->scrollbar_indicator, 8, 50);  // 8px wide, height varies
    lv_obj_align(screen->scrollbar_indicator, LV_ALIGN_TOP_RIGHT, -4, 6);  // Inside container, right edge
    lv_obj_set_style_bg_color(screen->scrollbar_indicator, lv_color_hex(0x8B4513), 0);  // Saddle brown
    lv_obj_set_style_border_width(screen->scrollbar_indicator, 0, 0);
    lv_obj_set_style_radius(screen->scrollbar_indicator, 4, 0);
    lv_obj_add_flag(screen->scrollbar_indicator, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    
    // === TOUCH BUTTONS (bottom, three buttons: Up, Play, Down) ===
    // Up button
    screen->btn_up = lv_btn_create(parent);
    lv_obj_set_size(screen->btn_up, 80, 50);
    lv_obj_align(screen->btn_up, LV_ALIGN_BOTTOM_LEFT, 20, -50);
    lv_obj_set_style_bg_color(screen->btn_up, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(screen->btn_up, 10, 0);
    lv_obj_add_event_cb(screen->btn_up, queue_button_event_cb, LV_EVENT_CLICKED, parent);
    
    screen->icon_up = lv_label_create(screen->btn_up);
    lv_label_set_text(screen->icon_up, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(screen->icon_up, lv_color_hex(0x8B4513), 0);
    lv_obj_set_style_text_font(screen->icon_up, &lv_font_montserrat_32, 0);
    lv_obj_center(screen->icon_up);
    
    // Play/Stop button (center)
    screen->btn_play = lv_btn_create(parent);
    lv_obj_set_size(screen->btn_play, 80, 50);
    lv_obj_align(screen->btn_play, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_obj_set_style_bg_color(screen->btn_play, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(screen->btn_play, 10, 0);
    lv_obj_add_event_cb(screen->btn_play, queue_button_event_cb, LV_EVENT_CLICKED, parent);
    
    screen->icon_play = lv_label_create(screen->btn_play);
    lv_label_set_text(screen->icon_play, LV_SYMBOL_PLAY);  // Starts as Play
    lv_obj_set_style_text_color(screen->icon_play, lv_color_hex(0x00FF00), 0);  // Green for Play
    lv_obj_set_style_text_font(screen->icon_play, &lv_font_montserrat_32, 0);
    lv_obj_center(screen->icon_play);
    
    // Down button
    screen->btn_down = lv_btn_create(parent);
    lv_obj_set_size(screen->btn_down, 80, 50);
    lv_obj_align(screen->btn_down, LV_ALIGN_BOTTOM_RIGHT, -20, -50);
    lv_obj_set_style_bg_color(screen->btn_down, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(screen->btn_down, 10, 0);
    lv_obj_add_event_cb(screen->btn_down, queue_button_event_cb, LV_EVENT_CLICKED, parent);
    
    screen->icon_down = lv_label_create(screen->btn_down);
    lv_label_set_text(screen->icon_down, LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(screen->icon_down, lv_color_hex(0x8B4513), 0);
    lv_obj_set_style_text_font(screen->icon_down, &lv_font_montserrat_32, 0);
    lv_obj_center(screen->icon_down);
    
    // === PHYSICAL BUTTON INDICATORS (RIGHT side, NOT touchable) ===
    // Top button indicator - Container for settings + mic + upload icons (vertically stacked)
    lv_obj_t *indicator_container = lv_obj_create(parent);
    lv_obj_remove_style_all(indicator_container);
    lv_obj_set_size(indicator_container, 30, 75);  // Increased from 50 to 75px
    lv_obj_align(indicator_container, LV_ALIGN_TOP_RIGHT, -5, 90);  // Moved up from 115 to 90
    lv_obj_set_style_bg_opa(indicator_container, LV_OPA_TRANSP, 0);
    
    // Settings icon (top) - positioned at top with proper spacing
    lv_obj_t *settings_icon = lv_label_create(indicator_container);
    lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(settings_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(settings_icon, lv_color_white(), 0);
    lv_obj_align(settings_icon, LV_ALIGN_TOP_MID, 0, 2);  // 2px from top (was -5, causing cut)
    
    // Mic icon (middle) - WHITE as requested, properly centered
    lv_obj_t *mic_icon = icon_create_mic_small(indicator_container, lv_color_white());
    lv_obj_align(mic_icon, LV_ALIGN_CENTER, 0, 0);  // Centered in container
    
    // Upload icon (bottom) - positioned at bottom with proper spacing
    screen->top_button_indicator = lv_label_create(indicator_container);
    lv_label_set_text(screen->top_button_indicator, LV_SYMBOL_UPLOAD);
    lv_obj_set_style_text_font(screen->top_button_indicator, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(screen->top_button_indicator, lv_color_hex(0x00FF00), 0);  // Green
    lv_obj_align(screen->top_button_indicator, LV_ALIGN_BOTTOM_MID, 0, -2);  // 2px from bottom
    
    // Bottom button indicator (Delete/Trash icon)
    screen->bottom_button_indicator = lv_label_create(parent);
    lv_label_set_text(screen->bottom_button_indicator, LV_SYMBOL_TRASH);
    lv_obj_set_style_text_font(screen->bottom_button_indicator, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(screen->bottom_button_indicator, lv_color_hex(0xFF0000), 0);  // Red
    lv_obj_align(screen->bottom_button_indicator, LV_ALIGN_BOTTOM_RIGHT, -5, -120);  // RIGHT side
    
    // Progress bar (thin brown bar below buttons)
    screen->progress_bar = lv_bar_create(parent);
    lv_obj_set_size(screen->progress_bar, SCREEN_WIDTH - 60, 4);  // Full width minus margins, 4px height
    lv_obj_align(screen->progress_bar, LV_ALIGN_BOTTOM_MID, 0, -10);  // 10px from bottom
    lv_bar_set_range(screen->progress_bar, 0, 100);  // 0-100%
    lv_bar_set_value(screen->progress_bar, 0, LV_ANIM_OFF);
    
    // Style: dark vivid brown themed (matches title)
    lv_obj_set_style_bg_color(screen->progress_bar, lv_color_hex(0x404040), LV_PART_MAIN);  // Dark gray background
    lv_obj_set_style_bg_opa(screen->progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(screen->progress_bar, lv_color_hex(0x8B4513), LV_PART_INDICATOR);  // Saddle brown (darker, more vivid)
    lv_obj_set_style_border_width(screen->progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(screen->progress_bar, 2, LV_PART_MAIN);
    
    // Hidden by default (only show during playback)
    lv_obj_add_flag(screen->progress_bar, LV_OBJ_FLAG_HIDDEN);
    
    // Initialize selection state
    screen->selected_index = 1;  // Start at row 2 (oldest recording, index 1)
    screen->item_count = 0;
    screen->window_offset = 0;  // Start showing from first item
    screen->is_playing = false;
}

/**
 * @brief Create QUEUE screen (public API)
 */
lv_obj_t* screen_queue_create(lv_obj_t *parent, queue_event_cb_t event_cb) {
    // Create screen container
    lv_obj_t *screen_obj = lv_obj_create(parent);
    lv_obj_remove_style_all(screen_obj);
    lv_obj_set_size(screen_obj, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, 0);
    lv_obj_center(screen_obj);
    
    // Allocate screen state
    queue_screen_t *screen = (queue_screen_t*)lv_mem_alloc(sizeof(queue_screen_t));
    LV_ASSERT_MALLOC(screen);
    memset(screen, 0, sizeof(queue_screen_t));
    screen->event_cb = event_cb;
    screen->wifi_connected = false;  // Will be updated via screen_queue_update_wifi()
    lv_obj_set_user_data(screen_obj, screen);
    
    // Register cleanup callback
    lv_obj_add_event_cb(screen_obj, screen_queue_delete_cb, LV_EVENT_DELETE, screen);
    
    // === STATUS BAR ===
    create_queue_status_bar(screen, screen_obj);
    
    // === UI ELEMENTS ===
    create_queue_ui_elements(screen, screen_obj);
    
    return screen_obj;
}

/**
 * @brief Update status bar (EXACT COPY of idle screen)
 */
void screen_queue_update_status(lv_obj_t *screen_obj, const idle_status_t *status) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
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
    
    // Update WiFi bars (all 4 bars)
    for (int i = 0; i < 4; i++) {
        if (status->wifi_connected && i < status->wifi_bars) {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_white(), 0);  // Active
        } else {
            lv_obj_set_style_bg_color(screen->wifi_bars[i], lv_color_hex(0x404040), 0);  // Inactive
        }
    }
}

/**
 * @brief Update display window to show only 8 items at a time
 * @param screen Queue screen state
 */
static void update_queue_display_window(queue_screen_t *screen) {
    if (!screen) return;
    
    // Row 0 is always "Select All Recordings" (always visible)
    // Rows 1-8 show recordings based on window_offset
    // window_offset determines which recording appears at row 1 (0-indexed into items array)
    
    for (int display_row = 1; display_row <= 8; display_row++) {
        int item_index = screen->window_offset + (display_row - 1);  // Map to items array
        
        if (item_index < (int)screen->items.size()) {
            // Show this recording
            lv_label_set_text(screen->list_labels[display_row], screen->items[item_index].time_str.c_str());
            lv_obj_clear_flag(screen->list_labels[display_row], LV_OBJ_FLAG_HIDDEN);
        } else {
            // Hide unused rows
            lv_obj_add_flag(screen->list_labels[display_row], LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    // Position pointer at selected item
    // selected_index 0 = "Select All" (row 0)
    // selected_index 1-N = recording (row = selected_index - window_offset)
    int pointer_row;
    if (screen->selected_index == 0) {
        pointer_row = 0;  // "Select All" at row 0
    } else {
        pointer_row = (screen->selected_index - screen->window_offset);
        // Clamp to visible range (rows 1-8)
        if (pointer_row < 1) pointer_row = 1;
        if (pointer_row > 8) pointer_row = 8;
    }
    
    int y_pos = 6 + (pointer_row * 26);
    lv_obj_align(screen->selection_pointer, LV_ALIGN_TOP_LEFT, 4, y_pos);
    
    // Update scrollbar indicator (show when >8 items)
    if (screen->items.size() > 8 && screen->scrollbar_indicator) {
        lv_obj_clear_flag(screen->scrollbar_indicator, LV_OBJ_FLAG_HIDDEN);
        
        // Calculate scrollbar position and size
        int total_items = screen->items.size();
        int visible_items = 8;
        int container_height = 241;  // List container height (updated to match new size)
        
        // Scrollbar height proportional to visible/total ratio
        int bar_height = (container_height * visible_items) / total_items;
        if (bar_height < 30) bar_height = 30;  // Minimum height
        
        // Scrollbar position proportional to window_offset
        int max_offset = total_items - visible_items;
        int bar_y_offset = (max_offset > 0) ? ((container_height - bar_height) * screen->window_offset) / max_offset : 0;
        
        lv_obj_set_height(screen->scrollbar_indicator, bar_height);
        lv_obj_align(screen->scrollbar_indicator, LV_ALIGN_TOP_RIGHT, -4, 6 + bar_y_offset);  // Inside container
    } else if (screen->scrollbar_indicator) {
        lv_obj_add_flag(screen->scrollbar_indicator, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Set selected index programmatically (for sequential playback)
 * @param screen_obj Queue screen object
 * @param index Index to select (0 = "Select All", 1+ = recordings)
 */
void screen_queue_set_selected_index(lv_obj_t *screen_obj, int index) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Clamp to valid range
    if (index < 0) index = 0;
    if (index > screen->item_count) index = screen->item_count;
    
    screen->selected_index = index;
    
    // Adjust window to keep selection visible
    if (index == 0) {
        screen->window_offset = 0;
    } else if (index > 0) {
        // Ensure selection is visible in window
        if (index < screen->window_offset + 1) {
            screen->window_offset = index - 1;
            if (screen->window_offset < 0) screen->window_offset = 0;
        } else if (index > screen->window_offset + 8) {
            screen->window_offset = index - 8;
        }
    }
    
    // Update display
    update_queue_display_window(screen);
}

/**
 * @brief Update queue filenames list (public API)
 * Now populates list with time + duration info
 */
void screen_queue_update_filenames(lv_obj_t *screen_obj, const char* filenames) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    // Get detailed queue info (index, date, time, duration)
    screen->items = queue_get_items_info();
    
    // Row 0: "Select All Recordings" (always at top)
    lv_label_set_text(screen->list_labels[0], "Select All Recordings");
    lv_obj_clear_flag(screen->list_labels[0], LV_OBJ_FLAG_HIDDEN);
    
    // Update item count and reset selection
    screen->item_count = screen->items.size();
    screen->selected_index = 1;  // Start at oldest recording (row 1)
    screen->window_offset = 0;   // Start showing from first item
    
    // Update display with windowed view
    update_queue_display_window(screen);
}

/**
 * @brief Update WiFi connection state (public API)
 */
void screen_queue_update_wifi(lv_obj_t *screen_obj, bool wifi_connected) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return;
    
    screen->wifi_connected = wifi_connected;
    
    // Note: In new UI design, physical buttons (Upload/Delete) are always visible
    // WiFi state is shown via status bar icons instead
}

/**
 * @brief Update progress bar (public API)
 * @param screen_obj Queue screen object
 * @param percent Progress percentage (0-100)
 */
void screen_queue_update_progress(lv_obj_t *screen_obj, int percent) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !screen->progress_bar) return;
    
    // Clamp to 0-100
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    lv_bar_set_value(screen->progress_bar, percent, LV_ANIM_OFF);
}

/**
 * @brief Show/hide progress bar (public API)
 * @param screen_obj Queue screen object
 * @param visible true to show, false to hide
 */
void screen_queue_show_progress(lv_obj_t *screen_obj, bool visible) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !screen->progress_bar) return;
    
    if (visible) {
        lv_obj_clear_flag(screen->progress_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen->progress_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Reset play button to play state (public API)
 * @param screen_obj Queue screen object
 */
void screen_queue_reset_play_button(lv_obj_t *screen_obj) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen || !screen->icon_play) return;
    
    // Reset to play icon and state
    screen->is_playing = false;
    lv_label_set_text(screen->icon_play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_color(screen->icon_play, lv_color_hex(0x00FF00), 0);  // Green
}

/**
 * @brief Show/hide QUEUE screen (public API)
 */
void screen_queue_set_visible(lv_obj_t *screen_obj, bool visible) {
    if (visible) {
        lv_obj_clear_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(screen_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Get selected index (public API)
 */
int screen_queue_get_selected_index(lv_obj_t *screen_obj) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return 1;  // Default to first recording
    return screen->selected_index;
}

/**
 * @brief Get selected filename (public API)
 */
String screen_queue_get_selected_filename(lv_obj_t *screen_obj) {
    queue_screen_t *screen = (queue_screen_t*)lv_obj_get_user_data(screen_obj);
    if (!screen) return "";
    
    // Index 0 = "All recordings" - return empty
    if (screen->selected_index == 0) {
        return "";
    }
    
    // Index 1+ = individual recordings
    int item_index = screen->selected_index - 1;  // Adjust for "All" row
    if (item_index >= 0 && item_index < (int)screen->items.size()) {
        return screen->items[item_index].filename;
    }
    
    return "";
}
