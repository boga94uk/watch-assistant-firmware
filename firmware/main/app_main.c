#include "app_events.h"
#include "audio.h"
#include "board.h"
#include "console_hw.h"
#include "display.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "input.h"
#include "net.h"
#include "nvs_flash.h"
#include "provision.h"
#include "recq.h"
#include "sd.h"
#include "settings.h"
#include "ui.h"
#include "esp_ota_ops.h"

static const char *TAG = "watch";

void app_main(void)
{
    ESP_LOGI(TAG, "watch_assistant");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(board_init(NULL));
    ESP_ERROR_CHECK(settings_init());
    if (sd_mount() != ESP_OK) {
        ESP_LOGW(TAG, "no SD card");
    }
    recq_init();

    const bool setup = provision_boot_hold() || !settings_has_wifi();

    err = audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(err));
    }

    err = ui_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ui init failed: %s", esp_err_to_name(err));
    } else {
        display_set_brightness(settings_get()->brightness);
    }
    ESP_ERROR_CHECK(input_init());
    ESP_ERROR_CHECK(app_start());
    console_hw_start();
    /* New OTA image is pending-verify until this succeeds. Failed boots roll back. */
    if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
        ESP_LOGW(TAG, "OTA mark-valid skipped");
    }

    if (setup) {
        ESP_LOGW(TAG, "provisioning AP");
        ESP_ERROR_CHECK(provision_start());
        app_post(APP_EVT_ENTER_SETUP);
    } else {
        ESP_ERROR_CHECK(net_init());
        app_post(APP_EVT_BOOT_DONE);
    }
}
