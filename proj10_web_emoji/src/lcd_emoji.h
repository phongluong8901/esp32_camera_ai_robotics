#pragma once
#include "emoji_types.h"

// Khởi tạo SPI và màn hình ST7789
void init_lcd_hardware(void);

// Task hiển thị giao diện Emoji (nhận QueueHandle_t qua pvParameters)
void lcd_emoji_task(void *pvParameters);