#include "ui.h"
#include "board.h"
#include "display.h"
#include "board_pins.h"

extern bool net_is_connected(void);
extern int net_wifi_bars(void);

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "ui";

#define LVGL_TICK_MS 2
#define BUF_LINES 40

static SemaphoreHandle_t s_mux;
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;
static volatile uint32_t s_lvgl_loops;
static volatile uint32_t s_flush_n;
static volatile int s_in_handler;
static volatile int s_in_flush;
static volatile int s_fx1, s_fy1, s_fx2, s_fy2;
static SemaphoreHandle_t s_flush_sem;

void ui_lock(void)
{
    xSemaphoreTake(s_mux, portMAX_DELAY);
}

bool ui_lock_timeout(uint32_t ms)
{
    return xSemaphoreTake(s_mux, pdMS_TO_TICKS(ms)) == pdTRUE;
}

void ui_unlock(void)
{
    xSemaphoreGive(s_mux);
}

static bool flush_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    (void)io;
    (void)edata;
    BaseType_t hp = pdFALSE;
    lv_disp_flush_ready((lv_disp_drv_t *)ctx);
    s_in_flush = 0;
    s_flush_n++;
    if (s_flush_sem) {
        xSemaphoreGiveFromISR(s_flush_sem, &hp);
    }
    return hp == pdTRUE;
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2 + 1;
    int y2 = area->y2 + 1;
    if (x1 < 0) {
        x1 = 0;
    }
    if (y1 < 0) {
        y1 = 0;
    }
    if (x2 > BOARD_LCD_H_RES) {
        x2 = BOARD_LCD_H_RES;
    }
    if (y2 > BOARD_LCD_V_RES) {
        y2 = BOARD_LCD_V_RES;
    }
    s_fx1 = x1;
    s_fy1 = y1;
    s_fx2 = x2;
    s_fy2 = y2;
    if (x2 <= x1 || y2 <= y1) {
        ESP_LOGW(TAG, "skip flush %d,%d-%d,%d", x1, y1, x2, y2);
        lv_disp_flush_ready(drv);
        return;
    }
    if (s_flush_sem) {
        xSemaphoreTake(s_flush_sem, 0);
    }
    s_in_flush = 1;
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)drv->user_data;
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, color_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "draw_bitmap %s %d,%d-%d,%d", esp_err_to_name(err), x1, y1, x2, y2);
        s_in_flush = 0;
        lv_disp_flush_ready(drv);
        return;
    }
    if (s_flush_sem && xSemaphoreTake(s_flush_sem, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGE(TAG, "flush timeout %d,%d-%d,%d", x1, y1, x2, y2);
        s_in_flush = 0;
        lv_disp_flush_ready(drv);
    }
}

static void rounder_cb(lv_disp_drv_t *drv, lv_area_t *area)
{
    (void)drv;
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static void touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    uint16_t x = 0, y = 0;
    static bool was_down;
    if (app_sleep_input_blocked()) {
        data->state = LV_INDEV_STATE_RELEASED;
        ui_show_touch(0, 0, false);
        was_down = false;
        return;
    }
    if (display_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        ui_show_touch(x, y, true);
        if (!was_down) {
            ui_note_activity();
        }
        was_down = true;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        ui_show_touch(0, 0, false);
        was_down = false;
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
}

static void hb_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "hb loops=%u handler=%d flush=%d flushes=%u last=%d,%d-%d,%d",
             (unsigned)s_lvgl_loops, s_in_handler, s_in_flush, (unsigned)s_flush_n,
             s_fx1, s_fy1, s_fx2, s_fy2);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        ui_lock();
        int64_t t0 = esp_timer_get_time();
        s_in_handler = 1;
        uint32_t delay = lv_timer_handler();
        s_in_handler = 0;
        s_lvgl_loops++;
        int64_t dt = esp_timer_get_time() - t0;
        if (dt > 40000) {
            ESP_LOGW(TAG, "lvgl slow %d ms heap %u", (int)(dt / 1000),
                     (unsigned)esp_get_free_heap_size());
        }
        static int64_t last_clock;
        static int64_t last_beat;
        int64_t now = esp_timer_get_time();
        if (now - last_beat > 10000000) {
            last_beat = now;
            ESP_LOGI(TAG, "alive lvgl=%d ms heap=%u", (int)(dt / 1000),
                     (unsigned)esp_get_free_heap_size());
        }
        if (now - last_clock > 1000000) {
            last_clock = now;
            ui_status_t st = { 0 };
            time_t t = time(NULL);
            if (t > 1700000000) {
                struct tm tm;
                localtime_r(&t, &tm);
                const bool sleep = ui_is_sleep_screen();
                if (sleep) {
                    snprintf(st.time, sizeof(st.time), "%02u:%02u:%02u",
                             (unsigned)tm.tm_hour % 24u, (unsigned)tm.tm_min % 60u,
                             (unsigned)tm.tm_sec % 60u);
                } else {
                    snprintf(st.time, sizeof(st.time), "%02u:%02u",
                             (unsigned)tm.tm_hour % 24u, (unsigned)tm.tm_min % 60u);
                }
                snprintf(st.date, sizeof(st.date), "%02u / %02u", (unsigned)tm.tm_mday % 32u, (unsigned)(tm.tm_mon + 1) % 13u);
            } else {
                snprintf(st.time, sizeof(st.time), "--:--");
                st.date[0] = 0;
            }
            /* I2C while holding the LVGL lock wedges sleep/wake. Skip it on the clock. */
            if (!ui_is_sleep_screen()) {
                st.battery_percent = board_battery_percent();
                st.charging = board_battery_charging();
                st.vbus = board_vbus_in();
                st.wifi_connected = net_is_connected();
                st.wifi_bars = net_wifi_bars();
            } else {
                st.battery_percent = -1;
            }
            ui_set_status(&st);
        }
        ui_unlock();
        display_apply_pending_brightness();
        if (delay < 1) {
            delay = 1;
        }
        if (delay > 50) {
            delay = 50;
        }
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

extern void ui_screens_create(void);

esp_err_t ui_init(void)
{
    lv_init();
    const size_t buf_px = BOARD_LCD_H_RES * BUF_LINES;
    lv_color_t *buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(buf1, ESP_ERR_NO_MEM, TAG, "lvgl buf");
    lv_disp_draw_buf_init(&s_draw_buf, buf1, NULL, buf_px);
    ESP_LOGI(TAG, "lvgl buf %u bytes, internal heap %u",
             (unsigned)(buf_px * sizeof(lv_color_t)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    s_flush_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_sem, ESP_ERR_NO_MEM, TAG, "flush sem");

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = BOARD_LCD_H_RES;
    s_disp_drv.ver_res = BOARD_LCD_V_RES;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.rounder_cb = rounder_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    ESP_RETURN_ON_ERROR(display_init(flush_done_cb, &s_disp_drv), TAG, "display");
    s_disp_drv.user_data = display_panel();
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_cb;
    lv_indev_drv_register(&s_indev_drv);

    const esp_timer_create_args_t tick_args = {
        .callback = tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick), TAG, "tick");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick, LVGL_TICK_MS * 1000), TAG, "tick start");

    const esp_timer_create_args_t hb_args = {
        .callback = hb_cb,
        .name = "ui_hb",
    };
    esp_timer_handle_t hb;
    ESP_RETURN_ON_ERROR(esp_timer_create(&hb_args, &hb), TAG, "hb");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(hb, 2000000), TAG, "hb start");

    s_mux = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_mux, ESP_ERR_NO_MEM, TAG, "mux");

    ui_lock();
    ui_screens_create();
    ui_unlock();

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 12 * 1024, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "LVGL 8 port running on core 1");
    return ESP_OK;
}
