#include "sd.h"
#include "board_pins.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include <string.h>

static const char *TAG = "sd";
static sdmmc_card_t *s_card;
static bool s_ok;

esp_err_t sd_mount(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false,
        .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
        s_ok = false;
        return err;
    }
    s_ok = true;
    ESP_LOGI(TAG, "mounted /sdcard");
    return ESP_OK;
}

esp_err_t sd_format(void)
{
    ESP_LOGW(TAG, "formatting /sdcard");
    if (s_ok && s_card) {
        esp_err_t err = esp_vfs_fat_sdcard_format("/sdcard", s_card);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "format %s", esp_err_to_name(err));
            s_ok = false;
            return err;
        }
        ESP_LOGI(TAG, "format ok");
        return ESP_OK;
    }
    /* Not mounted: remount and format if the filesystem is missing. */
    if (s_card) {
        (void)esp_vfs_fat_sdcard_unmount("/sdcard", s_card);
        s_card = NULL;
        s_ok = false;
    }
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = true,
        .max_files = 16,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount, &s_card);
    s_ok = (err == ESP_OK);
    ESP_LOGI(TAG, "format remount %s", esp_err_to_name(err));
    return err;
}

bool sd_ok(void)
{
    return s_ok;
}

const char *sd_root(void)
{
    return "/sdcard";
}

bool sd_path_ok(const char *path)
{
    if (!path || !s_ok) {
        return false;
    }
    if (strncmp(path, "/sdcard", 7) != 0) {
        return false;
    }
    if (strstr(path, "..")) {
        return false;
    }
    return true;
}
