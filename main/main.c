#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "display.h"
#include "ble_nus.h"

static const char* TAG = "buddy";

static void on_ble_connected(bool connected)
{
    display_lock();

    lv_obj_t* scr = lv_scr_act();
    lv_obj_t* label = lv_obj_get_child(scr, 0);
    if (label && lv_obj_check_type(label, &lv_label_class)) {
        if (connected) {
            lv_label_set_text(label, "Claude Buddy Touch\n\nConnected");
        } else {
            lv_label_set_text(label, "Claude Buddy Touch\n\nConnecting...");
        }
    }

    display_unlock();
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
    display_show_hello();

    ble_nus_set_connection_cb(on_ble_connected);

    ret = ble_nus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed (C6 may not be connected)");
    }

    ESP_LOGI(TAG, "Ready.");
}
