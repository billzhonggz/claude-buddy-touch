#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "display.h"
#include "touch.h"
#include "data.h"
#include "ble_nus.h"

static const char* TAG = "buddy";

enum PersonaState derive_state(const struct TamaState* s)
{
    if (!s->connected)             return P_IDLE;
    if (s->sessionsWaiting > 0)    return P_ATTENTION;
    if (s->recentlyCompleted)      return P_CELEBRATE;
    if (s->sessionsRunning >= 3)   return P_BUSY;
    return P_IDLE;
}

static void app_task(void* arg)
{
    (void)arg;
    struct TamaState* tama = data_state();
    enum PersonaState base_state = P_SLEEP;
    enum PersonaState active_state = P_SLEEP;

    data_set_demo(true);
    ESP_LOGI(TAG, "Demo mode enabled. Touch the screen to interact.");

    while (1) {
        data_poll(tama);
        base_state = derive_state(tama);
        active_state = base_state;

        touch_event_data_t touch = touch_process();
        if (touch.event == TOUCH_EVENT_TAP) {
            ESP_LOGI(TAG, "Tap at (%u, %u)", touch.x, touch.y);
            data_advance_demo();
        } else if (touch.event == TOUCH_EVENT_LONG_PRESS) {
            ESP_LOGI(TAG, "Long press at (%u, %u)", touch.x, touch.y);
            data_set_demo(!data_demo());
        }

        display_lock();
        display_update(tama, active_state);
        display_unlock();

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void on_ble_connected(bool connected)
{
    if (connected) {
        ESP_LOGI(TAG, "BLE connected");
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

    ble_nus_set_connection_cb(on_ble_connected);
    ble_nus_set_rx_cb(on_ble_rx);

    ret = ble_nus_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE init failed (C6 may not be connected) — running in demo mode");
    }

    xTaskCreate(app_task, "app", 8192, NULL, 5, NULL);

    ESP_LOGI(TAG, "Ready. Demo mode active.");
}
