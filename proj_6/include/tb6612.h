#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cấu hình chân phần cứng cho module TB6612FNG
 */
typedef struct {
    // Chân điều khiển Motor A
    gpio_num_t pwma_pin;
    gpio_num_t ain1_pin;
    gpio_num_t ain2_pin;

    // Chân điều khiển Motor B
    gpio_num_t pwmb_pin;
    gpio_num_t bin1_pin;
    gpio_num_t bin2_pin;

    // Chân Standby
    gpio_num_t stby_pin;

    // Kênh LEDC
    ledc_channel_t ledc_channel_a;
    ledc_channel_t ledc_channel_b;
    ledc_timer_t   ledc_timer;
    uint32_t       pwm_freq_hz;
} tb6612_config_t;

/**
 * @brief Cấu hình mặc định dành cho ESP32-S3
 */
#define TB6612_DEFAULT_CONFIG() {          \
    .pwma_pin = GPIO_NUM_1,                \
    .ain1_pin = GPIO_NUM_2,                \
    .ain2_pin = GPIO_NUM_3,                \
    .pwmb_pin = GPIO_NUM_5,                \
    .bin1_pin = GPIO_NUM_6,                \
    .bin2_pin = GPIO_NUM_7,                \
    .stby_pin = GPIO_NUM_4,                \
    .ledc_channel_a = LEDC_CHANNEL_0,      \
    .ledc_channel_b = LEDC_CHANNEL_1,      \
    .ledc_timer     = LEDC_TIMER_0,        \
    .pwm_freq_hz    = 5000,                \
}

/**
 * @brief Khởi tạo module TB6612FNG
 */
esp_err_t tb6612_init(const tb6612_config_t *config);

/**
 * @brief Bật hoặc tắt trạng thái Standby (tiết kiệm điện)
 * @param enable true để bật hoạt động, false để tắt/ngủ
 */
void tb6612_enable(bool enable);

/**
 * @brief Điều khiển tốc độ Motor A
 * @param speed_percent Tốc độ từ -100% (quay ngược) đến +100% (quay thuận). 0: Thả trôi dừng (Coast).
 */
void tb6612_motor_a_drive(int speed_percent);

/**
 * @brief Điều khiển tốc độ Motor B
 * @param speed_percent Tốc độ từ -100% (quay ngược) đến +100% (quay thuận). 0: Thả trôi dừng (Coast).
 */
void tb6612_motor_b_drive(int speed_percent);

/**
 * @brief Phanh hãm tức thì Motor A (Short brake)
 */
void tb6612_motor_a_brake(void);

/**
 * @brief Phanh hãm tức thì Motor B (Short brake)
 */
void tb6612_motor_b_brake(void);

/**
 * @brief Thả trôi dừng cả 2 động cơ (Coast stop)
 */
void tb6612_stop_all(void);

/**
 * @brief Phanh gấp cả 2 động cơ (Short brake all)
 */
void tb6612_brake_all(void);

/**
 * @brief Điều khiển di chuyển xe 2 bánh vi sai (Differential Drive)
 * @param speed Tốc độ tới/lùi (-100% đến +100%)
 * @param turn  Độ bẻ lái (-100%: rẽ trái gắt, +100%: rẽ phải gắt, 0: đi thẳng)
 */
void tb6612_robot_drive(int speed, int turn);

/**
 * @brief Tăng/giảm tốc độ từ từ (Ramp) để tránh sụt nguồn ESP32 do dòng khởi động động cơ
 * @param target_a Tốc độ mục tiêu Motor A (-100 đến 100)
 * @param target_b Tốc độ mục tiêu Motor B (-100 đến 100)
 * @param step_delay_ms Độ trễ giữa mỗi nấc tăng tốc (thường 10 - 20ms)
 */
void tb6612_ramp_drive(int target_a, int target_b, uint32_t step_delay_ms);

#ifdef __cplusplus
}
#endif

