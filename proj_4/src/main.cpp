#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>


// Định nghĩa chân kết nối I2S theo sơ đồ
#define I2S_WS GPIO_NUM_5  // Chân WS (LRCLK)
#define I2S_SD GPIO_NUM_7  // Chân SD (Data)
#define I2S_SCK GPIO_NUM_6 // Chân SCK (BCLK)

#define I2S_PORT I2S_NUM_0

static i2s_chan_handle_t rx_handle = NULL;

void i2s_mic_init(void) {
  // 1. Cấu hình kênh I2S ở chế độ nhận (Master RX)
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

  // 2. Cấu hình chuẩn giao tiếp I2S Standard (Phiên bản ESP-IDF 5.x / 6.x)
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000), // Tần số lấy mẫu 16kHz
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                  I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_SCK,
              .ws = I2S_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_SD,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

extern "C" void app_main(void) {
  printf("\n========================================\n");
  printf("=== TEST MICROPHONE I2S (INMP441) ===\n");
  printf("========================================\n");

  // Khởi tạo phần cứng I2S
  i2s_mic_init();
  printf("Khởi tạo I2S thành công! Hãy thử nói hoặc thổi vào mic...\n\n");

  int32_t buffer[256];
  size_t bytes_read = 0;

  while (1) {
    // Đọc dữ liệu âm thanh từ micro vào bộ đệm
    if (i2s_channel_read(rx_handle, buffer, sizeof(buffer), &bytes_read,
                         pdMS_TO_TICKS(1000)) == ESP_OK) {
      int samples_read = bytes_read / sizeof(int32_t);
      if (samples_read > 0) {
        int32_t max_val = buffer[0];
        int32_t min_val = buffer[0];

        // Tìm biên độ lớn nhất và nhỏ nhất trong khung dữ liệu
        for (int i = 1; i < samples_read; i++) {
          if (buffer[i] > max_val)
            max_val = buffer[i];
          if (buffer[i] < min_val)
            min_val = buffer[i];
        }

        int32_t amplitude = max_val - min_val;

        // In biên độ âm thanh ra màn hình
        printf("Biên độ âm thanh: %ld\n", amplitude);
      }
    } else {
      printf("[-] Lỗi: Không đọc được dữ liệu từ I2S! Kiểm tra lại chân SD, "
             "WS, SCK.\n");
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Lấy mẫu và in log mỗi 100ms
  }
}