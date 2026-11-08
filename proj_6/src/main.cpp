#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tb6612.h"

static const char *TAG = "MAIN_APP";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Khoi dong he thong dieu khien Robot TB6612FNG...");

    // Su dung cau hinh mac dinh cho ESP32-S3:
    // Motor A: PWMA = GPIO 1, AIN1 = GPIO 2, AIN2 = GPIO 3
    // Motor B: PWMB = GPIO 5, BIN1 = GPIO 6, BIN2 = GPIO 7
    // Standby: STBY = GPIO 4
    tb6612_config_t config = TB6612_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(tb6612_init(&config));

    ESP_LOGI(TAG, "San sang hoat dong!");

    while (1) {
        // 1. Tang toc tu tu di thang len 70% (tranh sut ap nguon)
        ESP_LOGI(TAG, ">> Ramp-up tien thang 70%%...");
        tb6612_ramp_drive(70, 70, 15);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // 2. Phanh gap
        ESP_LOGI(TAG, ">> Phanh khan cap (Brake)!");
        tb6612_brake_all();
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 3. Re trai vi sai (quay tai cho)
        ESP_LOGI(TAG, ">> Re trai vi sai (Spin Left)...");
        tb6612_robot_drive(0, -60); // speed = 0, turn = -60
        vTaskDelay(pdMS_TO_TICKS(1500));

        // 4. Re phai vi sai (quay tai cho)
        ESP_LOGI(TAG, ">> Re phai vi sai (Spin Right)...");
        tb6612_robot_drive(0, 60);  // speed = 0, turn = 60
        vTaskDelay(pdMS_TO_TICKS(1500));

        // 5. Lui xe tu tu len -60%
        ESP_LOGI(TAG, ">> Ramp-up lui xe -60%%...");
        tb6612_ramp_drive(-60, -60, 15);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // 6. Dung tha troi (Coast Stop) va dua module ve che do Standby ngu tiet kiem pin
        ESP_LOGI(TAG, ">> Dung tha troi & dua vao Standby (Sleep 2 giay)...");
        tb6612_stop_all();
        tb6612_enable(false); // STBY = LOW -> Tiet kiem dien
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Bat lai module truoc chu ky tiep theo
        tb6612_enable(true);
    }
}
