/**
 * @file symbol_reference.cpp
 * @brief Display all LVGL symbols for reference
 */

#include <lvgl.h>

void show_all_symbols(lv_obj_t *parent) {
    // Create scrollable container
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, 368, 448);
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_center(container);
    
    // Grid of symbols
    const char* symbols[] = {
        LV_SYMBOL_AUDIO,
        LV_SYMBOL_VIDEO,
        LV_SYMBOL_LIST,
        LV_SYMBOL_OK,
        LV_SYMBOL_CLOSE,
        LV_SYMBOL_POWER,
        LV_SYMBOL_SETTINGS,
        LV_SYMBOL_HOME,
        LV_SYMBOL_DOWNLOAD,
        LV_SYMBOL_DRIVE,
        LV_SYMBOL_REFRESH,
        LV_SYMBOL_MUTE,
        LV_SYMBOL_VOLUME_MID,
        LV_SYMBOL_VOLUME_MAX,
        LV_SYMBOL_IMAGE,
        LV_SYMBOL_EDIT,
        LV_SYMBOL_PREV,
        LV_SYMBOL_PLAY,
        LV_SYMBOL_PAUSE,
        LV_SYMBOL_STOP,
        LV_SYMBOL_NEXT,
        LV_SYMBOL_EJECT,
        LV_SYMBOL_LEFT,
        LV_SYMBOL_RIGHT,
        LV_SYMBOL_PLUS,
        LV_SYMBOL_MINUS,
        LV_SYMBOL_EYE_OPEN,
        LV_SYMBOL_EYE_CLOSE,
        LV_SYMBOL_WARNING,
        LV_SYMBOL_SHUFFLE,
        LV_SYMBOL_UP,
        LV_SYMBOL_DOWN,
        LV_SYMBOL_LOOP,
        LV_SYMBOL_DIRECTORY,
        LV_SYMBOL_UPLOAD,
        LV_SYMBOL_CALL,
        LV_SYMBOL_CUT,
        LV_SYMBOL_COPY,
        LV_SYMBOL_SAVE,
        LV_SYMBOL_CHARGE,
        LV_SYMBOL_PASTE,
        LV_SYMBOL_BELL,
        LV_SYMBOL_KEYBOARD,
        LV_SYMBOL_GPS,
        LV_SYMBOL_FILE,
        LV_SYMBOL_WIFI,
        LV_SYMBOL_BATTERY_FULL,
        LV_SYMBOL_BATTERY_3,
        LV_SYMBOL_BATTERY_2,
        LV_SYMBOL_BATTERY_1,
        LV_SYMBOL_BATTERY_EMPTY,
        LV_SYMBOL_USB,
        LV_SYMBOL_BLUETOOTH,
        LV_SYMBOL_TRASH,
        LV_SYMBOL_BACKSPACE,
        LV_SYMBOL_SD_CARD,
        LV_SYMBOL_NEW_LINE,
    };
    
    const char* names[] = {
        "AUDIO",
        "VIDEO",
        "LIST",
        "OK",
        "CLOSE",
        "POWER",
        "SETTINGS",
        "HOME",
        "DOWNLOAD",
        "DRIVE",
        "REFRESH",
        "MUTE",
        "VOLUME_MID",
        "VOLUME_MAX",
        "IMAGE",
        "EDIT",
        "PREV",
        "PLAY",
        "PAUSE",
        "STOP",
        "NEXT",
        "EJECT",
        "LEFT",
        "RIGHT",
        "PLUS",
        "MINUS",
        "EYE_OPEN",
        "EYE_CLOSE",
        "WARNING",
        "SHUFFLE",
        "UP",
        "DOWN",
        "LOOP",
        "DIRECTORY",
        "UPLOAD",
        "CALL",
        "CUT",
        "COPY",
        "SAVE",
        "CHARGE",
        "PASTE",
        "BELL",
        "KEYBOARD",
        "GPS",
        "FILE",
        "WIFI",
        "BAT_FULL",
        "BAT_3",
        "BAT_2",
        "BAT_1",
        "BAT_EMPTY",
        "USB",
        "BLUETOOTH",
        "TRASH",
        "BACKSPACE",
        "SD_CARD",
        "NEW_LINE",
    };
    
    int num_symbols = sizeof(symbols) / sizeof(symbols[0]);
    
    // Create labels in grid
    int cols = 4;
    int row = 0, col = 0;
    
    for (int i = 0; i < num_symbols; i++) {
        // Symbol
        lv_obj_t *sym_label = lv_label_create(container);
        lv_label_set_text(sym_label, symbols[i]);
        lv_obj_set_style_text_font(sym_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(sym_label, lv_color_white(), 0);
        lv_obj_set_pos(sym_label, 10 + col * 90, 10 + row * 60);
        
        // Name
        lv_obj_t *name_label = lv_label_create(container);
        lv_label_set_text(name_label, names[i]);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(0x808080), 0);
        lv_obj_set_pos(name_label, 10 + col * 90, 35 + row * 60);
        
        col++;
        if (col >= cols) {
            col = 0;
            row++;
        }
    }
}
