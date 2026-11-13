#include "sound_fx.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_10_BIT
#define LEDC_DUTY_ON (512) // 50% duty cycle tạo sóng vuông âm lượng rõ nhất

void init_speaker_pwm(void) {
  ledc_timer_config_t timer_conf = {};
  timer_conf.speed_mode = LEDC_MODE;
  timer_conf.duty_resolution = LEDC_DUTY_RES;
  timer_conf.timer_num = LEDC_TIMER;
  timer_conf.freq_hz = 1000;
  timer_conf.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_conf);

  ledc_channel_config_t ch_conf = {};
  ch_conf.gpio_num = PIN_SPEAKER_PWM;
  ch_conf.speed_mode = LEDC_MODE;
  ch_conf.channel = LEDC_CHANNEL;
  ch_conf.timer_sel = LEDC_TIMER;
  ch_conf.duty = 0;
  ch_conf.hpoint = 0;
  ledc_channel_config(&ch_conf);
}

void play_tone(int freq_hz, int duration_ms) {
  if (freq_hz > 0) {
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_ON);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
  }
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void play_touch_beep(void) {
  play_tone(1200, 60);
  vTaskDelay(pdMS_TO_TICKS(20));
  play_tone(1800, 80);
}

void play_emoji_sound(emoji_mode_t mode) {
  switch (mode) {
  case EMOJI_HAPPY:
  case EMOJI_LOVE:
    play_tone(880, 70);
    play_tone(1175, 70);
    play_tone(1568, 120);
    break;
  case EMOJI_ANGRY:
    play_tone(300, 120);
    vTaskDelay(pdMS_TO_TICKS(30));
    play_tone(220, 180);
    break;
  case EMOJI_SAD:
    play_tone(600, 150);
    play_tone(450, 200);
    break;
  case EMOJI_SLEEP:
    play_tone(500, 100);
    play_tone(350, 150);
    break;
  default:
    play_tone(1000, 60);
    break;
  }
}

void play_pcm_samples(const int16_t *samples, int count, int sample_rate) {
  if (!samples || count <= 0 || sample_rate <= 0)
    return;

  // Cấu hình LEDC PWM tần số cao (62.5kHz, 8-bit) làm 1-bit DAC cho PAM8403
  ledc_timer_config_t timer_conf = {};
  timer_conf.speed_mode = LEDC_MODE;
  timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
  timer_conf.timer_num = LEDC_TIMER;
  timer_conf.freq_hz = 62500;
  timer_conf.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_conf);

  int64_t interval_us = 1000000 / sample_rate;
  int64_t next_us = esp_timer_get_time();

  for (int i = 0; i < count; i++) {
    // Ánh xạ 16-bit (-32768..32767) sang dải duty (5..250)
    int duty = 128 + (samples[i] / 256);
    if (duty < 5)
      duty = 5;
    if (duty > 250)
      duty = 250;

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (uint32_t)duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    next_us += interval_us;
    int64_t now = esp_timer_get_time();
    if (next_us > now) {
      esp_rom_delay_us((uint32_t)(next_us - now));
    } else {
      next_us = now; // Đồng bộ lại nếu bị trễ
    }

    // Nhường CPU nhẹ mỗi 2000 mẫu (~125ms) mà không làm lệch nhịp
    if ((i % 2000) == 0 && i > 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      next_us = esp_timer_get_time(); // Cập nhật lại mốc thời gian sau delay
    }
  }

  // Tắt âm lượng và khôi phục lại cấu hình PWM ban đầu
  ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
  ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
  init_speaker_pwm();
}