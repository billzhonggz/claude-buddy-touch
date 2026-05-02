#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "display.h"
#include "touch.h"
#include "data.h"
#include "ble_nus.h"
#include "state_machine.h"

static const char* TAG = "buddy";
static uint32_t s_prompt_arrived_ms = 0;

static void app_task(void* arg)
{
    (void)arg;
    struct TamaState* tama = data_state();
    enum PersonaState base_state = P_SLEEP;
    enum PersonaState active_state = P_SLEEP;

    data_set_demo(true);
    ESP_LOGI(TAG, "Demo mode enabled. Touch the screen to interact.");

    static char s_last_prompt_id[40] = "";

    while (1) {
        data_poll(tama);
        base_state = state_machine_derive(tama);
        active_state = state_machine_update(base_state);

        if (tama->promptId[0] && strcmp(tama->promptId, s_last_prompt_id) != 0) {
            memcpy(s_last_prompt_id, tama->promptId, sizeof(s_last_prompt_id) - 1);
            s_last_prompt_id[sizeof(s_last_prompt_id) - 1] = '\0';
            s_prompt_arrived_ms = (uint32_t)(esp_timer_get_time() / 1000);
        }
        if (!tama->promptId[0]) {
            s_last_prompt_id[0] = '\0';
        }

        static bool was_connected = true;
        if (tama->connected != was_connected) {
            was_connected = tama->connected;
            if (!tama->connected) {
                state_machine_trigger_oneshot(P_DIZZY, 2000);
            }
        }

        touch_event_data_t touch = touch_process();
        if (touch.event == TOUCH_EVENT_TAP) {
            ESP_LOGI(TAG, "Tap at (%u, %u)", touch.x, touch.y);
            data_advance_demo();
        } else if (touch.event == TOUCH_EVENT_LONG_PRESS) {
            ESP_LOGI(TAG, "Long press at (%u, %u)", touch.x, touch.y);
            data_set_demo(!data_demo());
        } else if (touch.event == TOUCH_EVENT_SWIPE_LEFT) {
            enum DisplayMode m = display_get_mode();
            m = (m + 1) % DISPLAY_MODE_COUNT;
            display_set_mode(m);
        } else if (touch.event == TOUCH_EVENT_SWIPE_RIGHT) {
            enum DisplayMode m = display_get_mode();
            m = (m == 0) ? (DISPLAY_MODE_COUNT - 1) : (m - 1);
            display_set_mode(m);
        }

        static uint32_t last_status_tx = 0;
        if (tama->connected && (tama->lastUpdated - last_status_tx >= 30000 || last_status_tx == 0)) {
            last_status_tx = tama->lastUpdated;
            char status[256];
            snprintf(status, sizeof(status),
                     "{\"cmd\":\"status\",\"connected\":true,"
                     "\"sessions\":{\"total\":%u,\"running\":%u,\"waiting\":%u},"
                     "\"tokens_today\":%lu}\n",
                     tama->sessionsTotal, tama->sessionsRunning, tama->sessionsWaiting,
                     (unsigned long)tama->tokensToday);
            ble_nus_send((const uint8_t*)status, strlen(status));
        }

        display_lock();
        display_update(tama, active_state);
        if (tama->promptId[0] && display_get_mode() == DISPLAY_MODE_BUDDY) {
            display_show_permission(true, tama->promptTool, tama->promptHint);
        } else {
            display_show_permission(false, NULL, NULL);
        }
        display_unlock();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void on_approve(void)
{
    struct TamaState* s = data_state();
    if (s->promptId[0]) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"approved\"}\n",
                 s->promptId);
        ble_nus_send((const uint8_t*)resp, strlen(resp));
        state_machine_record_approval();
        uint32_t took = now_ms - s_prompt_arrived_ms;
        if (took < 5000) {
            state_machine_trigger_oneshot(P_HEART, 2000);
        }
        s->promptId[0] = '\0';
    }
}

static void on_deny(void)
{
    struct TamaState* s = data_state();
    if (s->promptId[0]) {
        char resp[256];
        snprintf(resp, sizeof(resp),
                 "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"denied\"}\n",
                 s->promptId);
        ble_nus_send((const uint8_t*)resp, strlen(resp));
        s->promptId[0] = '\0';
    }
}

static void on_ble_connected(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "BLE connected");
        char msg[64];
        snprintf(msg, sizeof(msg),
                 "{\"cmd\":\"status\",\"connected\":true}\n");
        ble_nus_send((const uint8_t*)msg, strlen(msg));
    } else {
        ESP_LOGI(TAG, "BLE disconnected");
    }
}

static void on_ble_rx(const uint8_t* data, uint16_t len)
{
    data_feed(data, len);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Claude Buddy Touch starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    display_init();
    touch_init();
    display_set_approve_cb(on_approve);
    display_set_deny_cb(on_deny);

    ble_nus_set_connection_cb(on_ble_connected);
    ble_nus_set_rx_cb(on_ble_rx);

    ret = ble_nus_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE init failed (C6 may not be connected) — running in demo mode");
    }

    xTaskCreate(app_task, "app", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready. Demo mode active.");
}
