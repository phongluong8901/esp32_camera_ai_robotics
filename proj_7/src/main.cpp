#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "AUDIO_TEST";

// Định nghĩa chân phát tín hiệu âm thanh sang chân LIN/RIN của PAM8403
#define AUDIO_OUT_GPIO GPIO_NUM_17

// Cấu hình LEDC PWM cho âm thanh
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT // Độ phân giải 10-bit (0 - 1023)
#define LEDC_DUTY_50_PERCENT (512)      // 50% của 1024 để tạo sóng vuông chuẩn

void audio_pwm_init(void) {
  ESP_LOGI(TAG, "Đang khởi tạo LEDC PWM trên GPIO %d...", AUDIO_OUT_GPIO);

  // 1. Cấu hình Timer PWM
  ledc_timer_config_t ledc_timer = {};
  ledc_timer.speed_mode = LEDC_MODE;
  ledc_timer.duty_resolution = LEDC_DUTY_RES;
  ledc_timer.timer_num = LEDC_TIMER;
  ledc_timer.freq_hz = 1000; // Tần số khởi tạo 1000Hz
  ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  esp_err_t ret = ledc_timer_config(&ledc_timer);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi ledc_timer_config: %s", esp_err_to_name(ret));
    return;
  }

  // 2. Cấu hình Channel PWM gắn vào GPIO 17
  ledc_channel_config_t ledc_channel = {};
  ledc_channel.gpio_num = AUDIO_OUT_GPIO;
  ledc_channel.speed_mode = LEDC_MODE;
  ledc_channel.channel = LEDC_CHANNEL;
  ledc_channel.timer_sel = LEDC_TIMER;
  ledc_channel.duty = 0; // Ban đầu tắt tiếng (duty = 0)
  ledc_channel.hpoint = 0;
  ret = ledc_channel_config(&ledc_channel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Lỗi ledc_channel_config: %s", esp_err_to_name(ret));
    return;
  }

  ESP_LOGI(TAG, "Khởi tạo PWM Audio Output trên GPIO %d thành công!", AUDIO_OUT_GPIO);
}

// Hàm phát âm thanh theo tần số (Hz) và thời lượng (ms)
void play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  if (freq_hz > 0) {
    ESP_LOGI(TAG, ">> Đang phát: %lu Hz trong %lu ms", freq_hz, duration_ms);
    esp_err_t err = ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Lỗi set_freq (%lu Hz): %s", freq_hz, esp_err_to_name(err));
    }
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_50_PERCENT);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    // Tắt âm thanh sau khi hết thời lượng nốt
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(50)); // Nghỉ ngắn giữa các nốt
  } else {
    // Im lặng
    ESP_LOGI(TAG, ">> Tạm dừng: %lu ms", duration_ms);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
  }
}

extern "C" void app_main(void) {
  // Chờ 2 giây để Serial Monitor kịp kết nối
  vTaskDelay(pdMS_TO_TICKS(2000));
  ESP_LOGI(TAG, "=== BẮT ĐẦU CHƯƠNG TRÌNH TEST LOA GPIO 17 ===");

  audio_pwm_init();

  while (1) {
    // Phát chuỗi âm bíp với các tần số rõ ràng
    play_tone(1000, 300); // 1000Hz (300ms)
    play_tone(1500, 300); // 1500Hz (300ms)
    play_tone(2000, 400); // 2000Hz (400ms)

    play_tone(0, 500);    // Nghỉ 500ms

    play_tone(2000, 300);
    play_tone(1500, 300);
    play_tone(1000, 400);

    // Nghỉ 2 giây trước khi lặp lại
    play_tone(0, 2000);
  }
}
