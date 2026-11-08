#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>


#define LCD_HOST SPI2_HOST
#define PIN_NUM_SCL GPIO_NUM_12
#define PIN_NUM_SDA GPIO_NUM_11
#define PIN_NUM_RES GPIO_NUM_14
#define PIN_NUM_DC GPIO_NUM_13
#define PIN_NUM_CS GPIO_NUM_10
#define PIN_NUM_BLK GPIO_NUM_9

#define LCD_H_RES 240
#define LCD_V_RES 240
#define LINES_BATCH 40

static uint16_t *line_buf = NULL;

void fill_screen_safe(esp_lcd_panel_handle_t panel_handle, uint16_t color) {
  uint16_t swapped = (color >> 8) | (color << 8);
  for (int i = 0; i < LCD_H_RES * LINES_BATCH; i++) {
    line_buf[i] = swapped;
  }
  for (int y = 0; y < LCD_V_RES; y += LINES_BATCH) {
    esp_lcd_panel_draw_bitmap(panel_handle, 0, y, LCD_H_RES, y + LINES_BATCH,
                              line_buf);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void test_with_config(int spi_mode, int cs_pin) {
  printf("\n>>> ĐANG TEST: SPI_MODE = %d | CS_PIN = %d <<<\n", spi_mode,
         cs_pin);

  if (cs_pin == -1) {
    gpio_set_direction(PIN_NUM_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NUM_CS, 0);
  }

  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.dc_gpio_num = PIN_NUM_DC;
  io_config.cs_gpio_num =
      (gpio_num_t)cs_pin; // Ép kiểu tường minh tránh lỗi -fpermissive
  io_config.pclk_hz = 10 * 1000 * 1000;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  io_config.spi_mode = spi_mode;
  io_config.trans_queue_depth = 10;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST,
                                           &io_config, &io_handle));

  esp_lcd_panel_handle_t panel_handle = NULL;
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

  // Đã sửa cú pháp gọi hàm bị cụt trước đó
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  printf(" -> Vẽ màu ĐỎ (MODE %d)\n", spi_mode);
  fill_screen_safe(panel_handle, 0xF800);
  vTaskDelay(pdMS_TO_TICKS(1000));

  printf(" -> Vẽ màu XANH LÁ (MODE %d)\n", spi_mode);
  fill_screen_safe(panel_handle, 0x07E0);
  vTaskDelay(pdMS_TO_TICKS(1000));

  printf(" -> Vẽ màu XANH DƯƠNG (MODE %d)\n", spi_mode);
  fill_screen_safe(panel_handle, 0x001F);
  vTaskDelay(pdMS_TO_TICKS(1000));

  esp_lcd_panel_del(panel_handle);
  esp_lcd_panel_io_del(io_handle);
}

extern "C" void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(1000));

  line_buf = (uint16_t *)heap_caps_malloc(
      LCD_H_RES * LINES_BATCH * sizeof(uint16_t), MALLOC_CAP_DMA);

  gpio_set_direction(PIN_NUM_BLK, GPIO_MODE_OUTPUT);
  printf("Kiểm tra đèn nền BLK (GPIO 9): Tắt 0.5s -> Bật...\n");
  gpio_set_level(PIN_NUM_BLK, 0);
  vTaskDelay(pdMS_TO_TICKS(500));
  gpio_set_level(PIN_NUM_BLK, 1);

  spi_bus_config_t buscfg = {};
  buscfg.sclk_io_num = PIN_NUM_SCL;
  buscfg.mosi_io_num = PIN_NUM_SDA;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = LCD_H_RES * LINES_BATCH * sizeof(uint16_t);
  ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

  while (1) {
    test_with_config(0, PIN_NUM_CS);
    test_with_config(3, -1);
    test_with_config(2, -1);
  }
}