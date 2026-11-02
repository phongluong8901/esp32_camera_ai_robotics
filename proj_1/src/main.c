#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>


void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(2000));

  printf("\n\n========================================\n");
  printf("=== KIỂM TRA PHẦN CỨNG ESP32-S3 N16R8 ===\n");
  printf("========================================\n");

  // 1. Kiểm tra thông tin chip
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  printf("Dòng chip: ESP32-S3 (Số nhân: %d)\n", chip_info.cores);

  // 2. Kiểm tra dung lượng Flash thực tế
  uint32_t flash_size = 0;
  if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
    printf("Dung lượng Flash thực tế: %lu MB (%lu bytes)\n",
           (unsigned long)(flash_size / (1024 * 1024)),
           (unsigned long)flash_size);
  }

  // 3. Kiểm tra tổng dung lượng PSRAM qua Heap Caps
  size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (psram_total > 0) {
    printf("PSRAM: ĐÃ BẬT THÀNH CÔNG! Tổng dung lượng: %lu MB (%lu bytes)\n",
           (unsigned long)(psram_total / (1024 * 1024)),
           (unsigned long)psram_total);
  } else {
    printf("[!] PSRAM chưa được kích hoạt trong sdkconfig.defaults!\n");
  }
  printf("========================================\n\n");

  int count = 0;
  while (1) {
    printf("Hệ thống chạy ổn định... [%d s]\n", count++);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}