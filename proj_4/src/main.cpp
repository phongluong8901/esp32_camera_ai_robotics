#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

#define I2S_WS GPIO_NUM_5
#define I2S_SD GPIO_NUM_7
#define I2S_SCK GPIO_NUM_6

#define I2S_PORT I2S_NUM_0

static i2s_chan_handle_t rx_handle = NULL;
int32_t noise_floor = 0; // Biến lưu mức nhiễu nền tự động

void i2s_mic_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      // === ĐÃ SỬA: dùng PHILIPS thay vì MSB (quan trọng với INMP441) ===
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                                      I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = I2S_SCK,
              .ws = I2S_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = I2S_SD,
              .invert_flags = {false, false, false},
          },
  };

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

extern "C" void app_main(void) {
  printf("\n==================================================\n");
  printf("=== TỰ ĐỘNG HIỆU CHUẨN NHIỄU NỀN (CALIBRATION) ===\n");
  printf("==================================================\n");

  i2s_mic_init();

  printf("Đang đo nhiễu nền môi trường, xin vui lòng giữ im lặng trong 1 "
         "giây...\n");

  int32_t buffer[256];
  size_t bytes_read = 0;
  int64_t total_noise = 0;
  int calibration_samples = 20;

  // Tự động đo 20 mẫu đầu tiên lúc khởi động để tính mức nhiễu nền thực tế
  for (int c = 0; c < calibration_samples; c++) {
    if (i2s_channel_read(rx_handle, buffer, sizeof(buffer), &bytes_read,
                         pdMS_TO_TICKS(100)) == ESP_OK) {
      int samples_read = bytes_read / sizeof(int32_t);
      if (samples_read > 0) {
        int32_t max_val = -2147483647;
        int32_t min_val = 2147483647;
        for (int i = 0; i < samples_read; i++) {
          int32_t sample = buffer[i] >> 8; // 24-bit data nằm ở 24 bit cao
          if (sample > max_val)
            max_val = sample;
          if (sample < min_val)
            min_val = sample;
        }
        total_noise += (max_val - min_val);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  // Lấy trung bình cộng nhiễu nền và nhân lên 3 lần làm ngưỡng kích hoạt an
  // toàn
  noise_floor = (total_noise / calibration_samples) * 3;
  if (noise_floor < 50000)
    noise_floor = 50000; // Ngưỡng tối thiểu hợp lý hơn (tránh quá nhạy)

  printf("-> Đã hiệu chuẩn xong! Mức ngưỡng kích hoạt hiện tại là: %ld\n",
         (long)noise_floor);
  printf("Bây giờ bạn có thể nói chuyện...\n\n");

  while (1) {
    esp_err_t ret = i2s_channel_read(rx_handle, buffer, sizeof(buffer),
                                     &bytes_read, pdMS_TO_TICKS(500));

    if (ret == ESP_OK) {
      int samples_read = bytes_read / sizeof(int32_t);
      if (samples_read > 0) {
        int32_t max_val = -2147483647;
        int32_t min_val = 2147483647;

        for (int i = 0; i < samples_read; i++) {
          int32_t sample = buffer[i] >> 8;
          if (sample > max_val)
            max_val = sample;
          if (sample < min_val)
            min_val = sample;
        }

        int32_t amplitude = max_val - min_val;

        // So sánh trực tiếp với ngưỡng nhiễu nền đã tự động đo
        if (amplitude < noise_floor) {
          printf("[YÊN TĨNH] Biên độ: %-8ld (Nền: %ld)\r", (long)amplitude,
                 (long)noise_floor);
        } else {
          int bar_len = (amplitude - noise_floor) / (noise_floor / 2 + 1);
          if (bar_len > 40)
            bar_len = 40;

          char bar[41];
          for (int i = 0; i < 40; i++) {
            bar[i] = (i < bar_len) ? '|' : ' ';
          }
          bar[40] = '\0';

          printf("\n>>> BẮT ĐƯỢC GIỌNG NÓI! Biên độ: %-8ld [%s]\n",
                 (long)amplitude, bar);
        }
      }
    } else {
      printf("[-] LỖI TIMEOUT I2S!\n");
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}