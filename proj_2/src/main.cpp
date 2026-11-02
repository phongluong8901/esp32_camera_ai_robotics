#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

#define TOUCH_PIN GPIO_NUM_4

extern "C" void app_main(void) {
  // 1. Cấu hình chân GPIO làm đầu vào, tắt hoàn toàn trở kéo
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << TOUCH_PIN);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

  gpio_config(&io_conf);

  printf("\n========================================\n");
  printf("=== DEBUG LOG CHI TIẾT MODULE TOUCH ===\n");
  printf("========================================\n");

  while (1) {
    // Đọc giá trị mức logic thực tế tại chân GPIO 4 (trả về 0 hoặc 1)
    int raw_value = gpio_get_level(TOUCH_PIN);

    // In ra log giá trị thô liên tục để quan sát
    printf("Trạng thái thô (GPIO 4) = %d\n", raw_value);

    vTaskDelay(pdMS_TO_TICKS(200)); // Đợi 200ms đọc một lần cho dễ nhìn
  }
}