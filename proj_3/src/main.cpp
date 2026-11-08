#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

// Bạn có thể chọn GPIO 4 & 5 (2 chân này nằm sát nhau trên ESP32-S3, rất dễ cắm)
// Hoặc giữ nguyên GPIO 9 & 8 (nhớ kiểm tra đúng vị trí in chữ trên board)
#define I2C_MASTER_SDA_IO GPIO_NUM_4 // Gợi ý: GPIO 4 (hoặc đổi lại GPIO_NUM_9)
#define I2C_MASTER_SCL_IO GPIO_NUM_5 // Gợi ý: GPIO 5 (hoặc đổi lại GPIO_NUM_8)
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_NUM I2C_NUM_0

// Kiểm tra mức điện áp tĩnh của chân SDA và SCL trước khi bật I2C
void diagnose_i2c_bus_lines(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << I2C_MASTER_SDA_IO) | (1ULL << I2C_MASTER_SCL_IO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  vTaskDelay(pdMS_TO_TICKS(10));

  int sda_level = gpio_get_level(I2C_MASTER_SDA_IO);
  int scl_level = gpio_get_level(I2C_MASTER_SCL_IO);

  printf("\n=== CHẨN ĐOÁN PHẦN CỨNG I2C BUS ===\n");
  printf("  [*] Chân SDA (GPIO %d) mức logic: %s\n", I2C_MASTER_SDA_IO,
         sda_level ? "HIGH (Tốt)" : "LOW (CẢNH BÁO: Bị kẹt mass/chập GND!)");
  printf("  [*] Chân SCL (GPIO %d) mức logic: %s\n", I2C_MASTER_SCL_IO,
         scl_level ? "HIGH (Tốt)" : "LOW (CẢNH BÁO: Bị kẹt mass/chập GND!)");

  if (!sda_level || !scl_level) {
    printf("  [!] Đường truyền I2C đang bị kẹt ở mức 0V! Kiểm tra dây hoặc chân cắm.\n");
  } else {
    printf("  [+] Đường truyền I2C ở mức 3.3V (Bình thường).\n");
  }
  printf("=====================================\n\n");
}

void i2c_master_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master =
          {
              .clk_speed = I2C_MASTER_FREQ_HZ,
          },
      .clk_flags = 0,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));
  printf("[INFO] Đã khởi tạo I2C Master trên SDA: GPIO %d, SCL: GPIO %d\n",
         I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
}

// Đọc Model ID của chip VL6180X (thanh ghi 0x0000, mặc định trả về 0xB4)
bool tof_check_model_id(uint8_t addr) {
  uint16_t reg = 0x0000;
  uint8_t val = 0;

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, (reg >> 8) & 0xFF, true);
  i2c_master_write_byte(cmd, reg & 0xFF, true);

  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
  i2c_master_read_byte(cmd, &val, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd);

  esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret == ESP_OK) {
    printf("      --> Đọc thành công Model ID: 0x%02X (Chuẩn VL6180X là 0xB4)\n", val);
    return true;
  }
  return false;
}

void scan_and_report_i2c(void) {
  printf("--- Đang quét bus I2C (0x01 -> 0x7E) ---\n");
  int found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
      printf("[+] PHÁT HIỆN THIẾT BỊ: 0x%02X\n", addr);
      found++;
      tof_check_model_id(addr);
    }
  }

  if (found == 0) {
    printf("[-] Chưa thấy thiết bị nào phản hồi.\n");
    printf("    -> Hãy đảm bảo chân SHUT đã cắm lên 3V3!\n");
    printf("    -> Thử đảo 2 dây SDA và SCL.\n");
  } else {
    printf("[+] Quét xong: Tìm thấy %d thiết bị trên bus.\n", found);
  }
  printf("----------------------------------------\n\n");
}

extern "C" void app_main(void) {
  // Bước 1: Chẩn đoán xem dây có bị chập 0V không
  diagnose_i2c_bus_lines();

  // Bước 2: Bật I2C Master
  i2c_master_init();

  // Bước 3: Vòng lặp quét liên tục mỗi 2.5 giây
  while (1) {
    scan_and_report_i2c();
    vTaskDelay(pdMS_TO_TICKS(2500));
  }
}
