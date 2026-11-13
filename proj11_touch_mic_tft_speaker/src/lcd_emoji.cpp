#include "lcd_emoji.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sound_fx.h"


static const char *TAG = "LCD_EMOJI";

#define LCD_H_RES 240
#define LCD_V_RES 240

#define COLOR_BG 0x0000
#define COLOR_CYAN 0x07FF
#define COLOR_RED 0xF800
#define COLOR_WHITE 0xFFFF
#define COLOR_PINK 0xF810

static uint16_t *frame_buf = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

static inline uint16_t swap_color(uint16_t color) {
  return (color >> 8) | (color << 8);
}

static void clear_canvas(uint16_t color) {
  uint16_t c = swap_color(color);
  for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
    frame_buf[i] = c;
  }
}

static void draw_pixel(int x, int y, uint16_t color) {
  if (x >= 0 && x < LCD_H_RES && y >= 0 && y < LCD_V_RES) {
    frame_buf[y * LCD_H_RES + x] = swap_color(color);
  }
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
  for (int dy = 0; dy < h; dy++) {
    for (int dx = 0; dx < w; dx++) {
      bool draw = true;
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
      if (draw)
        draw_pixel(x + dx, y + dy, color);
    }
  }
}

static void render_screen() {
  const int batch = 40;
  for (int y = 0; y < LCD_V_RES; y += batch) {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y + batch,
                              &frame_buf[y * LCD_H_RES]);
  }
}

static void render_xiaozhi(emoji_mode_t mode, bool blink, int offset_x,
                           int offset_y, int mouth_extra) {
  clear_canvas(COLOR_BG);

  int lx = 42 + offset_x;
  int rx = 138 + offset_x;
  int ey = 75 + offset_y;
  int ew = 60;
  int eh = 75;

  if (blink && mode != EMOJI_SLEEP) {
    uint16_t c = (mode == EMOJI_ANGRY) ? COLOR_RED : COLOR_CYAN;
    fill_round_rect(lx, ey + 32, ew, 10, 5, c);
    fill_round_rect(rx, ey + 32, ew, 10, 5, c);
    fill_round_rect(105 + offset_x, 172 + offset_y, 30, 10 + mouth_extra, 5, c);
    render_screen();
    return;
  }

  switch (mode) {
  case EMOJI_NORMAL:
  default:
    fill_round_rect(lx, ey, ew, eh, 16, COLOR_CYAN);
    fill_round_rect(rx, ey, ew, eh, 16, COLOR_CYAN);
    fill_round_rect(lx + 12, ey + 14, 14, 14, 7, COLOR_WHITE);
    fill_round_rect(rx + 12, ey + 14, 14, 14, 7, COLOR_WHITE);
    // Miệng mở to nhỏ theo tiếng nói từ Mic (mouth_extra)
    fill_round_rect(102 + offset_x, 170 + offset_y, 36, 10 + mouth_extra, 6,
                    COLOR_CYAN);
    break;

  case EMOJI_HAPPY:
    fill_round_rect(lx, ey + 15, ew, 40, 18, COLOR_CYAN);
    fill_round_rect(lx + 8, ey + 30, ew - 16, 30, 12, COLOR_BG);
    fill_round_rect(rx, ey + 15, ew, 40, 18, COLOR_CYAN);
    fill_round_rect(rx + 8, ey + 30, ew - 16, 30, 12, COLOR_BG);
    fill_round_rect(25 + offset_x, 145 + offset_y, 22, 12, 6, COLOR_PINK);
    fill_round_rect(193 + offset_x, 145 + offset_y, 22, 12, 6, COLOR_PINK);
    fill_round_rect(96 + offset_x, 165 + offset_y, 48, 20 + mouth_extra, 10,
                    COLOR_CYAN);
    break;

  case EMOJI_ANGRY:
    fill_round_rect(lx, ey + 10, ew, eh - 15, 12, COLOR_RED);
    fill_round_rect(rx, ey + 10, ew, eh - 15, 12, COLOR_RED);
    for (int y = 0; y < 25; y++) {
      for (int x = 0; x < ew; x++) {
        if (x > y * 2)
          draw_pixel(lx + x, ey + 10 + y, COLOR_BG);
        if ((ew - x) > y * 2)
          draw_pixel(rx + x, ey + 10 + y, COLOR_BG);
      }
    }
    fill_round_rect(95 + offset_x, 172 + offset_y, 50, 12 + mouth_extra, 5,
                    COLOR_RED);
    break;

  case EMOJI_SAD:
    fill_round_rect(lx, ey + 15, ew, eh - 20, 14, COLOR_CYAN);
    fill_round_rect(rx, ey + 15, ew, eh - 20, 14, COLOR_CYAN);
    for (int y = 0; y < 20; y++) {
      for (int x = 0; x < ew; x++) {
        if ((ew - x) > y * 2)
          draw_pixel(lx + x, ey + 15 + y, COLOR_BG);
        if (x > y * 2)
          draw_pixel(rx + x, ey + 15 + y, COLOR_BG);
      }
    }
    fill_round_rect(lx + 10, ey + eh, 10, 18, 5, COLOR_WHITE);
    fill_round_rect(rx + ew - 20, ey + eh, 10, 18, 5, COLOR_WHITE);
    fill_round_rect(100 + offset_x, 180 + offset_y, 40, 8 + (mouth_extra / 2),
                    4, COLOR_CYAN);
    break;

  case EMOJI_SLEEP:
    fill_round_rect(lx, ey + 35, ew, 10, 5, COLOR_CYAN);
    fill_round_rect(rx, ey + 35, ew, 10, 5, COLOR_CYAN);
    fill_round_rect(112 + offset_x, 175 + offset_y, 16, 16, 8, COLOR_CYAN);
    fill_round_rect(155, 45 + offset_y, 28, 28, 14, COLOR_WHITE);
    fill_round_rect(160, 50 + offset_y, 18, 18, 9, COLOR_BG);
    break;

  case EMOJI_LOVE:
    fill_round_rect(lx, ey, ew, eh, 20, COLOR_PINK);
    fill_round_rect(rx, ey, ew, eh, 20, COLOR_PINK);
    fill_round_rect(lx + 10, ey + 10, 20, 20, 10, COLOR_WHITE);
    fill_round_rect(rx + 10, ey + 10, 20, 20, 10, COLOR_WHITE);
    fill_round_rect(lx + 35, ey + 45, 10, 10, 5, COLOR_WHITE);
    fill_round_rect(rx + 35, ey + 45, 10, 10, 5, COLOR_WHITE);
    fill_round_rect(100 + offset_x, 168 + offset_y, 40, 16 + mouth_extra, 8,
                    COLOR_PINK);
    break;
  }

  render_screen();
}

void init_lcd_hardware(void) {
  frame_buf = (uint16_t *)heap_caps_malloc(
      LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_DMA);
  if (!frame_buf) {
    frame_buf = (uint16_t *)heap_caps_malloc(
        LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_8BIT);
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
  io_config.pclk_hz = 20 * 1000 * 1000;
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
  ESP_LOGI(TAG, "Khoi tao man hinh ST7789 thanh cong!");
}

void lcd_emoji_task(void *pvParameters) {
  QueueHandle_t emoji_queue = (QueueHandle_t)pvParameters;
  emoji_mode_t current_mode = EMOJI_AUTO;
  emoji_mode_t active_face = EMOJI_NORMAL;
  int tick_count = 0;
  int offset_x = 0;
  int offset_y = 0;

  while (1) {
    emoji_mode_t new_mode;
    if (xQueueReceive(emoji_queue, &new_mode, pdMS_TO_TICKS(60)) == pdPASS) {
      current_mode = new_mode;
      tick_count = 0;
      offset_x = 0;
      offset_y = 0;
      play_emoji_sound(current_mode);
    }

    if (current_mode == EMOJI_AUTO) {
      int phase = (tick_count / 60) % 5;
      if (phase == 0)
        active_face = EMOJI_NORMAL;
      else if (phase == 1)
        active_face = EMOJI_HAPPY;
      else if (phase == 2)
        active_face = EMOJI_LOVE;
      else if (phase == 3)
        active_face = EMOJI_SAD;
      else
        active_face = EMOJI_ANGRY;
    } else {
      active_face = current_mode;
    }

    bool is_blinking = (tick_count % 45 == 20);
    if (active_face == EMOJI_SLEEP) {
      offset_y = (tick_count % 14 < 7) ? -4 : 4;
      offset_x = 0;
    } else {
      if (tick_count % 60 == 15)
        offset_x = -12;
      else if (tick_count % 60 == 30)
        offset_x = 12;
      else if (tick_count % 60 == 45)
        offset_x = 0;
    }

    render_xiaozhi(active_face, is_blinking, offset_x, offset_y,
                   g_mic_mouth_open);
    tick_count++;
  }
}