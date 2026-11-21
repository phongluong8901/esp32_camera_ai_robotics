// #include "driver/gpio.h"
// #include "driver/ledc.h"
// #include "esp_err.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include <stdio.h>
// #include <string.h>

// // Chân tín hiệu PWM cho 2 servo
// #define SERVO1_GPIO GPIO_NUM_15
// #define SERVO2_GPIO GPIO_NUM_16

// // Cấu hình LEDC PWM 50Hz (chu kỳ 20,000 us)
// #define LEDC_TIMER LEDC_TIMER_0
// #define LEDC_MODE LEDC_LOW_SPEED_MODE
// #define LEDC_DUTY_RES LEDC_TIMER_14_BIT // Độ phân giải 14-bit (0 - 16383)
// #define LEDC_FREQUENCY 50               // 50Hz chuẩn servo

// #define SERVO1_CHANNEL LEDC_CHANNEL_0
// #define SERVO2_CHANNEL LEDC_CHANNEL_1

// // Dải xung chuẩn của MG996R (500us = 0 độ, 2500us = 180 độ)
// #define SERVO_MIN_PULSE_US 500
// #define SERVO_MAX_PULSE_US 2500
// #define SERVO_MAX_DEGREE 180

// // Đổi góc (0 - 180 độ) sang giá trị duty cycle 14-bit
// static uint32_t angle_to_duty(int angle) {
//   if (angle < 0)
//     angle = 0;
//   if (angle > SERVO_MAX_DEGREE)
//     angle = SERVO_MAX_DEGREE;

//   uint32_t pulse_us =
//       SERVO_MIN_PULSE_US +
//       ((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle) / SERVO_MAX_DEGREE;
//   uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;
//   return (pulse_us * max_duty) / 20000;
// }

// // Khởi tạo PWM cho 2 servo
// static void servo_init(void) {
//   // 1. Cấu hình Timer
//   ledc_timer_config_t timer_conf = {};
//   timer_conf.speed_mode = LEDC_MODE;
//   timer_conf.duty_resolution = LEDC_DUTY_RES;
//   timer_conf.timer_num = LEDC_TIMER;
//   timer_conf.freq_hz = LEDC_FREQUENCY;
//   timer_conf.clk_cfg = LEDC_AUTO_CLK;
//   ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

//   // 2. Cấu hình Kênh 0 cho Servo 1
//   ledc_channel_config_t ch1_conf = {};
//   ch1_conf.gpio_num = SERVO1_GPIO;
//   ch1_conf.speed_mode = LEDC_MODE;
//   ch1_conf.channel = SERVO1_CHANNEL;
//   ch1_conf.timer_sel = LEDC_TIMER;
//   ch1_conf.duty = angle_to_duty(90); // Khởi động ở góc 90 độ
//   ch1_conf.hpoint = 0;
//   ESP_ERROR_CHECK(ledc_channel_config(&ch1_conf));

//   // 3. Cấu hình Kênh 1 cho Servo 2
//   ledc_channel_config_t ch2_conf = {};
//   ch2_conf.gpio_num = SERVO2_GPIO;
//   ch2_conf.speed_mode = LEDC_MODE;
//   ch2_conf.channel = SERVO2_CHANNEL;
//   ch2_conf.timer_sel = LEDC_TIMER;
//   ch2_conf.duty = angle_to_duty(90); // Khởi động ở góc 90 độ
//   ch2_conf.hpoint = 0;
//   ESP_ERROR_CHECK(ledc_channel_config(&ch2_conf));
// }

// // Hàm quay servo đến góc mong muốn
// static void servo_set_angle(ledc_channel_t channel, int angle) {
//   uint32_t duty = angle_to_duty(angle);
//   ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
//   ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
// }

// extern "C" void app_main(void) {
//   printf("\n=== TEST 2 SERVO MG996R ===\n");
//   servo_init();

//   while (1) {
//     // Quay cùng lúc về 0 độ
//     printf("Servo 1 & 2 -> 0 do\n");
//     servo_set_angle(SERVO1_CHANNEL, 0);
//     servo_set_angle(SERVO2_CHANNEL, 0);
//     vTaskDelay(pdMS_TO_TICKS(1500));

//     // Quay về 90 độ
//     printf("Servo 1 & 2 -> 90 do\n");
//     servo_set_angle(SERVO1_CHANNEL, 90);
//     servo_set_angle(SERVO2_CHANNEL, 90);
//     vTaskDelay(pdMS_TO_TICKS(1500));

//     // Quay đối nghịch (Servo 1: 180 độ, Servo 2: 0 độ)
//     printf("Servo 1 -> 180 do | Servo 2 -> 0 do\n");
//     servo_set_angle(SERVO1_CHANNEL, 180);
//     servo_set_angle(SERVO2_CHANNEL, 0);
//     vTaskDelay(pdMS_TO_TICKS(1500));
//   }
// }