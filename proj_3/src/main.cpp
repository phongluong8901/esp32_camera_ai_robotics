#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>


#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO GPIO_NUM_9 // Chân SDA đang cắm
#define I2C_MASTER_SCL_IO GPIO_NUM_8 // Chân SCL đang cắm
#define I2C_MASTER_FREQ_HZ 100000

void i2c_master_init(void) {
  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = I2C_MASTER_SDA_IO;
  conf.scl_io_num = I2C_MASTER_SCL_IO;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

  i2c_param_config(I2C_MASTER_NUM, &conf);
  i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

extern "C" void app_main(void) {
  i2c_master_init();

  printf("\n========================================\n");
  printf("=== ĐANG QUÉT CÁC THIẾT BỊ I2C TRÊN BUS ===\n");
  printf("========================================\n");

  while (1) {
    int devices_found = 0;

    for (uint8_t i = 1; i < 127; i++) {
      i2c_cmd_handle_t cmd = i2c_cmd_link_create();
      i2c_master_start(cmd);
      i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, true);
      i2c_master_stop(cmd);

      esp_err_t res =
          i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
      i2c_cmd_link_delete(cmd);

      if (res == ESP_OK) {
        printf("[+] Tìm thấy thiết bị I2C tại địa chỉ: 0x%02X\n", i);
        devices_found++;
      }
    }

    if (devices_found == 0) {
      printf("[-] Không tìm thấy thiết bị nào! (Kiểm tra lại nguồn, SDA, SCL, "
             "mối hàn)\n");
    } else {
      printf("--- Quét xong! Tổng số thiết bị: %d ---\n", devices_found);
    }

    printf("----------------------------------------\n");
    vTaskDelay(pdMS_TO_TICKS(2000)); // Quét lại sau mỗi 2 giây
  }
}