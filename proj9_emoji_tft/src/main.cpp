#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>

// Cấu hình chân (giữ nguyên theo phần cứng của bạn)
#define LCD_HOST SPI2_HOST
#define PIN_NUM_SCL GPIO_NUM_12
#define PIN_NUM_SDA GPIO_NUM_11
#define PIN_NUM_RES GPIO_NUM_14
#define PIN_NUM_DC GPIO_NUM_13
#define PIN_NUM_CS GPIO_NUM_10
#define PIN_NUM_BLK GPIO_NUM_9

// CHỈNH LẠI 2 THÔNG SỐ NÀY NẾU MÀN HÌNH KHÔNG LÊN HÌNH:
// - Màn có chân CS: dùng SPI_MODE = 0, USE_CS_PIN = PIN_NUM_CS
// - Màn không chân CS (GMT130-V1.0): dùng SPI_MODE = 3, USE_CS_PIN = -1
#define ACTIVE_SPI_MODE 3
#define ACTIVE_CS_PIN -1

#define LCD_H_RES 240
#define LCD_V_RES 240

// Bảng màu RGB565 cho khuôn mặt Xiaozhi
#define COLOR_BG 0x0000        // Đen tuyền
#define COLOR_EYE 0x07FF       // Xanh Cyan đặc trưng của Xiaozhi
#define COLOR_HIGHLIGHT 0xFFFF // Trắng (con ngươi/ánh sáng)
#define COLOR_BLUSH 0xF810     // Hồng má (tùy chọn)

static uint16_t *frame_buf = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

// Hàm chuyển đổi màu sang Big-Endian cho SPI ST7789
static inline uint16_t swap_color(uint16_t color) {
  return (color >> 8) | (color << 8);
}

// Xóa toàn bộ buffer về màu nền
void clear_canvas(uint16_t color) {
  uint16_t c = swap_color(color);
  for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
    frame_buf[i] = c;
  }
}

// Vẽ 1 pixel an toàn vào RAM buffer
void draw_pixel(int x, int y, uint16_t color) {
  if (x >= 0 && x < LCD_H_RES && y >= 0 && y < LCD_V_RES) {
    frame_buf[y * LCD_H_RES + x] = swap_color(color);
  }
}

// Vẽ hình chữ nhật bo góc (dùng vẽ mắt đặc trưng của Xiaozhi)
void fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
  for (int dy = 0; dy < h; dy++) {
    for (int dx = 0; dx < w; dx++) {
      bool draw = true;
      // Kiểm tra 4 góc bo tròn
      if (dx < r && dy < r) {
        if ((r - dx) * (r - dx) + (r - dy) * (r - dy) > r * r)
          draw = false;
      } else if (dx >= w - r && dy < r) {
        if ((dx - (w - r - 1)) * (dx - (w - r - 1)) + (r - dy) * (r - dy) >
            r * r)
          draw = false;
      } else if (dx < r && dy >= h - r) {
        if ((r - dx) * (r - dx) + (dy - (h - r - 1)) * (dy - (h - r - 1)) >
            r * r)
          draw = false;
      } else if (dx >= w - r && dy >= h - r) {
        if ((dx - (w - r - 1)) * (dx - (w - r - 1)) +
                (dy - (h - r - 1)) * (dy - (h - r - 1)) >
            r * r)
          draw = false;
      }
      if (draw) {
        draw_pixel(x + dx, y + dy, color);
      }
    }
  }
}

// Đẩy toàn bộ khung hình từ RAM lên màn hình (chia nhỏ từng dải 40 dòng để
// tránh tràn DMA)
void render_screen() {
  const int batch = 40;
  for (int y = 0; y < LCD_V_RES; y += batch) {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y + batch,
                              &frame_buf[y * LCD_H_RES]);
  }
}

// Vẽ biểu cảm khuôn mặt Xiaozhi
// state: 0 = Bình thường, 1 = Chớp mắt, 2 = Vui vẻ (mắt cười)
void draw_xiaozhi_face(int state, int offset_x, int offset_y) {
  clear_canvas(COLOR_BG);

  int left_eye_x = 42 + offset_x;
  int right_eye_x = 138 + offset_x;
  int eye_y = 75 + offset_y;
  int eye_w = 60;
  int eye_h = 75;

  if (state == 0) {
    // Mắt mở to bo góc + chấm sáng long lanh
    fill_round_rect(left_eye_x, eye_y, eye_w, eye_h, 16, COLOR_EYE);
    fill_round_rect(right_eye_x, eye_y, eye_w, eye_h, 16, COLOR_EYE);

    // Chấm sáng trong mắt
    fill_round_rect(left_eye_x + 12, eye_y + 14, 14, 14, 7, COLOR_HIGHLIGHT);
    fill_round_rect(right_eye_x + 12, eye_y + 14, 14, 14, 7, COLOR_HIGHLIGHT);

    // Miệng nhỏ xinh ở giữa
    fill_round_rect(105 + offset_x, 175 + offset_y, 30, 10, 5, COLOR_EYE);
  } else if (state == 1) {
    // Chớp mắt (2 đường ngang dẹt)
    fill_round_rect(left_eye_x, eye_y + 32, eye_w, 12, 6, COLOR_EYE);
    fill_round_rect(right_eye_x, eye_y + 32, eye_w, 12, 6, COLOR_EYE);
    fill_round_rect(108 + offset_x, 175 + offset_y, 24, 8, 4, COLOR_EYE);
  } else if (state == 2) {
    // Mắt cười híp (^ ^)
    fill_round_rect(left_eye_x, eye_y + 20, eye_w, 35, 16, COLOR_EYE);
    fill_round_rect(left_eye_x + 8, eye_y + 34, eye_w - 16, 25, 10, COLOR_BG);

    fill_round_rect(right_eye_x, eye_y + 20, eye_w, 35, 16, COLOR_EYE);
    fill_round_rect(right_eye_x + 8, eye_y + 34, eye_w - 16, 25, 10, COLOR_BG);

    // Miệng cười mở
    fill_round_rect(98 + offset_x, 170 + offset_y, 44, 22, 11, COLOR_EYE);
  }

  render_screen();
}

void init_lcd() {
  // Cấp phát bộ nhớ cho toàn bộ khung hình 240x240 (115.2 KB)
  // Nếu chip không có PSRAM và thiếu RAM nội, đổi MALLOC_CAP_DMA thành
  // MALLOC_CAP_8BIT
  frame_buf = (uint16_t *)heap_caps_malloc(
      LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (frame_buf == NULL) {
    printf("Lỗi: Không đủ RAM cấp phát frame_buf!\n");
    return;
  }

  gpio_set_direction(PIN_NUM_BLK, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_NUM_BLK, 1);

  if (ACTIVE_CS_PIN == -1) {
    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_CS, 0);
  }

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = PIN_NUM_SCL;
  buscfg.mosi_io_num = PIN_NUM_SDA;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = PIN_NUM_DC;
  io_config.cs_gpio_num = (gpio_num_t)ACTIVE_CS_PIN;
  io_config.pclk_hz = 20 * 1000 * 1000; // Tăng lên 20MHz cho mượt
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = ACTIVE_SPI_MODE;
  io_config.trans_queue_depth = 10;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                           &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config = {};
  panel_config.reset_gpio_num = PIN_NUM_RES;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  vTaskDelay(pdMS_TO_TICKS(150));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  vTaskDelay(pdMS_TO_TICKS(150));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

extern "C" void app_main(void) {
  init_lcd();
  if (!frame_buf)
    return;

  printf("Bắt đầu chạy animation Emoji Xiaozhi!\n");

  while (1) {
    // 1. Nhìn thẳng bình thường
    draw_xiaozhi_face(0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 2. Chớp mắt nhanh
    draw_xiaozhi_face(1, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    draw_xiaozhi_face(0, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 3. Liếc mắt sang trái
    draw_xiaozhi_face(0, -15, -5);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4. Liếc mắt sang phải
    draw_xiaozhi_face(0, 15, -5);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 5. Cười vui vẻ
    draw_xiaozhi_face(2, 0, 0);
    vTaskDelay(pdMS_TO_TICKS(1800));
  }
}