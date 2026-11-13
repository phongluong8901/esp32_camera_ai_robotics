#include "emoji_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lcd_emoji.h"
#include "web_server.h"


extern "C" void app_main(void) {
  // 1. Tạo Queue truyền dữ liệu giữa Web Task và LCD Task
  QueueHandle_t emoji_queue = xQueueCreate(5, sizeof(emoji_mode_t));

  // 2. Khởi tạo phần cứng LCD ST7789
  init_lcd_hardware();

  // 3. Khởi chạy Task 1: Render Emoji trên Core 1
  xTaskCreatePinnedToCore(lcd_emoji_task, "lcd_emoji_task", 4096,
                          (void *)emoji_queue, // Truyền Queue vào task
                          5, NULL, 1);

  // 4. Khởi chạy Task 2: Web Server trên Core 0
  xTaskCreatePinnedToCore(web_server_task, "web_server_task", 6144,
                          (void *)emoji_queue, // Truyền Queue vào task
                          4, NULL, 0);
}