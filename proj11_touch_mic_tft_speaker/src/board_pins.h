#pragma once
#include "driver/gpio.h"

// 1. Màn hình TFT ST7789 240x240
#define LCD_HOST SPI2_HOST
#define PIN_NUM_SCL GPIO_NUM_12
#define PIN_NUM_SDA GPIO_NUM_11
#define PIN_NUM_RES GPIO_NUM_14
#define PIN_NUM_DC GPIO_NUM_13
#define PIN_NUM_CS GPIO_NUM_10
#define PIN_NUM_BLK GPIO_NUM_9

// Màn hình của bạn có chân CS (như trong ảnh 4), nếu chạy Mode 0 thì đổi
// ACTIVE_SPI_MODE = 0, ACTIVE_CS_PIN = PIN_NUM_CS
#define ACTIVE_SPI_MODE 3
#define ACTIVE_CS_PIN -1

// 2. Cảm biến chạm TTP223 (Màu đỏ)
#define PIN_TOUCH_SIG GPIO_NUM_4

// 3. Mạch loa PAM8403 (Chân LIN hoặc RIN nối vào GPIO 5)
#define PIN_SPEAKER_PWM GPIO_NUM_5

// 4. Micro I2S INMP441 (Chân L/R và GND nối xuống GND chung)
#define PIN_MIC_SCK GPIO_NUM_16
#define PIN_MIC_WS GPIO_NUM_17
#define PIN_MIC_SD GPIO_NUM_18