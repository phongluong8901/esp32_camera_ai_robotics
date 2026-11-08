#include "tb6612.h"
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TB6612";

#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY   1023
#define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE

static tb6612_config_t s_cfg;
static int s_curr_speed_a = 0;
static int s_curr_speed_b = 0;

esp_err_t tb6612_init(const tb6612_config_t *config) {
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *config;

    // 1. Cấu hình GPIO Output cho các chân điều khiển hướng và Standby
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << s_cfg.ain1_pin) | (1ULL << s_cfg.ain2_pin) |
                           (1ULL << s_cfg.bin1_pin) | (1ULL << s_cfg.bin2_pin) |
                           (1ULL << s_cfg.stby_pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi cau hinh GPIO: %s", esp_err_to_name(err));
        return err;
    }

    // Mặc định kéo Standby lên HIGH để bật IC
    gpio_set_level(s_cfg.stby_pin, 1);

    // 2. Cấu hình Timer cho LEDC PWM
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = LEDC_SPEED_MODE;
    ledc_timer.duty_resolution = LEDC_DUTY_RES;
    ledc_timer.timer_num = s_cfg.ledc_timer;
    ledc_timer.freq_hz = s_cfg.pwm_freq_hz > 0 ? s_cfg.pwm_freq_hz : 5000;
    ledc_timer.clk_cfg = LEDC_AUTO_CLK;
    err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi cau hinh LEDC Timer: %s", esp_err_to_name(err));
        return err;
    }

    // 3. Cấu hình kênh PWM Motor A
    ledc_channel_config_t ch_a = {};
    ch_a.gpio_num = s_cfg.pwma_pin;
    ch_a.speed_mode = LEDC_SPEED_MODE;
    ch_a.channel = s_cfg.ledc_channel_a;
    ch_a.intr_type = LEDC_INTR_DISABLE;
    ch_a.timer_sel = s_cfg.ledc_timer;
    ch_a.duty = 0;
    ch_a.hpoint = 0;
    err = ledc_channel_config(&ch_a);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi cau hinh Channel A: %s", esp_err_to_name(err));
        return err;
    }

    // 4. Cấu hình kênh PWM Motor B
    ledc_channel_config_t ch_b = {};
    ch_b.gpio_num = s_cfg.pwmb_pin;
    ch_b.speed_mode = LEDC_SPEED_MODE;
    ch_b.channel = s_cfg.ledc_channel_b;
    ch_b.intr_type = LEDC_INTR_DISABLE;
    ch_b.timer_sel = s_cfg.ledc_timer;
    ch_b.duty = 0;
    ch_b.hpoint = 0;
    err = ledc_channel_config(&ch_b);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi cau hinh Channel B: %s", esp_err_to_name(err));
        return err;
    }

    tb6612_stop_all();
    ESP_LOGI(TAG, "Khoi tao driver TB6612 thanh cong!");
    return ESP_OK;
}

void tb6612_enable(bool enable) {
    gpio_set_level(s_cfg.stby_pin, enable ? 1 : 0);
}

void tb6612_motor_a_drive(int speed_percent) {
    if (speed_percent > 100) speed_percent = 100;
    if (speed_percent < -100) speed_percent = -100;
    s_curr_speed_a = speed_percent;

    uint32_t duty = (abs(speed_percent) * LEDC_MAX_DUTY) / 100;

    if (speed_percent > 0) {
        // Quay thuận (CW)
        gpio_set_level(s_cfg.ain1_pin, 1);
        gpio_set_level(s_cfg.ain2_pin, 0);
    } else if (speed_percent < 0) {
        // Quay ngược (CCW)
        gpio_set_level(s_cfg.ain1_pin, 0);
        gpio_set_level(s_cfg.ain2_pin, 1);
    } else {
        // Thả trôi dừng (Coast)
        gpio_set_level(s_cfg.ain1_pin, 0);
        gpio_set_level(s_cfg.ain2_pin, 0);
        duty = 0;
    }

    ledc_set_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_a, duty);
    ledc_update_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_a);
}

void tb6612_motor_b_drive(int speed_percent) {
    if (speed_percent > 100) speed_percent = 100;
    if (speed_percent < -100) speed_percent = -100;
    s_curr_speed_b = speed_percent;

    uint32_t duty = (abs(speed_percent) * LEDC_MAX_DUTY) / 100;

    if (speed_percent > 0) {
        // Quay thuận (CW)
        gpio_set_level(s_cfg.bin1_pin, 1);
        gpio_set_level(s_cfg.bin2_pin, 0);
    } else if (speed_percent < 0) {
        // Quay ngược (CCW)
        gpio_set_level(s_cfg.bin1_pin, 0);
        gpio_set_level(s_cfg.bin2_pin, 1);
    } else {
        // Thả trôi dừng (Coast)
        gpio_set_level(s_cfg.bin1_pin, 0);
        gpio_set_level(s_cfg.bin2_pin, 0);
        duty = 0;
    }

    ledc_set_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_b, duty);
    ledc_update_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_b);
}

void tb6612_motor_a_brake(void) {
    gpio_set_level(s_cfg.ain1_pin, 1);
    gpio_set_level(s_cfg.ain2_pin, 1);
    ledc_set_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_a, 0);
    ledc_update_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_a);
    s_curr_speed_a = 0;
}

void tb6612_motor_b_brake(void) {
    gpio_set_level(s_cfg.bin1_pin, 1);
    gpio_set_level(s_cfg.bin2_pin, 1);
    ledc_set_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_b, 0);
    ledc_update_duty(LEDC_SPEED_MODE, s_cfg.ledc_channel_b);
    s_curr_speed_b = 0;
}

void tb6612_stop_all(void) {
    tb6612_motor_a_drive(0);
    tb6612_motor_b_drive(0);
}

void tb6612_brake_all(void) {
    tb6612_motor_a_brake();
    tb6612_motor_b_brake();
}

void tb6612_robot_drive(int speed, int turn) {
    int left_speed = speed + turn;
    int right_speed = speed - turn;

    if (left_speed > 100) left_speed = 100;
    if (left_speed < -100) left_speed = -100;
    if (right_speed > 100) right_speed = 100;
    if (right_speed < -100) right_speed = -100;

    tb6612_motor_a_drive(left_speed);
    tb6612_motor_b_drive(right_speed);
}

void tb6612_ramp_drive(int target_a, int target_b, uint32_t step_delay_ms) {
    if (target_a > 100) target_a = 100;
    if (target_a < -100) target_a = -100;
    if (target_b > 100) target_b = 100;
    if (target_b < -100) target_b = -100;

    while (s_curr_speed_a != target_a || s_curr_speed_b != target_b) {
        if (s_curr_speed_a < target_a) s_curr_speed_a++;
        else if (s_curr_speed_a > target_a) s_curr_speed_a--;

        if (s_curr_speed_b < target_b) s_curr_speed_b++;
        else if (s_curr_speed_b > target_b) s_curr_speed_b--;

        tb6612_motor_a_drive(s_curr_speed_a);
        tb6612_motor_b_drive(s_curr_speed_b);

        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }
}

