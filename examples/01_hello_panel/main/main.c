/*
 * 01_hello_panel — the smallest complete connected HMI.
 *
 * Brings up the display and touch, connects to Hyperwisor, and shows the three
 * things you need before anything else can be debugged: who this device says it
 * is, whether it reached the relay, and what to do next.
 *
 * There is deliberately no UI framework above LVGL. This repository gives you a
 * board and a connection; the screens are yours.
 *
 * What to copy from this file:
 *   - the order of bring-up: NVS before the RGB panel (see app_main)
 *   - taking the LVGL lock around every UI touch from another task
 *   - reading connection state under the state lock, not from a loose bool
 */

#include "bsp.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "hyperwisor.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hello_panel";

static lv_obj_t *s_status;      /* connection state                */
static lv_obj_t *s_device;      /* device id, as the relay sees it */
static lv_obj_t *s_hint;        /* what to do next                 */

static void build_ui(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x15212d), 0);

    lv_obj_t *col = lv_obj_create(scr);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 14, 0);

    lv_obj_t *title = lv_label_create(col);
    lv_label_set_text(title, "HYPERWISOR HMI");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe1e7ee), 0);

    s_status = lv_label_create(col);
    lv_label_set_text(s_status, "STARTING");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x8f5a00), 0);

    s_device = lv_label_create(col);
    lv_label_set_text(s_device, "device id: (pending)");
    lv_obj_set_style_text_color(s_device, lv_color_hex(0x97a5b3), 0);

    s_hint = lv_label_create(col);
    lv_label_set_text(s_hint, "");
    lv_obj_set_style_text_color(s_hint, lv_color_hex(0x6c7b8a), 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);

    lvgl_port_unlock();
}

/* Paint whatever the library currently reports.
 *
 * Three states worth distinguishing, because they fail for different reasons:
 * no credentials at all (provision it), associated but no relay (check the
 * host or the device key), and connected.
 *
 * Read under the state lock — the Hyperwisor task writes these fields. */
static void paint_state(void)
{
    hyperwisor_state_t *st = hyperwisor_get_state();

    hyperwisor_state_lock();
    bool ap   = st->ap_mode_active;
    bool ip   = st->ip_ready;
    bool ws   = st->ws_connected;
    hyperwisor_state_unlock();

    const char *text;
    uint32_t colour;
    const char *hint;

    if (ws) {
        text   = "CONNECTED";
        colour = 0x0f7a52;
        hint   = "This panel is a device in your Hyperwisor account.\n"
                 "Send it a command from the dashboard and watch the log.";
    } else if (ap) {
        text   = "SETUP MODE";
        colour = 0x1d5e92;
        hint   = "Join the access point this device is advertising\n"
                 "and give it your Wi-Fi credentials.";
    } else if (ip) {
        text   = "REACHING RELAY...";
        colour = 0x8f5a00;
        hint   = "On the network but not yet authenticated.\n"
                 "Register the device public key shown in the log.";
    } else {
        text   = "JOINING WI-FI...";
        colour = 0x8f5a00;
        hint   = "";
    }

    if (!lvgl_port_lock(200)) {
        ESP_LOGW(TAG, "could not take the LVGL lock; skipping this repaint");
        return;
    }
    lv_label_set_text(s_status, text);
    lv_obj_set_style_text_color(s_status, lv_color_hex(colour), 0);
    lv_label_set_text(s_hint, hint);
    lvgl_port_unlock();
}

void app_main(void)
{
    /* NVS first, before the RGB panel starts.
     *
     * The RGB DMA end-of-frame ISR reads the PSRAM bounce buffer, and a flash
     * read temporarily disables the flash/PSRAM cache. An ISR firing mid-read
     * panics with "Cache disabled but cached memory region accessed". Doing the
     * flash I/O before any periodic DMA begins avoids the race entirely. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(bsp_init());
    ESP_ERROR_CHECK(lvgl_port_init());
    build_ui();

    /* Tell the library what it may touch on this board. The library knows
     * nothing about GPIO numbers or backlights; the port is how a different
     * panel plugs in without the library changing. */
    static const hyperwisor_port_t port = {
        .board_name   = "Waveshare ESP32-S3-Touch-LCD-7",
        .manufacturer = "Waveshare",
    };
    hyperwisor_set_port(&port);

    ESP_ERROR_CHECK(hyperwisor_init());

    /* On-chip P-256 identity. The private half never leaves the device; the
     * platform only ever stores the public key, so this log line is safe to
     * copy anywhere. */
    if (hyperwisor_enable_security() == ESP_OK) {
        char pub[128];
        if (hyperwisor_get_public_key_b64(pub, sizeof(pub)) == ESP_OK) {
            ESP_LOGW(TAG, "Register this device public key with the platform:");
            ESP_LOGW(TAG, "%s", pub);
        }
    }

    char id[64] = {0};
    if (hyperwisor_get_device_id(id, sizeof(id)) == ESP_OK && id[0]) {
        if (lvgl_port_lock(200)) {
            lv_label_set_text_fmt(s_device, "device id: %s", id);
            lvgl_port_unlock();
        }
    }

    ESP_ERROR_CHECK(hyperwisor_start());
    ESP_LOGI(TAG, "panel up; device id %s", id[0] ? id : "(unset)");

    /* The library runs its own task. This one just reflects state on screen —
     * a second of latency on a status label costs nothing, and polling here is
     * simpler to read than a callback for a first example. */
    while (true) {
        paint_state();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
