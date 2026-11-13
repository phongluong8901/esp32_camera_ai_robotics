#include "emoji_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd_emoji.h"
#include "mic_touch.h"
#include "sound_fx.h"
#include "web_server.h"


extern "C" void app_main(void) {
  // 1. Tạo Queue truyền lệnh đổi biểu cảm
  QueueHandle_t emoji_queue = xQueueCreate(10, sizeof(emoji_mode_t));

  // 2. Khởi tạo Loa PWM & Màn hình ST7789
  init_speaker_pwm();
  init_lcd_hardware();
  play_touch_beep(); // Kêu Beep báo hiệu khởi động xong

  // 3. Task 1: Render màn hình LCD (Core 1)
  xTaskCreatePinnedToCore(lcd_emoji_task, "lcd_emoji_task", 4096,
                          (void *)emoji_queue, 5, NULL, 1);

  // 4. Task 2: Đọc I2S Mic INMP441 & Touch TTP223 (Core 1)
  xTaskCreatePinnedToCore(mic_touch_task, "mic_touch_task", 4096,
                          (void *)emoji_queue, 6, NULL, 1);

  // 5. Task 3: Wi-Fi AP & Web Server (Core 0)
  xTaskCreatePinnedToCore(web_server_task, "web_server_task", 6144,
                          (void *)emoji_queue, 4, NULL, 0);
}