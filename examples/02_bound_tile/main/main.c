/*
 * 02_bound_tile — a tile bound to a command, in both directions.
 *
 * This is the example to copy. Everything a connected panel does is one of
 * these two things:
 *
 *   INBOUND   the dashboard (or another device) sends this panel a command,
 *             and the panel acts on it and shows the result.
 *
 *   OUTBOUND  someone presses a tile, and the panel sends a command to another
 *             device over the HSC.
 *
 * Both use the same envelope, which is the whole point:
 *
 *   { "targetId": "<device uuid>",
 *     "payload": { "commands": [ { "command": "relay_control",
 *                                  "actions": [ { "action": "update",
 *                                                 "params": { "relay": "1",
 *                                                             "state": "true" } } ] } ] } }
 *
 * `hyperwisor_emit()` builds that for you; `hyperwisor_cmd_register()` receives
 * it. There is no other protocol to learn.
 *
 * TO RUN THIS: set REMOTE_TARGET below to a device id your account can reach.
 * The panel's own id is printed to the log at boot. Pointing it at itself is a
 * perfectly good first test — the outbound tile then drives the inbound one.
 */

#include "bsp.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "hyperwisor.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bound_tile";

/* The device this panel sends to. Put your own device id here.
 * Leave it empty and the outbound tile targets this panel itself. */
#define REMOTE_TARGET ""

#define RELAY_CHANNEL "1"

static lv_obj_t *s_in_tile;     /* lit by an inbound command  */
static lv_obj_t *s_in_label;
static lv_obj_t *s_out_label;
static bool      s_relay_on;    /* what we were last told     */
static bool      s_out_on;      /* what we last asked for     */

static void paint_inbound(void)
{
    if (!lvgl_port_lock(200)) return;
    lv_obj_set_style_bg_color(s_in_tile,
        lv_color_hex(s_relay_on ? 0x0f7a52 : 0x24303e), 0);
    lv_label_set_text(s_in_label, s_relay_on ? "RELAY 1\nON" : "RELAY 1\nOFF");
    lvgl_port_unlock();
}

/* ---------------------------------------------------------------- INBOUND */

/* Called when anything sends this panel a `relay_control` command.
 *
 * `payload` is the whole envelope, so the helpers do the digging: find the
 * command, find the action inside it, then read the params. Doing this by hand
 * with cJSON_GetObjectItem chains is where most of the bugs live.
 *
 * This runs on the Hyperwisor task. Touch hardware here if you like, but take
 * the LVGL lock before touching the screen — which paint_inbound() does. */
static void on_relay_control(const char *from, cJSON *payload)
{
    cJSON *params = hyperwisor_cmd_find_params(payload, "relay_control", "update");
    if (!params) {
        ESP_LOGW(TAG, "relay_control from %s had no update action", from);
        return;
    }

    cJSON *relay = cJSON_GetObjectItem(params, "relay");
    cJSON *state = cJSON_GetObjectItem(params, "state");
    if (!cJSON_IsString(relay) || !cJSON_IsString(state)) {
        ESP_LOGW(TAG, "relay_control from %s had unexpected params", from);
        return;
    }

    /* The dashboard sends these as strings, not booleans. Worth knowing before
     * you write cJSON_IsBool() and wonder why nothing happens. */
    if (strcmp(relay->valuestring, RELAY_CHANNEL) != 0) return;

    s_relay_on = (strcmp(state->valuestring, "true") == 0);
    ESP_LOGI(TAG, "relay %s -> %s (from %s)",
             relay->valuestring, state->valuestring, from);

    /* Drive your hardware here: a GPIO, a modbus coil, a contactor. */

    paint_inbound();
}

/* --------------------------------------------------------------- OUTBOUND */

static void on_tile_pressed(lv_event_t *e)
{
    (void)e;
    s_out_on = !s_out_on;

    char target[64] = {0};
    if (REMOTE_TARGET[0]) {
        snprintf(target, sizeof(target), "%s", REMOTE_TARGET);
    } else {
        /* No remote configured — talk to ourselves, which still proves the
         * whole round trip: out through the HSC, back in as a command. */
        hyperwisor_get_device_id(target, sizeof(target));
    }

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "relay", RELAY_CHANNEL);
    cJSON_AddStringToObject(params, "state", s_out_on ? "true" : "false");

    /* Takes ownership of params — do not free it yourself. */
    esp_err_t err = hyperwisor_emit(target, "relay_control", "update", params);

    ESP_LOGI(TAG, "sent relay_control state=%s to %s (%s)",
             s_out_on ? "true" : "false", target, esp_err_to_name(err));

    lv_label_set_text_fmt(s_out_label, "SEND\n%s", s_out_on ? "ON" : "OFF");
}

/* -------------------------------------------------------------------- UI */

static lv_obj_t *make_tile(lv_obj_t *parent, const char *text, lv_obj_t **label_out)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 240, 180);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x24303e), 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x2b3846), 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *l = lv_label_create(tile);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xe1e7ee), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    if (label_out) *label_out = l;
    return tile;
}

static void build_ui(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x15212d), 0);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 30, 0);

    /* Left: lit by whatever the dashboard sends us. Not pressable — its state
     * belongs to the sender, and a tile you can both press and be told about
     * is a tile that lies when the two disagree. */
    s_in_tile = make_tile(row, "RELAY 1\nOFF", &s_in_label);
    lv_obj_clear_flag(s_in_tile, LV_OBJ_FLAG_CLICKABLE);

    /* Right: sends. */
    lv_obj_t *out = make_tile(row, "SEND\nOFF", &s_out_label);
    lv_obj_add_event_cb(out, on_tile_pressed, LV_EVENT_CLICKED, NULL);

    lvgl_port_unlock();
}

void app_main(void)
{
    /* NVS before the RGB panel — see 01_hello_panel for why. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* bsp_init() brings up the LVGL port too — do not call lvgl_port_init(). */
    ESP_ERROR_CHECK(bsp_init());
    build_ui();

    static const hyperwisor_port_t port = {
        .board_name   = "Waveshare ESP32-S3-Touch-LCD-7",
        .manufacturer = "Waveshare",
    };
    hyperwisor_set_port(&port);

    ESP_ERROR_CHECK(hyperwisor_init());

    /* Register BEFORE start(), so a command arriving on the first connection
     * is not answered with "Unknown command". */
    ESP_ERROR_CHECK(hyperwisor_cmd_register("relay_control", on_relay_control));

    if (hyperwisor_enable_security() == ESP_OK) {
        char pub[128];
        if (hyperwisor_get_public_key_b64(pub, sizeof(pub)) == ESP_OK) {
            ESP_LOGW(TAG, "Register this device public key with the platform:");
            ESP_LOGW(TAG, "%s", pub);
        }
    }

    char id[64] = {0};
    hyperwisor_get_device_id(id, sizeof(id));
    ESP_LOGI(TAG, "this panel is device %s", id[0] ? id : "(unset)");
    ESP_LOGI(TAG, "outbound target: %s", REMOTE_TARGET[0] ? REMOTE_TARGET : "(itself)");

    ESP_ERROR_CHECK(hyperwisor_start());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
