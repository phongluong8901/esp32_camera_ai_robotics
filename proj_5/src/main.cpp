#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>


// Định nghĩa các chân GPIO kết nối màn hình TFT
#define PIN_NUM_MOSI 11 // SDA của màn hình
#define PIN_NUM_CLK 12  // SCL của màn hình
#define PIN_NUM_CS 10   // CS của màn hình
#define PIN_NUM_DC 13   // DC của màn hình
#define PIN_NUM_RST 14  // RES của màn hình

#define LCD_WIDTH 132
#define LCD_HEIGHT 162

static spi_device_handle_t spi;

// Gửi lệnh điều khiển xuống màn hình
void lcd_send_cmd(const uint8_t cmd) {
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  t.tx_buffer = &cmd;
  t.user = (void *)0; // DC = 0
  gpio_set_level((gpio_num_t)PIN_NUM_DC, 0);
  spi_device_transmit(spi, &t);
}

// Gửi dữ liệu màu sắc xuống màn hình
void lcd_send_data(const uint8_t *data, int len) {
  if (len == 0)
    return;
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = len * 8;
  t.tx_buffer = data;
  t.user = (void *)1; // DC = 1
  gpio_set_level((gpio_num_t)PIN_NUM_DC, 1);
  spi_device_transmit(spi, &t);
}

// Khởi tạo Bus SPI
void lcd_spi_init(void) {
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << PIN_NUM_DC) | (1ULL << PIN_NUM_RST);
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io_conf);

  spi_bus_config_t buscfg;
  memset(&buscfg, 0, sizeof(buscfg));
  buscfg.mosi_io_num = PIN_NUM_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.sclk_io_num = PIN_NUM_CLK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2 + 10;

  spi_device_interface_config_t devcfg;
  memset(&devcfg, 0, sizeof(devcfg));
  devcfg.clock_speed_hz = 20 * 1000 * 1000; // 20MHz
  devcfg.mode = 0;
  devcfg.spics_io_num = PIN_NUM_CS;
  devcfg.queue_size = 7;

  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));
}

// Khởi tạo chuỗi lệnh ST7789
void lcd_init_sequence(void) {
  gpio_set_level((gpio_num_t)PIN_NUM_RST, 0);
  vTaskDelay(pdMS_TO_TICKS(100));
  gpio_set_level((gpio_num_t)PIN_NUM_RST, 1);
  vTaskDelay(pdMS_TO_TICKS(100));

  lcd_send_cmd(0x01); // Software Reset
  vTaskDelay(pdMS_TO_TICKS(150));

  lcd_send_cmd(0x11); // Sleep Out
  vTaskDelay(pdMS_TO_TICKS(150));

  lcd_send_cmd(0x3A); // Pixel Format Set (RGB565)
  uint8_t color_mode = 0x55;
  lcd_send_data(&color_mode, 1);

  lcd_send_cmd(0x21); // Display Inversion On
  lcd_send_cmd(0x29); // Display ON
  vTaskDelay(pdMS_TO_TICKS(50));
}

// Thiết lập vùng vẽ cửa sổ trên màn hình
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  lcd_send_cmd(0x2A);
  uint8_t col_data[4] = {(uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
                         (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)};
  lcd_send_data(col_data, 4);

  lcd_send_cmd(0x2B);
  uint8_t row_data[4] = {(uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
                         (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)};
  lcd_send_data(row_data, 4);

  lcd_send_cmd(0x2C); // Memory Write
}

// Tô toàn bộ màn hình một màu sắc bất kỳ
void lcd_fill_screen(uint16_t color) {
  lcd_set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
  uint8_t buffer[256];
  for (int i = 0; i < sizeof(buffer); i += 2) {
    buffer[i] = (color >> 8) & 0xFF;
    buffer[i + 1] = color & 0xFF;
  }
  int total_pixels = LCD_WIDTH * LCD_HEIGHT;
  for (int i = 0; i < total_pixels; i += sizeof(buffer) / 2) {
    lcd_send_data(buffer, sizeof(buffer));
  }
}

// Vẽ một khối chữ nhật nhỏ (dùng để dựng font chữ đơn giản)
void lcd_draw_filled_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                          uint16_t color) {
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
    return;
  if (x + w > LCD_WIDTH)
    w = LCD_WIDTH - x;
  if (y + h > LCD_HEIGHT)
    h = LCD_HEIGHT - y;

  lcd_set_window(x, y, x + w - 1, y + h - 1);

  uint8_t hb = (color >> 8) & 0xFF;
  uint8_t lb = color & 0xFF;

  // Gửi dữ liệu theo từng dòng để tối ưu bộ nhớ
  uint8_t line_buf[w * 2];
  for (int i = 0; i < w; i++) {
    line_buf[i * 2] = hb;
    line_buf[i * 2 + 1] = lb;
  }

  for (int j = 0; j < h; j++) {
    lcd_send_data(line_buf, w * 2);
  }
}

// Hàm vẽ một ký tự đơn giản dạng khối (Block Character Renderer)
void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color) {
  // Tạo một số khối pixel đặc trưng để hiển thị chữ "ESP32-S3 OK"
  lcd_draw_filled_rect(x, y, 10, 14, color);
}

extern "C" void app_main(void) {
  printf("\n========================================\n");
  printf("=== TEST HIỂN THỊ MÀN HÌNH TFT SPI ===\n");
  printf("========================================\n");

  lcd_spi_init();
  lcd_init_sequence();
  printf("Khởi tạo màn hình thành công!\n");

  while (1) {
    // 1. Xóa nền màn hình thành màu Đen
    lcd_fill_screen(0x0000);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Vẽ các khối chữ nhật giả lập dòng chữ kiểm tra (Màu Trắng và Vàng)
    lcd_draw_filled_rect(20, 40, 92, 20,
                         0xF800); // Khối màu Đỏ (Báo hiệu hệ thống chạy)
    lcd_draw_filled_rect(20, 70, 92, 20, 0x07E0);  // Khối màu Xanh lá
    lcd_draw_filled_rect(20, 100, 92, 20, 0x001F); // Khối màu Xanh dương

    vTaskDelay(pdMS_TO_TICKS(1500));

    // 3. Đổi nền sang màu Trắng để kiểm tra tương phản
    lcd_fill_screen(0xFFFF);
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}