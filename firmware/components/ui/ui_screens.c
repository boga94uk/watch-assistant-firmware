#include "ui.h"
#include "ui_galaxy.h"
#include "board_pins.h"
#include "display.h"
#include "settings.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static const char *TAG_UI = "ui_set";

extern void audio_set_volume(uint8_t percent);
extern void audio_set_mic_gain(uint8_t steps);
extern void audio_play_beep(void);

#define COL_BG    0x050508
#define COL_FG    0xF1EFE6
#define COL_MUTED 0x8A8680
#define COL_IDLE  0x7CFFB2
#define COL_REC   0xFF0000
#define COL_SEND  0xFF0000
#define COL_PROC  0xE6D000
#define COL_RECV  0x00FF00
#define COL_NET   0x6EA8FF
#define COL_SAY   0x7CFFB2
#define COL_ASK   0xFFDD00
#define COL_CONF  0xC9A0FF
#define COL_READY 0x00FF00
#define COL_MIC   0x00FF00
#define COL_RED   0xFF0000

static lv_obj_t *s_scr;
static lv_obj_t *s_galaxy;
static lv_obj_t *s_orb;
static lv_obj_t *s_ring;
static lv_obj_t *s_title;
static lv_obj_t *s_body;
static lv_obj_t *s_queue;
static char s_spoken[768];
static lv_obj_t *s_reply_box;
static lv_obj_t *s_reply_lbl;
static lv_obj_t *s_dot;
static lv_obj_t *s_bat_bar;
static lv_obj_t *s_bat_pct;
static lv_obj_t *s_charge;
static lv_obj_t *s_clock;
static lv_obj_t *s_wifi[4];
static lv_obj_t *s_gear;
static lv_obj_t *s_sep;
static lv_obj_t *s_sep_bot;
static lv_obj_t *s_mic;
static lv_obj_t *s_power;
static lv_obj_t *s_tick;
static lv_obj_t *s_cancel;
static lv_obj_t *s_glyph;
static lv_obj_t *s_play_bar;
static lv_obj_t *s_settings;
static lv_obj_t *s_sl_bright;
static lv_obj_t *s_sl_vol;
static lv_obj_t *s_sl_mic;
static lv_obj_t *s_lb_bright;
static lv_obj_t *s_lb_vol;
static lv_obj_t *s_lb_mic;
static lv_obj_t *s_set_wifi;
static lv_obj_t *s_set_note;
static lv_obj_t *s_batt_box;
static lv_obj_t *s_batt_opt[SETTINGS_AUTO_OFF_COUNT];
static bool s_batt_open;
static lv_obj_t *s_sleep_box;
static lv_obj_t *s_sleep_time;
static lv_obj_t *s_sleep_date;
static lv_obj_t *s_pwr_box;
static lv_obj_t *s_pwr_note;
static bool s_pwr_open;
static lv_obj_t *s_qdlg_box;
static lv_obj_t *s_qdlg_one_l;
static bool s_qdlg_open;
static lv_anim_t s_breath;
static lv_anim_t s_orb_color_anim;
static uint32_t s_orb_from = COL_IDLE;
static uint32_t s_orb_to = COL_IDLE;
static uint32_t s_orb_now = COL_IDLE;
static ui_screen_t s_cur = UI_SCREEN_BOOT;
static char s_time[10] = "--:--";
static char s_date[12] = "";
static int s_battery = -1;
static int s_wifi_bars;
static bool s_charging;
static bool s_vbus;
static bool s_wifi_on;
static bool s_settings_open;

static lv_color_t c(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static void style_label(lv_obj_t *obj, const lv_font_t *font, uint32_t rgb)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, c(rgb), 0);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
}

static void vis(lv_obj_t *obj, bool on)
{
    if (!obj) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void add_mic_drawing(lv_obj_t *btn, uint32_t color)
{
    lv_obj_t *cap = lv_obj_create(btn);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 8, 11);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_radius(cap, 4, 0);
    lv_obj_set_style_bg_color(cap, c(color), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(cap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *stem = lv_obj_create(btn);
    lv_obj_remove_style_all(stem);
    lv_obj_set_size(stem, 2, 4);
    lv_obj_align(stem, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_set_style_bg_color(stem, c(color), 0);
    lv_obj_set_style_bg_opa(stem, LV_OPA_COVER, 0);
    lv_obj_clear_flag(stem, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(stem, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *base = lv_obj_create(btn);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, 10, 2);
    lv_obj_align(base, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_color(base, c(color), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_icon_btn(lv_obj_t *parent, uint32_t bg, bool round, lv_align_t align, int x, int y,
                               lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 20, 20);
    lv_obj_align(btn, align, x, y);
    lv_obj_set_style_radius(btn, round ? 10 : 0, 0);
    lv_obj_set_style_bg_color(btn, c(bg), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_ext_click_area(btn, 12);
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

static void mic_cb(lv_event_t *e)
{
    (void)e;
    app_mic_clicked();
}

static void orb_cb(lv_event_t *e)
{
    (void)e;
    if (s_cur == UI_SCREEN_RECORDING) {
        app_confirm_clicked();
    }
}

static void power_cb(lv_event_t *e)
{
    (void)e;
    app_power_clicked();
}

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    app_confirm_clicked();
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    app_close_overlay();
}

static void pwr_off_cb(lv_event_t *e)
{
    (void)e;
    app_power_off_confirmed();
}

static void pwr_cancel_cb(lv_event_t *e)
{
    (void)e;
    ui_power_dialog_close();
}

static void qdlg_one_cb(lv_event_t *e)
{
    (void)e;
    ui_queue_dialog_close();
    app_queue_erase_one();
}

static void qdlg_all_cb(lv_event_t *e)
{
    (void)e;
    ui_queue_dialog_close();
    app_queue_erase_all();
}

static void qdlg_cancel_cb(lv_event_t *e)
{
    (void)e;
    ui_queue_dialog_close();
}

static lv_obj_t *make_stack_btn(lv_obj_t *parent, const char *text, uint32_t bg, lv_coord_t y,
                                lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 252, 46);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, c(bg), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, c(0xFFFFFF), 0);
    lv_obj_center(l);
    return l;
}

static void style_cancel_for_queue(bool on_queue)
{
    if (!s_cancel) {
        return;
    }
    lv_obj_set_style_bg_color(s_cancel, c(COL_RED), 0);
    lv_obj_set_style_radius(s_cancel, 10, 0);
    lv_obj_t *lbl = lv_obj_get_child(s_cancel, 0);
    if (lbl) {
        lv_obj_set_style_text_color(lbl, c(0xFFFFFF), 0);
    }
    lv_obj_align(s_cancel, LV_ALIGN_BOTTOM_RIGHT, -5, on_queue ? -92 : -60);
}

static void layout_play_bar(void)
{
    if (!s_play_bar) {
        return;
    }
    vis(s_play_bar, true);
    lv_bar_set_value(s_play_bar, 0, LV_ANIM_OFF);
    if (s_reply_box) {
        lv_obj_align_to(s_play_bar, s_reply_box, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    }
}

static void reply_scroll_to(int percent)
{
    if (!s_reply_box || lv_obj_has_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    lv_obj_update_layout(s_reply_box);
    lv_coord_t y = lv_obj_get_scroll_y(s_reply_box);
    lv_coord_t bot = lv_obj_get_scroll_bottom(s_reply_box);
    lv_coord_t max_y = y + bot;
    if (max_y < 0) {
        max_y = 0;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    lv_obj_scroll_to_y(s_reply_box, (lv_coord_t)((max_y * percent) / 100), LV_ANIM_OFF);
}

static void layout_bottom_text(bool ready)
{
    vis(s_title, true);
    vis(s_body, true);
    vis(s_glyph, false);
    vis(s_play_bar, false);
    vis(s_reply_box, false);
    lv_obj_set_width(s_body, 300);
    lv_obj_set_style_max_height(s_body, 48, 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_font(s_body, &lv_font_montserrat_16, 0);
    lv_obj_align(s_title, LV_ALIGN_BOTTOM_LEFT, 22, -50);
    lv_obj_align(s_body, LV_ALIGN_BOTTOM_LEFT, 22, -20);
    if (ready) {
        lv_obj_set_style_text_color(s_title, c(COL_READY), 0);
        lv_obj_set_style_text_color(s_body, c(0xFFFFFF), 0);
    } else {
        lv_obj_set_style_text_color(s_title, c(COL_FG), 0);
        lv_obj_set_style_text_color(s_body, c(COL_MUTED), 0);
    }
}

static void layout_reply_text(uint32_t glyph_color, const char *glyph)
{
    vis(s_title, false);
    vis(s_queue, false);
    vis(s_body, false);
    vis(s_glyph, true);
    vis(s_reply_box, true);
    lv_label_set_text(s_glyph, glyph);
    lv_obj_set_style_text_color(s_glyph, c(glyph_color), 0);
    lv_obj_align(s_glyph, LV_ALIGN_TOP_MID, 0, 48);
    if (s_reply_lbl) {
        lv_obj_set_style_text_font(s_reply_lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_reply_lbl, c(0xFFFFFF), 0);
        lv_label_set_text(s_reply_lbl, s_spoken[0] ? s_spoken : "");
    }
    if (s_reply_box) {
        lv_obj_scroll_to_y(s_reply_box, 0, LV_ANIM_OFF);
        lv_obj_move_foreground(s_reply_box);
    }
    layout_play_bar();
    if (s_play_bar) {
        lv_obj_move_foreground(s_play_bar);
    }
}

static void set_side_icons(bool gear, bool sep, bool mic, bool power, bool tick, bool cancel)
{
    vis(s_gear, gear);
    vis(s_sep, sep);
    vis(s_mic, mic);
    vis(s_power, power);
    vis(s_tick, tick);
    vis(s_cancel, cancel);
    vis(s_sep_bot, power && cancel);
    style_cancel_for_queue(false);
    if (mic && gear) {
        lv_obj_align(s_mic, LV_ALIGN_TOP_RIGHT, -5, 60);
        lv_obj_align(s_gear, LV_ALIGN_TOP_RIGHT, -5, 89);
    } else if (mic) {
        lv_obj_align(s_mic, LV_ALIGN_TOP_RIGHT, -5, 52);
        lv_obj_align(s_gear, LV_ALIGN_TOP_RIGHT, -5, 60);
    } else if (gear) {
        lv_obj_align(s_gear, LV_ALIGN_TOP_RIGHT, -5, 60);
    }
}

static void breath_cb(void *var, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static uint32_t mix_rgb(uint32_t a, uint32_t b, int t)
{
    int ar = (int)((a >> 16) & 0xff);
    int ag = (int)((a >> 8) & 0xff);
    int ab = (int)(a & 0xff);
    int br = (int)((b >> 16) & 0xff);
    int bg = (int)((b >> 8) & 0xff);
    int bb = (int)(b & 0xff);
    int r = ar + ((br - ar) * t) / 256;
    int g = ag + ((bg - ag) * t) / 256;
    int bl = ab + ((bb - ab) * t) / 256;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void orb_color_cb(void *var, int32_t v)
{
    (void)var;
    s_orb_now = mix_rgb(s_orb_from, s_orb_to, (int)v);
    if (s_orb) {
        lv_obj_set_style_bg_color(s_orb, c(s_orb_now), 0);
    }
}

static void start_breath(uint32_t color)
{
    lv_obj_clear_flag(s_orb, LV_OBJ_FLAG_HIDDEN);
    if (color == COL_REC) {
        s_orb_from = COL_REC;
        s_orb_now = COL_REC;
    } else {
        s_orb_from = s_orb_now ? s_orb_now : color;
    }
    s_orb_to = color;
    lv_anim_del(s_orb, orb_color_cb);
    if (s_orb_from != s_orb_to) {
        lv_anim_init(&s_orb_color_anim);
        lv_anim_set_var(&s_orb_color_anim, s_orb);
        lv_anim_set_values(&s_orb_color_anim, 0, 256);
        lv_anim_set_time(&s_orb_color_anim, 1600);
        lv_anim_set_exec_cb(&s_orb_color_anim, orb_color_cb);
        lv_anim_set_path_cb(&s_orb_color_anim, lv_anim_path_ease_in_out);
        lv_anim_start(&s_orb_color_anim);
        lv_obj_set_style_bg_color(s_orb, c(s_orb_from), 0);
    } else {
        lv_obj_set_style_bg_color(s_orb, c(color), 0);
        s_orb_now = color;
    }
    lv_anim_del(s_orb, breath_cb);
    lv_anim_init(&s_breath);
    lv_anim_set_var(&s_breath, s_orb);
    lv_anim_set_values(&s_breath, LV_OPA_60, LV_OPA_COVER);
    lv_anim_set_time(&s_breath, 1800);
    lv_anim_set_playback_time(&s_breath, 1800);
    lv_anim_set_repeat_count(&s_breath, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&s_breath, breath_cb);
    lv_anim_set_path_cb(&s_breath, lv_anim_path_ease_in_out);
    lv_anim_start(&s_breath);
}

static void stop_breath(void)
{
    lv_anim_del(s_orb, breath_cb);
    lv_anim_del(s_orb, orb_color_cb);
    lv_obj_set_style_bg_opa(s_orb, LV_OPA_COVER, 0);
}

static void refresh_status(void)
{
    if (s_cur == UI_SCREEN_SLEEP) {
        if (s_sleep_time) {
            lv_label_set_text(s_sleep_time, s_time);
        }
        if (s_sleep_date && s_date[0]) {
            lv_label_set_text(s_sleep_date, s_date);
        }
        return;
    }
    if (s_battery >= 0) {
        lv_bar_set_value(s_bat_bar, s_battery, LV_ANIM_OFF);
        char pct[8];
        snprintf(pct, sizeof(pct), "%d%%", s_battery);
        lv_label_set_text(s_bat_pct, pct);
        uint32_t col = s_battery > 50 ? 0x3DDC84 : (s_battery > 20 ? 0xFFD36E : 0xFF5A5A);
        lv_obj_set_style_bg_color(s_bat_bar, c(col), LV_PART_INDICATOR);
        if (s_charging) {
            lv_obj_clear_flag(s_charge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_charge, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_bar_set_value(s_bat_bar, s_vbus ? 100 : 0, LV_ANIM_OFF);
        lv_label_set_text(s_bat_pct, s_vbus ? "USB" : "--");
        lv_obj_set_style_bg_color(s_bat_bar, c(0x6EA8FF), LV_PART_INDICATOR);
        lv_obj_add_flag(s_charge, LV_OBJ_FLAG_HIDDEN);
    }

    const int n = s_wifi_on ? s_wifi_bars : 0;
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(s_wifi[i], c(i < n ? 0xF1EFE6 : 0x3A3A44), 0);
    }
    lv_label_set_text(s_clock, s_time);
    if (s_sleep_time) {
        lv_label_set_text(s_sleep_time, s_time);
    }
    if (s_sleep_date && s_date[0]) {
        lv_label_set_text(s_sleep_date, s_date);
    }
}

static void gear_cb(lv_event_t *e)
{
    (void)e;
    app_open_settings();
}

static void close_settings_cb(lv_event_t *e)
{
    (void)e;
    app_close_overlay();
}

static void refresh_tune_labels(void)
{
    const settings_t *st = settings_get();
    if (s_lb_bright) {
        lv_label_set_text_fmt(s_lb_bright, "Brightness  %d%%", st->brightness);
    }
    if (s_lb_vol) {
        lv_label_set_text_fmt(s_lb_vol, "Volume  %d%%", st->volume);
    }
    if (s_lb_mic) {
        lv_label_set_text_fmt(s_lb_mic, "Mic gain  %d / 7", st->mic_gain);
    }
}

static void persist_tune(void)
{
    settings_save(settings_get());
}

static void slider_bright_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    settings_set_tune((uint8_t)v, settings_get()->volume, settings_get()->mic_gain);
    display_set_brightness((uint8_t)v);
    refresh_tune_labels();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        persist_tune();
        ESP_LOGI(TAG_UI, "brightness saved %d", v);
    }
}

static void slider_vol_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    settings_set_tune(settings_get()->brightness, (uint8_t)v, settings_get()->mic_gain);
    audio_set_volume((uint8_t)v);
    refresh_tune_labels();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        persist_tune();
        audio_play_beep();
        ESP_LOGI(TAG_UI, "volume saved %d + beep", v);
    }
}

static void slider_mic_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    settings_set_tune(settings_get()->brightness, settings_get()->volume, (uint8_t)v);
    audio_set_mic_gain((uint8_t)v);
    refresh_tune_labels();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        persist_tune();
        ESP_LOGI(TAG_UI, "mic gain saved %d", v);
    }
}

static void ap_btn_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG_UI, "open setup Wi-Fi");
    if (s_set_note) {
        lv_label_set_text(s_set_note, "Starting setup AP...");
    }
    app_open_setup_ap();
}

static void style_batt_opts(void)
{
    const uint8_t cur = settings_get()->auto_off;
    for (unsigned i = 0; i < SETTINGS_AUTO_OFF_COUNT; i++) {
        if (!s_batt_opt[i]) {
            continue;
        }
        lv_obj_set_style_bg_color(s_batt_opt[i], c(i == cur ? COL_RED : 0x5A1212), 0);
    }
}

static void batt_opt_cb(lv_event_t *e)
{
    uint8_t mode = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    settings_set_auto_off(mode);
    style_batt_opts();
    if (s_set_note) {
        lv_label_set_text_fmt(s_set_note, "Auto off  %s", settings_auto_off_label());
    }
    ui_battery_dialog_close();
    ESP_LOGI(TAG_UI, "auto off %s", settings_auto_off_label());
}

static void batt_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_battery_dialog_open();
}

static void batt_bg_cb(lv_event_t *e)
{
    (void)e;
    ui_battery_dialog_close();
}

static lv_obj_t *make_slider(lv_obj_t *parent, int min, int max, int val, lv_event_cb_t cb)
{
    lv_obj_t *sl = lv_slider_create(parent);
    lv_obj_set_size(sl, 300, 36);
    lv_slider_set_range(sl, min, max);
    lv_slider_set_value(sl, val, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, c(0x2A2A32), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, c(0x667eea), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, c(COL_FG), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 12, LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 28);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_RELEASED, NULL);
    lv_obj_clear_flag(sl, LV_OBJ_FLAG_SCROLL_CHAIN);
    return sl;
}

static lv_obj_t *make_caption(lv_obj_t *parent, const char *text)
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_label_set_text(lab, text);
    lv_obj_set_style_text_color(lab, c(COL_FG), 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_16, 0);
    return lab;
}

static void build_settings(lv_obj_t *parent)
{
    s_settings = lv_obj_create(parent);
    lv_obj_remove_style_all(s_settings);
    lv_obj_set_size(s_settings, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_pos(s_settings, 0, 0);
    lv_obj_set_style_bg_color(s_settings, c(0x0B0B10), 0);
    lv_obj_set_style_bg_opa(s_settings, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_settings, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *head = lv_label_create(s_settings);
    lv_label_set_text(head, "Settings");
    lv_obj_set_style_text_font(head, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(head, c(COL_FG), 0);
    lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 14);

    const settings_t *st = settings_get();

    s_lb_bright = make_caption(s_settings, "Brightness");
    lv_obj_align(s_lb_bright, LV_ALIGN_TOP_LEFT, 34, 56);
    s_sl_bright = make_slider(s_settings, 8, 100, st->brightness, slider_bright_cb);
    lv_obj_align(s_sl_bright, LV_ALIGN_TOP_MID, 0, 80);

    s_lb_vol = make_caption(s_settings, "Volume");
    lv_obj_align(s_lb_vol, LV_ALIGN_TOP_LEFT, 34, 128);
    s_sl_vol = make_slider(s_settings, 0, 100, st->volume, slider_vol_cb);
    lv_obj_align(s_sl_vol, LV_ALIGN_TOP_MID, 0, 152);

    s_lb_mic = make_caption(s_settings, "Mic gain");
    lv_obj_align(s_lb_mic, LV_ALIGN_TOP_LEFT, 34, 200);
    s_sl_mic = make_slider(s_settings, 0, 7, st->mic_gain, slider_mic_cb);
    lv_obj_align(s_sl_mic, LV_ALIGN_TOP_MID, 0, 224);

    s_set_wifi = lv_label_create(s_settings);
    lv_obj_set_style_text_color(s_set_wifi, c(COL_MUTED), 0);
    lv_obj_set_style_text_font(s_set_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_set_wifi, 300);
    lv_obj_align(s_set_wifi, LV_ALIGN_TOP_LEFT, 34, 276);
    lv_label_set_text_fmt(s_set_wifi, "Wi-Fi  %s", st->wifi_ssid[0] ? st->wifi_ssid : "(none)");

    s_set_note = lv_label_create(s_settings);
    lv_obj_set_style_text_color(s_set_note, c(0x667eea), 0);
    lv_obj_set_style_text_font(s_set_note, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_set_note, 300);
    lv_obj_set_style_text_align(s_set_note, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_set_note, LV_ALIGN_TOP_LEFT, 34, 300);
    lv_label_set_text(s_set_note, "Release volume to hear a tone");

    lv_obj_t *ap = lv_btn_create(s_settings);
    lv_obj_set_size(ap, 128, 56);
    lv_obj_align(ap, LV_ALIGN_BOTTOM_LEFT, 14, -16);
    lv_obj_set_style_bg_color(ap, c(0x2A3344), 0);
    lv_obj_set_style_radius(ap, 14, 0);
    lv_obj_set_style_shadow_width(ap, 0, 0);
    lv_obj_set_ext_click_area(ap, 6);
    lv_obj_add_event_cb(ap, ap_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *apl = lv_label_create(ap);
    lv_label_set_text(apl, "Wi-Fi\nsetup");
    lv_obj_set_style_text_color(apl, c(COL_FG), 0);
    lv_obj_set_style_text_font(apl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(apl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(apl);

    lv_obj_t *batt = lv_btn_create(s_settings);
    lv_obj_set_size(batt, 128, 56);
    lv_obj_align(batt, LV_ALIGN_BOTTOM_LEFT, 150, -16);
    lv_obj_set_style_bg_color(batt, c(0x3A1212), 0);
    lv_obj_set_style_radius(batt, 14, 0);
    lv_obj_set_style_shadow_width(batt, 0, 0);
    lv_obj_set_ext_click_area(batt, 6);
    lv_obj_add_event_cb(batt, batt_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(batt);
    lv_label_set_text(bl, "Battery\nsaver");
    lv_obj_set_style_text_color(bl, c(COL_FG), 0);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(bl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(bl);

    lv_obj_t *xbtn = lv_btn_create(s_settings);
    lv_obj_set_size(xbtn, 52, 52);
    lv_obj_align(xbtn, LV_ALIGN_BOTTOM_RIGHT, -14, -18);
    lv_obj_set_style_bg_color(xbtn, c(COL_RED), 0);
    lv_obj_set_style_radius(xbtn, 26, 0);
    lv_obj_set_style_shadow_width(xbtn, 0, 0);
    lv_obj_set_ext_click_area(xbtn, 8);
    lv_obj_add_event_cb(xbtn, close_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *xl = lv_label_create(xbtn);
    lv_label_set_text(xl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(xl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(xl, c(0xFFFFFF), 0);
    lv_obj_center(xl);

    s_batt_box = lv_obj_create(s_settings);
    lv_obj_remove_style_all(s_batt_box);
    lv_obj_set_size(s_batt_box, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_batt_box, c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_batt_box, LV_OPA_70, 0);
    lv_obj_add_flag(s_batt_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_batt_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_batt_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_batt_box, batt_bg_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bcard = lv_obj_create(s_batt_box);
    lv_obj_remove_style_all(bcard);
    lv_obj_set_size(bcard, 320, 400);
    lv_obj_center(bcard);
    lv_obj_set_style_bg_color(bcard, c(0x1A1A22), 0);
    lv_obj_set_style_bg_opa(bcard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bcard, 16, 0);
    lv_obj_clear_flag(bcard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bcard, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *bt = lv_label_create(bcard);
    lv_label_set_text(bt, "Battery saver");
    lv_obj_set_style_text_font(bt, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(bt, c(COL_FG), 0);
    lv_obj_align(bt, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *bn = lv_label_create(bcard);
    lv_label_set_text(bn, "Shuts the watch off after idle.");
    lv_label_set_long_mode(bn, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bn, 280);
    lv_obj_set_style_text_font(bn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bn, c(COL_MUTED), 0);
    lv_obj_set_style_text_align(bn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(bn, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *list = lv_obj_create(bcard);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 280, 310);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 80);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(list, 8, 0);

    for (unsigned i = 0; i < SETTINGS_AUTO_OFF_COUNT; i++) {
        s_batt_opt[i] = lv_btn_create(list);
        lv_obj_set_size(s_batt_opt[i], 268, 48);
        lv_obj_set_pos(s_batt_opt[i], 6, (lv_coord_t)(i * 56));
        lv_obj_set_style_radius(s_batt_opt[i], 10, 0);
        lv_obj_set_style_shadow_width(s_batt_opt[i], 0, 0);
        lv_obj_add_event_cb(s_batt_opt[i], batt_opt_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *ol = lv_label_create(s_batt_opt[i]);
        lv_label_set_text(ol, settings_auto_off_option_label((uint8_t)i));
        lv_obj_set_style_text_font(ol, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(ol, c(0xFFFFFF), 0);
        lv_obj_center(ol);
    }
    style_batt_opts();

    refresh_tune_labels();
}

static void build_status(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, BOARD_LCD_H_RES, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);

    s_bat_bar = lv_bar_create(bar);
    lv_obj_set_size(s_bat_bar, 32, 16);
    lv_obj_align(s_bat_bar, LV_ALIGN_LEFT_MID, 18, 0);
    lv_bar_set_range(s_bat_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bat_bar, c(0x202028), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bat_bar, c(0x3DDC84), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(s_bat_bar, c(COL_FG), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bat_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_bat_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bat_bar, 3, 0);

    lv_obj_t *tip = lv_obj_create(bar);
    lv_obj_set_size(tip, 3, 8);
    lv_obj_align_to(tip, s_bat_bar, LV_ALIGN_OUT_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(tip, c(COL_FG), 0);
    lv_obj_set_style_border_width(tip, 0, 0);
    lv_obj_set_style_radius(tip, 0, 0);
    lv_obj_set_style_pad_all(tip, 0, 0);

    s_bat_pct = lv_label_create(bar);
    lv_label_set_text(s_bat_pct, "--");
    lv_obj_set_style_text_color(s_bat_pct, c(COL_FG), 0);
    lv_obj_set_style_text_font(s_bat_pct, &lv_font_montserrat_14, 0);
    lv_obj_align_to(s_bat_pct, tip, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

    s_charge = lv_label_create(bar);
    lv_label_set_text(s_charge, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(s_charge, c(COL_FG), 0);
    lv_obj_align_to(s_charge, s_bat_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_charge, LV_OBJ_FLAG_HIDDEN);

    s_clock = lv_label_create(bar);
    lv_label_set_text(s_clock, "--:--");
    lv_obj_set_style_text_color(s_clock, c(COL_FG), 0);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_16, 0);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);

    const int heights[] = {5, 8, 12, 16};
    for (int i = 0; i < 4; i++) {
        s_wifi[i] = lv_obj_create(bar);
        lv_obj_set_size(s_wifi[i], 4, heights[i]);
        lv_obj_align(s_wifi[i], LV_ALIGN_RIGHT_MID, -18 - (3 - i) * 7, (16 - heights[i]) / 2);
        lv_obj_set_style_bg_color(s_wifi[i], c(0x3A3A44), 0);
        lv_obj_set_style_border_width(s_wifi[i], 0, 0);
        lv_obj_set_style_radius(s_wifi[i], 1, 0);
        lv_obj_set_style_pad_all(s_wifi[i], 0, 0);
    }
}

void ui_screens_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, c(COL_BG), 0);
    lv_obj_set_style_bg_opa(s_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    s_galaxy = ui_galaxy_create(s_scr, BOARD_LCD_H_RES, BOARD_LCD_V_RES);

    s_orb = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_orb);
    lv_obj_set_size(s_orb, 148, 148);
    lv_obj_center(s_orb);
    lv_obj_set_style_radius(s_orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_orb, c(COL_IDLE), 0);
    lv_obj_set_style_bg_opa(s_orb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_orb, 0, 0);
    lv_obj_clear_flag(s_orb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_orb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_orb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_orb, orb_cb, LV_EVENT_CLICKED, NULL);

    s_ring = lv_arc_create(s_scr);
    lv_obj_set_size(s_ring, 196, 196);
    lv_obj_center(s_ring);
    lv_arc_set_rotation(s_ring, 270);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_value(s_ring, 0);
    lv_obj_remove_style(s_ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_ring, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, c(0x22222A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, c(COL_REC), LV_PART_INDICATOR);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);

    s_title = lv_label_create(s_scr);
    style_label(s_title, &lv_font_montserrat_28, COL_READY);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_title, LV_ALIGN_BOTTOM_LEFT, 22, -50);
    lv_label_set_text(s_title, "Watch");

    s_body = lv_label_create(s_scr);
    style_label(s_body, &lv_font_montserrat_16, 0xFFFFFF);
    lv_obj_set_width(s_body, 300);
    lv_obj_set_style_max_height(s_body, 48, 0);
    lv_obj_set_style_text_align(s_body, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_body, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_body, LV_ALIGN_BOTTOM_LEFT, 22, -20);
    lv_label_set_text(s_body, "Hold the top button to talk");

    s_reply_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_reply_box);
    lv_obj_set_size(s_reply_box, 320, 232);
    lv_obj_align(s_reply_box, LV_ALIGN_TOP_MID, 0, 108);
    lv_obj_set_style_bg_opa(s_reply_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_reply_box, 4, 0);
    lv_obj_set_scroll_dir(s_reply_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_reply_box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_reply_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_reply_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_reply_box, LV_OBJ_FLAG_CLICKABLE);

    s_reply_lbl = lv_label_create(s_reply_box);
    lv_obj_set_width(s_reply_lbl, 300);
    lv_label_set_long_mode(s_reply_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_reply_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_reply_lbl, c(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_reply_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_reply_lbl, "");
    lv_obj_align(s_reply_lbl, LV_ALIGN_TOP_MID, 0, 0);

    s_queue = lv_label_create(s_scr);
    style_label(s_queue, &lv_font_montserrat_14, COL_MUTED);
    lv_obj_set_width(s_queue, 140);
    lv_obj_set_style_text_align(s_queue, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_queue, LV_ALIGN_BOTTOM_RIGHT, -22, -28);
    lv_label_set_text(s_queue, "");

    s_glyph = lv_label_create(s_scr);
    style_label(s_glyph, &lv_font_montserrat_48, COL_READY);
    lv_obj_align(s_glyph, LV_ALIGN_TOP_MID, 0, 56);
    lv_label_set_text(s_glyph, LV_SYMBOL_OK);
    lv_obj_add_flag(s_glyph, LV_OBJ_FLAG_HIDDEN);

    s_play_bar = lv_bar_create(s_scr);
    lv_obj_set_size(s_play_bar, 280, 6);
    lv_bar_set_range(s_play_bar, 0, 100);
    lv_bar_set_value(s_play_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_play_bar, c(0x22222A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_play_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_play_bar, c(COL_READY), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_play_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_play_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_play_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(s_play_bar, 0, 0);
    lv_obj_set_style_pad_all(s_play_bar, 0, 0);
    lv_obj_clear_flag(s_play_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_play_bar, LV_OBJ_FLAG_HIDDEN);

    s_mic = make_icon_btn(s_scr, COL_MIC, true, LV_ALIGN_TOP_RIGHT, -5, 60, mic_cb);
    add_mic_drawing(s_mic, 0xFFFFFF);

    s_sep = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_sep);
    lv_obj_set_size(s_sep, 20, 1);
    lv_obj_align(s_sep, LV_ALIGN_TOP_RIGHT, -5, 84);
    lv_obj_set_style_bg_color(s_sep, c(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_sep, LV_OPA_COVER, 0);

    s_gear = make_icon_btn(s_scr, 0x404040, true, LV_ALIGN_TOP_RIGHT, -5, 89, gear_cb);
    lv_obj_t *gl = lv_label_create(s_gear);
    lv_label_set_text(gl, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_font(gl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gl, c(0xFFFFFF), 0);
    lv_obj_center(gl);

    s_power = make_icon_btn(s_scr, COL_RED, true, LV_ALIGN_BOTTOM_RIGHT, -5, -60, power_cb);
    lv_obj_t *pl = lv_label_create(s_power);
    lv_label_set_text(pl, LV_SYMBOL_POWER);
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pl, c(0xFFFFFF), 0);
    lv_obj_center(pl);

    s_sep_bot = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_sep_bot);
    lv_obj_set_size(s_sep_bot, 20, 1);
    lv_obj_align(s_sep_bot, LV_ALIGN_BOTTOM_RIGHT, -5, -86);
    lv_obj_set_style_bg_color(s_sep_bot, c(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_sep_bot, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_sep_bot, LV_OBJ_FLAG_HIDDEN);

    s_tick = make_icon_btn(s_scr, 0x000000, false, LV_ALIGN_TOP_RIGHT, -5, 78, confirm_cb);
    lv_obj_t *tl = lv_label_create(s_tick);
    lv_label_set_text(tl, LV_SYMBOL_OK);
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tl, c(COL_READY), 0);
    lv_obj_center(tl);
    lv_obj_add_flag(s_tick, LV_OBJ_FLAG_HIDDEN);

    s_cancel = make_icon_btn(s_scr, COL_RED, true, LV_ALIGN_BOTTOM_RIGHT, -5, -60, cancel_cb);
    lv_obj_t *cl = lv_label_create(s_cancel);
    lv_label_set_text(cl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cl, c(0xFFFFFF), 0);
    lv_obj_center(cl);
    lv_obj_add_flag(s_cancel, LV_OBJ_FLAG_HIDDEN);

    build_status(s_scr);

    s_sleep_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_sleep_box);
    lv_obj_set_size(s_sleep_box, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_sleep_box, c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_sleep_box, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_sleep_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_sleep_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_sleep_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_sleep_box, close_settings_cb, LV_EVENT_CLICKED, NULL);

    s_sleep_time = lv_label_create(s_sleep_box);
    lv_label_set_text(s_sleep_time, "--:--");
    lv_obj_set_style_text_font(s_sleep_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_sleep_time, c(COL_FG), 0);
    lv_obj_align(s_sleep_time, LV_ALIGN_CENTER, 0, -36);

    s_sleep_date = lv_label_create(s_sleep_box);
    lv_label_set_text(s_sleep_date, "");
    lv_obj_set_style_text_font(s_sleep_date, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_sleep_date, c(0x6A6A72), 0);
    lv_obj_align(s_sleep_date, LV_ALIGN_CENTER, 0, 28);

    lv_obj_t *wake = lv_label_create(s_sleep_box);
    lv_label_set_text(wake, "Tap to wake");
    lv_obj_set_style_text_font(wake, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wake, c(0x4A4A52), 0);
    lv_obj_align(wake, LV_ALIGN_BOTTOM_MID, 0, -40);

    s_pwr_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_pwr_box);
    lv_obj_set_size(s_pwr_box, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_pwr_box, c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pwr_box, LV_OPA_70, 0);
    lv_obj_add_flag(s_pwr_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pwr_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pwr_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_pwr_box, pwr_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(s_pwr_box);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, 220);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, c(0x1A1A22), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *pt = lv_label_create(card);
    lv_label_set_text(pt, "Power off?");
    lv_obj_set_style_text_font(pt, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(pt, c(COL_FG), 0);
    lv_obj_align(pt, LV_ALIGN_TOP_MID, 0, 22);

    s_pwr_note = lv_label_create(card);
    lv_label_set_text(s_pwr_note, "The watch will shut down.");
    lv_obj_set_style_text_font(s_pwr_note, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_pwr_note, c(COL_MUTED), 0);
    lv_obj_set_style_text_align(s_pwr_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_pwr_note, 260);
    lv_obj_align(s_pwr_note, LV_ALIGN_TOP_MID, 0, 64);

    lv_obj_t *off = lv_btn_create(card);
    lv_obj_set_size(off, 240, 44);
    lv_obj_align(off, LV_ALIGN_BOTTOM_MID, 0, -62);
    lv_obj_set_style_bg_color(off, c(COL_RED), 0);
    lv_obj_set_style_radius(off, 10, 0);
    lv_obj_set_style_shadow_width(off, 0, 0);
    lv_obj_add_event_cb(off, pwr_off_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *off_l = lv_label_create(off);
    lv_label_set_text(off_l, "Power off");
    lv_obj_set_style_text_font(off_l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(off_l, c(0xFFFFFF), 0);
    lv_obj_center(off_l);

    lv_obj_t *can = lv_btn_create(card);
    lv_obj_set_size(can, 240, 40);
    lv_obj_align(can, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_color(can, c(0x3A3A44), 0);
    lv_obj_set_style_radius(can, 10, 0);
    lv_obj_set_style_shadow_width(can, 0, 0);
    lv_obj_add_event_cb(can, pwr_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *can_l = lv_label_create(can);
    lv_label_set_text(can_l, "Cancel");
    lv_obj_set_style_text_font(can_l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(can_l, c(COL_FG), 0);
    lv_obj_center(can_l);

    s_qdlg_box = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_qdlg_box);
    lv_obj_set_size(s_qdlg_box, BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    lv_obj_set_style_bg_color(s_qdlg_box, c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_qdlg_box, LV_OPA_70, 0);
    lv_obj_add_flag(s_qdlg_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_qdlg_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_qdlg_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_qdlg_box, qdlg_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *qcard = lv_obj_create(s_qdlg_box);
    lv_obj_remove_style_all(qcard);
    lv_obj_set_size(qcard, 312, 292);
    lv_obj_center(qcard);
    lv_obj_set_style_bg_color(qcard, c(0x1A1A22), 0);
    lv_obj_set_style_bg_opa(qcard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(qcard, 16, 0);
    lv_obj_clear_flag(qcard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(qcard, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *qt = lv_label_create(qcard);
    lv_label_set_text(qt, "Queue");
    lv_obj_set_style_text_font(qt, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(qt, c(COL_FG), 0);
    lv_obj_align(qt, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *qn = lv_label_create(qcard);
    lv_label_set_text(qn, "Oldest clip is sent first.");
    lv_obj_set_style_text_font(qn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(qn, c(COL_MUTED), 0);
    lv_obj_align(qn, LV_ALIGN_TOP_MID, 0, 54);

    s_qdlg_one_l = make_stack_btn(qcard, "Erase that 1 file", COL_RED, 92, qdlg_one_cb);
    make_stack_btn(qcard, "Erase full queue", 0x8A1010, 148, qdlg_all_cb);
    make_stack_btn(qcard, "Cancel", 0x3A3A44, 204, qdlg_cancel_cb);

    s_dot = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, 18, 18);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_dot, c(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);

    build_settings(s_scr);
    lv_scr_load(s_scr);
}

void ui_show_touch(uint16_t x, uint16_t y, bool down)
{
    if (!s_dot || s_settings_open || s_cur == UI_SCREEN_SLEEP) {
        if (s_dot) {
            lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (!down) {
        lv_obj_add_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_dot, (lv_coord_t)(x > 9 ? x - 9 : 0), (lv_coord_t)(y > 9 ? y - 9 : 0));
}

void ui_settings_open(void)
{
    s_settings_open = true;
    ui_galaxy_pause(true);
    ui_battery_dialog_close();
    const settings_t *st = settings_get();
    lv_slider_set_value(s_sl_bright, st->brightness, LV_ANIM_OFF);
    lv_slider_set_value(s_sl_vol, st->volume, LV_ANIM_OFF);
    lv_slider_set_value(s_sl_mic, st->mic_gain, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_set_wifi, "Wi-Fi  %s", st->wifi_ssid[0] ? st->wifi_ssid : "(none)");
    if (s_set_note) {
        lv_label_set_text_fmt(s_set_note, "Auto off  %s", settings_auto_off_label());
    }
    refresh_tune_labels();
    lv_obj_clear_flag(s_settings, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_settings);
}

void ui_settings_close(void)
{
    if (s_settings_open) {
        persist_tune();
    }
    ui_battery_dialog_close();
    s_settings_open = false;
    lv_obj_add_flag(s_settings, LV_OBJ_FLAG_HIDDEN);
}

void ui_settings_set_note(const char *msg)
{
    if (s_set_note && msg) {
        lv_label_set_text(s_set_note, msg);
    }
}

bool ui_settings_is_open(void)
{
    return s_settings_open;
}

void ui_power_dialog_open(bool vbus)
{
    if (!s_pwr_box) {
        return;
    }
    if (s_pwr_note) {
        lv_label_set_text(s_pwr_note,
                          vbus ? "Unplug USB first or it may restart."
                               : "The watch will shut down.");
    }
    s_pwr_open = true;
    lv_obj_clear_flag(s_pwr_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_pwr_box);
}

void ui_power_dialog_close(void)
{
    s_pwr_open = false;
    if (s_pwr_box) {
        lv_obj_add_flag(s_pwr_box, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_power_dialog_is_open(void)
{
    return s_pwr_open;
}

void ui_queue_dialog_open(int count)
{
    if (!s_qdlg_box) {
        return;
    }
    ui_power_dialog_close();
    if (s_qdlg_one_l) {
        lv_label_set_text(s_qdlg_one_l, "Erase that 1 file");
    }
    (void)count;
    s_qdlg_open = true;
    lv_obj_clear_flag(s_qdlg_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_qdlg_box);
}

void ui_queue_dialog_close(void)
{
    s_qdlg_open = false;
    if (s_qdlg_box) {
        lv_obj_add_flag(s_qdlg_box, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_queue_dialog_is_open(void)
{
    return s_qdlg_open;
}

void ui_battery_dialog_open(void)
{
    if (!s_batt_box) {
        return;
    }
    style_batt_opts();
    s_batt_open = true;
    lv_obj_clear_flag(s_batt_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_batt_box);
}

void ui_battery_dialog_close(void)
{
    s_batt_open = false;
    if (s_batt_box) {
        lv_obj_add_flag(s_batt_box, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_battery_dialog_is_open(void)
{
    return s_batt_open;
}

bool ui_is_sleep_screen(void)
{
    return s_cur == UI_SCREEN_SLEEP;
}

void ui_note_activity(void)
{
    app_touch_activity();
}

void ui_show(ui_screen_t screen)
{
    ESP_LOGI(TAG_UI, "show %d", (int)screen);
    s_cur = screen;
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
    vis(s_play_bar, false);
    vis(s_reply_box, false);
    lv_obj_add_flag(s_sleep_box, LV_OBJ_FLAG_HIDDEN);
    ui_power_dialog_close();
    ui_queue_dialog_close();
    stop_breath();
    lv_obj_add_flag(s_orb, LV_OBJ_FLAG_HIDDEN);
    vis(s_queue, false);
    ui_galaxy_set_visible(false);
    if (screen != UI_SCREEN_SETTINGS && s_settings_open) {
        ui_settings_close();
    }

    switch (screen) {
    case UI_SCREEN_BOOT:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Watch");
        lv_label_set_text(s_body, "Starting");
        start_breath(COL_IDLE);
        break;
    case UI_SCREEN_IDLE:
        ui_galaxy_set_visible(true);
        layout_bottom_text(true);
        set_side_icons(true, true, true, true, false, false);
        lv_label_set_text(s_title, "Ready");
        lv_label_set_text(s_body, "Hold top to talk");
        lv_label_set_text(s_queue, "");
        s_spoken[0] = 0;
        break;
    case UI_SCREEN_RECORDING:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_obj_clear_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_arc_color(s_ring, c(COL_REC), LV_PART_INDICATOR);
        lv_label_set_text(s_title, "Listening");
        lv_label_set_text(s_body, "Tap orb or top to send");
        start_breath(COL_REC);
        break;
    case UI_SCREEN_UPLOADING:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Sending");
        lv_label_set_text(s_body, "Uploading to the shop");
        start_breath(COL_SEND);
        break;
    case UI_SCREEN_PROCESSING:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Processing");
        lv_label_set_text(s_body, "Shop is working on it");
        start_breath(COL_PROC);
        break;
    case UI_SCREEN_DOWNLOADING:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Receiving");
        lv_label_set_text(s_body, "Fetching the reply");
        start_breath(COL_RECV);
        break;
    case UI_SCREEN_PLAYING_SAY:
        set_side_icons(false, false, false, false, false, false);
        layout_reply_text(COL_READY, LV_SYMBOL_OK);
        break;
    case UI_SCREEN_PLAYING_ASK:
        set_side_icons(false, false, true, false, false, true);
        layout_reply_text(COL_ASK, "?");
        break;
    case UI_SCREEN_PLAYING_CONFIRM:
        set_side_icons(false, false, true, false, true, true);
        layout_reply_text(COL_READY, LV_SYMBOL_OK);
        break;
    case UI_SCREEN_QUEUE:
        ui_galaxy_set_visible(true);
        layout_bottom_text(false);
        set_side_icons(true, true, true, true, false, true);
        style_cancel_for_queue(true);
        vis(s_sep_bot, true);
        vis(s_queue, true);
        lv_label_set_text(s_title, "Queue");
        lv_label_set_text(s_body, "Tap top to send oldest");
        break;
    case UI_SCREEN_SETTINGS:
        ui_galaxy_set_visible(true);
        ui_galaxy_pause(true);
        ui_settings_open();
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Settings");
        lv_label_set_text(s_body, "");
        break;
    case UI_SCREEN_SLEEP:
        ESP_LOGI(TAG_UI, "sleep overlay");
        lv_obj_clear_flag(s_sleep_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_sleep_box);
        lv_label_set_text(s_sleep_time, s_time);
        lv_label_set_text(s_sleep_date, s_date);
        break;
    case UI_SCREEN_ERROR:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Problem");
        if (!s_spoken[0]) {
            lv_label_set_text(s_body, "Could not save recording");
        }
        start_breath(COL_REC);
        break;
    case UI_SCREEN_PROVISION:
        layout_bottom_text(false);
        set_side_icons(false, false, false, false, false, false);
        lv_label_set_text(s_title, "Setup");
        lv_label_set_text(s_body, "Join Wi-Fi  ESP Assistant");
        start_breath(COL_NET);
        break;
    }
    ESP_LOGI(TAG_UI, "show %d done", (int)screen);
}

void ui_set_status(const ui_status_t *status)
{
    if (!status) {
        return;
    }
    s_battery = status->battery_percent;
    s_wifi_bars = status->wifi_bars;
    s_charging = status->charging;
    s_vbus = status->vbus;
    s_wifi_on = status->wifi_connected;
    if (status->time[0]) {
        strncpy(s_time, status->time, sizeof(s_time) - 1);
        s_time[sizeof(s_time) - 1] = 0;
    }
    if (status->date[0]) {
        strncpy(s_date, status->date, sizeof(s_date) - 1);
        s_date[sizeof(s_date) - 1] = 0;
    }
    refresh_status();
}

void ui_set_message(const char *title, const char *body)
{
    if (title && s_cur != UI_SCREEN_PLAYING_SAY && s_cur != UI_SCREEN_PLAYING_ASK &&
        s_cur != UI_SCREEN_PLAYING_CONFIRM) {
        lv_label_set_text(s_title, title);
    }
    if (body) {
        strncpy(s_spoken, body, sizeof(s_spoken) - 1);
        s_spoken[sizeof(s_spoken) - 1] = 0;
        lv_label_set_text(s_body, s_spoken);
        if (s_cur == UI_SCREEN_PLAYING_SAY || s_cur == UI_SCREEN_PLAYING_ASK ||
            s_cur == UI_SCREEN_PLAYING_CONFIRM) {
            vis(s_body, false);
            vis(s_reply_box, true);
            if (s_reply_lbl) {
                lv_obj_set_style_text_font(s_reply_lbl, &lv_font_montserrat_28, 0);
                lv_obj_set_style_text_color(s_reply_lbl, c(0xFFFFFF), 0);
                lv_label_set_text(s_reply_lbl, s_spoken);
            }
            layout_play_bar();
        } else {
            lv_obj_set_style_text_color(s_body, c(COL_FG), 0);
        }
    }
}

void ui_set_play_progress(int percent)
{
    if (!s_play_bar) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    vis(s_play_bar, true);
    lv_bar_set_value(s_play_bar, percent, LV_ANIM_OFF);
    reply_scroll_to(percent);
}

void ui_set_level(float level)
{
    if (level < 0) {
        level = 0;
    }
    if (level > 1) {
        level = 1;
    }
    lv_arc_set_value(s_ring, (int16_t)(level * 100));
}

void ui_set_queue_info(int count, const char *list)
{
    char line[32];
    (void)list;
    snprintf(line, sizeof(line), "%d queued", count);
    lv_label_set_text(s_queue, line);
    lv_obj_align(s_queue, LV_ALIGN_BOTTOM_RIGHT, -22, -28);
}
