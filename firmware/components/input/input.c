#include "input.h"
#include "app_events.h"
#include "board.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input";

#define DEBOUNCE_US        40000
#define HOLD_RECORD_US     500000
#define HOLD_POWER_US      3000000
#define TRIPLE_WINDOW_US   700000

static int64_t top_down_us;
static int64_t top_last_click_us;
static int top_clicks;
static bool top_held;
static bool top_down;

static int64_t bot_down_us;
static bool bot_held;
static bool bot_down;
static bool bot_wake_click; /* press already woke sleep; swallow the matching click */

static bool top_pressed(void)
{
    return gpio_get_level(BOARD_BTN_TOP) == 0;
}

static void input_task(void *arg)
{
    (void)arg;
    while (1) {
        const int64_t now = esp_timer_get_time();
        const bool t = top_pressed();
        const bool b = board_bottom_button_pressed();

        if (t && !top_down) {
            top_down = true;
            top_held = false;
            top_down_us = now;
            if (app_state() == APP_STATE_SLEEP) {
                ESP_LOGI(TAG, "top press wake");
                app_post(APP_EVT_WAKE);
            }
        } else if (t && top_down && !top_held && (now - top_down_us) > HOLD_RECORD_US) {
            top_held = true;
            top_clicks = 0;
            app_post(APP_EVT_BTN_TOP_HOLD);
        } else if (!t && top_down) {
            top_down = false;
            if (top_held) {
                app_post(APP_EVT_BTN_TOP_RELEASE);
            } else if ((now - top_down_us) > DEBOUNCE_US) {
                if (now - top_last_click_us < TRIPLE_WINDOW_US) {
                    top_clicks++;
                } else {
                    top_clicks = 1;
                }
                top_last_click_us = now;
                if (top_clicks >= 3) {
                    top_clicks = 0;
                    app_post(APP_EVT_BTN_TOP_TRIPLE);
                }
            }
        } else if (!t && top_clicks == 1 && (now - top_last_click_us) > TRIPLE_WINDOW_US) {
            top_clicks = 0;
            app_post(APP_EVT_BTN_TOP_CLICK);
        }

        if (b && !bot_down) {
            bot_down = true;
            bot_held = false;
            bot_wake_click = false;
            bot_down_us = now;
            if (app_state() == APP_STATE_SLEEP) {
                ESP_LOGI(TAG, "bottom press wake");
                bot_wake_click = true;
                app_post(APP_EVT_WAKE);
            }
        } else if (b && bot_down && !bot_held && (now - bot_down_us) > HOLD_POWER_US) {
            bot_held = true;
            bot_wake_click = false;
            app_post(APP_EVT_BTN_BOTTOM_HOLD_POWER);
        } else if (!b && bot_down) {
            bot_down = false;
            if (bot_wake_click) {
                bot_wake_click = false;
                ESP_LOGI(TAG, "bottom click swallowed after sleep wake");
            } else if (!bot_held && (now - bot_down_us) > DEBOUNCE_US) {
                app_post(APP_EVT_BTN_BOTTOM_CLICK);
            }
        }

        if (app_state() == APP_STATE_SLEEP && board_imu_shake() && !app_sleep_input_blocked()) {
            ESP_LOGI(TAG, "imu shake wake");
            app_post(APP_EVT_WAKE);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t input_init(void)
{
    xTaskCreate(input_task, "input", 3 * 1024, NULL, 6, NULL);
    ESP_LOGI(TAG, "buttons: GPIO0 top, expander IO4 bottom");
    return ESP_OK;
}
