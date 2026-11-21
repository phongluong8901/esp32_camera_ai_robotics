#include "servo_ctrl.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "SERVO_CTRL";

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE // ESP32-S3 chi dung Low Speed
#define LEDC_DUTY_RES LEDC_TIMER_14_BIT
#define LEDC_FREQUENCY 50 // 50Hz (20ms)
#define SERVO_MIN_US 500  // 0 do
#define SERVO_MAX_US 2500 // 180 do

static uint32_t angle_to_duty(int angle) {
  if (angle < 0)
    angle = 0;
  if (angle > 180)
    angle = 180;
  uint32_t pulse_us =
      SERVO_MIN_US + ((SERVO_MAX_US - SERVO_MIN_US) * angle) / 180;
  return (pulse_us * ((1 << LEDC_DUTY_RES) - 1)) / 20000;
}

void servo_ctrl_init(void) {
  ledc_timer_config_t timer_conf = {.speed_mode = LEDC_MODE,
                                    .timer_num = LEDC_TIMER,
                                    .duty_resolution = LEDC_DUTY_RES,
                                    .freq_hz = LEDC_FREQUENCY,
                                    .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

  int gpios[2] = {SERVO1_GPIO, SERVO2_GPIO};
  ledc_channel_t channels[2] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1};

  for (int i = 0; i < 2; i++) {
    ledc_channel_config_t ch_conf = {.speed_mode = LEDC_MODE,
                                     .channel = channels[i],
                                     .timer_sel = LEDC_TIMER,
                                     .intr_type = LEDC_INTR_DISABLE,
                                     .gpio_num = gpios[i],
                                     .duty = angle_to_duty(90),
                                     .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));
  }
  ESP_LOGI(TAG, "Khoi tao 2 Servo (GPIO %d, %d) thanh cong", SERVO1_GPIO,
           SERVO2_GPIO);
}

void servo_ctrl_set_angle(int servo_id, int angle) {
  if (servo_id != 1 && servo_id != 2)
    return;
  ledc_channel_t ch = (servo_id == 1) ? LEDC_CHANNEL_0 : LEDC_CHANNEL_1;
  uint32_t duty = angle_to_duty(angle);
  ledc_set_duty(LEDC_MODE, ch, duty);
  ledc_update_duty(LEDC_MODE, ch);
  ESP_LOGI(TAG, "Servo %d -> %d do", servo_id, angle);
}