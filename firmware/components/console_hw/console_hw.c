#include "console_hw.h"
#include "app_events.h"
#include "board.h"
#include "board_pins.h"
#include "net.h"
#include "recq.h"
#include "sd.h"
#include "settings.h"
#include "ui.h"
#include "wav_util.h"
#include "esp_heap_caps.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "hwtest";

static int cmd_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint8_t found[16];
    int n = board_i2c_scan(found, 16);
    printf("WATCH_ASSISTANT status\n");
    printf("state=%s\n", app_state_name(app_state()));
    printf("i2c_count=%d\n", n);
    for (int i = 0; i < n; i++) {
        printf("i2c=0x%02X\n", found[i]);
    }
    printf("top=%d bottom=%d\n", board_top_button_pressed(), board_bottom_button_pressed());
    printf("wifi_ssid=%s\n", settings_get()->wifi_ssid);
    printf("wifi_up=%d bars=%d rssi=%d\n", net_is_connected(), net_wifi_bars(), net_wifi_rssi());
    printf("battery=%d%% mv=%d charging=%d vbus=%d\n",
           board_battery_percent(), board_battery_mv(),
           board_battery_charging(), board_vbus_in());
    printf("TEST_LINE ok status\n");
    return 0;
}

static int cmd_test(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const bool es = board_i2c_has(BOARD_ES8311_ADDR);
    const bool exp = board_i2c_has(BOARD_EXPANDER_ADDR);
    const bool axp = board_i2c_has(BOARD_AXP2101_ADDR);
    const bool tp = board_i2c_has(BOARD_TOUCH_I2C_ADDR);
    const bool rtc = board_i2c_has(BOARD_RTC_ADDR);
    const bool imu = board_i2c_has(BOARD_IMU_ADDR);
    const int pass = es && exp && tp;
    printf("TEST_RESULT {\"es8311\":%s,\"tca9554\":%s,\"axp2101\":%s,\"touch\":%s,\"rtc\":%s,\"imu\":%s,\"pass\":%s}\n",
           es ? "true" : "false",
           exp ? "true" : "false",
           axp ? "true" : "false",
           tp ? "true" : "false",
           rtc ? "true" : "false",
           imu ? "true" : "false",
           pass ? "true" : "false");
    return pass ? 0 : 1;
}

static int cmd_screen(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: screen idle|rec|up|proc|down|say|ask|confirm|queue|settings|sleep|error|setup\n");
        return 1;
    }
    ui_screen_t s = UI_SCREEN_IDLE;
    if (!strcmp(argv[1], "rec")) {
        s = UI_SCREEN_RECORDING;
    } else if (!strcmp(argv[1], "up")) {
        s = UI_SCREEN_UPLOADING;
    } else if (!strcmp(argv[1], "proc")) {
        s = UI_SCREEN_PROCESSING;
    } else if (!strcmp(argv[1], "down")) {
        s = UI_SCREEN_DOWNLOADING;
    } else if (!strcmp(argv[1], "say")) {
        s = UI_SCREEN_PLAYING_SAY;
    } else if (!strcmp(argv[1], "ask")) {
        s = UI_SCREEN_PLAYING_ASK;
    } else if (!strcmp(argv[1], "confirm")) {
        s = UI_SCREEN_PLAYING_CONFIRM;
    } else if (!strcmp(argv[1], "queue")) {
        s = UI_SCREEN_QUEUE;
    } else if (!strcmp(argv[1], "settings")) {
        s = UI_SCREEN_SETTINGS;
    } else if (!strcmp(argv[1], "sleep")) {
        s = UI_SCREEN_SLEEP;
    } else if (!strcmp(argv[1], "error")) {
        s = UI_SCREEN_ERROR;
    } else if (!strcmp(argv[1], "setup")) {
        s = UI_SCREEN_PROVISION;
    }
    ui_lock();
    ui_show(s);
    ui_unlock();
    printf("TEST_LINE ok screen %s\n", argv[1]);
    return 0;
}

static int cmd_queue(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    recq_dump();
    printf("sd_ok=%d heap_spiram=%u\n", (int)sd_ok(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    printf("TEST_LINE ok queue\n");
    return 0;
}

static int cmd_qpush(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const size_t pcm = 16000;
    const size_t len = 44 + pcm;
    uint8_t *wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav) {
        printf("qpush oom %u\n", (unsigned)len);
        return 1;
    }
    memset(wav + 44, 0, pcm);
    wav_write_header(wav, (uint32_t)pcm, 16000);
    esp_err_t err = recq_push(wav, len);
    printf("qpush err=%s last=%s\n", esp_err_to_name(err), recq_last_error());
    recq_dump();
    free(wav);
    printf("TEST_LINE %s qpush\n", err == ESP_OK ? "ok" : "fail");
    return err == ESP_OK ? 0 : 1;
}

esp_err_t console_hw_start(void)
{
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "watch>";
    repl_config.max_cmdline_length = 128;

    const esp_console_cmd_t cmds[] = {
        { .command = "status", .help = "Print device status", .hint = NULL, .func = cmd_status },
        { .command = "test", .help = "I2C self-test JSON", .hint = NULL, .func = cmd_test },
        { .command = "screen", .help = "Force a UI screen", .hint = "<name>", .func = cmd_screen },
        { .command = "queue", .help = "Dump recording queue", .hint = NULL, .func = cmd_queue },
        { .command = "qpush", .help = "Try saving a dummy WAV", .hint = NULL, .func = cmd_qpush },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_register_help_command());

    esp_console_repl_t *repl = NULL;
    esp_console_dev_usb_serial_jtag_config_t hw = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&hw, &repl_config, &repl);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "USB Serial/JTAG REPL failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "HWTEST listen — type 'test' or 'help'");
    printf("WATCH_ASSISTANT ready\nHWTEST listen\n");
    return esp_console_start_repl(repl);
}
