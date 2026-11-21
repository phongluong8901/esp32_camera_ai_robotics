#include "motor_ctrl.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <stdlib.h>


static const char *TAG = "MOTOR_CTRL";

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT // 0 -> 1023
#define LEDC_FREQUENCY 20000            // 20kHz (chay em, khong keu rit)
#define MAX_DUTY ((1 << LEDC_DUTY_RES) - 1)

static void set_single_motor(int in1, int in2, ledc_channel_t ch, int speed) {
  if (speed > 100)
    speed = 100;
  if (speed < -100)
    speed = -100;

  if (speed > 0) {
    gpio_set_level(in1, 1);
    gpio_set_level(in2, 0);
  } else if (speed < 0) {
    gpio_set_level(in1, 0);
    gpio_set_level(in2, 1);
  } else {
    gpio_set_level(in1, 0);
    gpio_set_level(in2, 0);
  }

  uint32_t duty = (abs(speed) * MAX_DUTY) / 100;
  ledc_set_duty(LEDC_MODE, ch, duty);
  ledc_update_duty(LEDC_MODE, ch);
}

void motor_ctrl_init(void) {
  // Cau hinh cac chan dieu khien chieu quay & STBY
  gpio_config_t io_conf = {.pin_bit_mask =
                               (1ULL << MOTOR_STBY_GPIO) |
                               (1ULL << MOTOR_A_IN1) | (1ULL << MOTOR_A_IN2) |
                               (1ULL << MOTOR_B_IN1) | (1ULL << MOTOR_B_IN2),
                           .mode = GPIO_MODE_OUTPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  ESP_ERROR_CHECK(gpio_config(&io_conf));

  // Bat chan STBY de kich hoat TB6612FNG
  gpio_set_level(MOTOR_STBY_GPIO, 1);

  // Cau hinh PWM 20kHz
  ledc_timer_config_t timer_conf = {.speed_mode = LEDC_MODE,
                                    .timer_num = LEDC_TIMER,
                                    .duty_resolution = LEDC_DUTY_RES,
                                    .freq_hz = LEDC_FREQUENCY,
                                    .clk_cfg = LEDC_AUTO_CLK};
  ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

  int pwm_gpios[2] = {MOTOR_A_PWM, MOTOR_B_PWM};
  ledc_channel_t channels[2] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1};

  for (int i = 0; i < 2; i++) {
    ledc_channel_config_t ch_conf = {.speed_mode = LEDC_MODE,
                                     .channel = channels[i],
                                     .timer_sel = LEDC_TIMER,
                                     .intr_type = LEDC_INTR_DISABLE,
                                     .gpio_num = pwm_gpios[i],
                                     .duty = 0,
                                     .hpoint = 0};
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));
  }

  motor_ctrl_stop();
  ESP_LOGI(TAG, "Khoi tao TB6612FNG (20kHz PWM) thanh cong");
}

void motor_ctrl_set(int left_speed, int right_speed) {
  set_single_motor(MOTOR_A_IN1, MOTOR_A_IN2, LEDC_CHANNEL_0, left_speed);
  set_single_motor(MOTOR_B_IN1, MOTOR_B_IN2, LEDC_CHANNEL_1, right_speed);
  ESP_LOGI(TAG, "Motor L: %d%% | R: %d%%", left_speed, right_speed);
}

void motor_ctrl_stop(void) { motor_ctrl_set(0, 0); }