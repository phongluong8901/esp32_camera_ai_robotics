#include "mic_touch.h"
#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sound_fx.h"
#include <math.h>

static const char *TAG = "MIC_TOUCH";

volatile int g_mic_mouth_open = 0;
volatile int g_mic_volume_percent = 0;

#define REC_MAX_SECONDS 5
#define REC_SAMPLE_RATE 16000
#define REC_MAX_SAMPLES (REC_MAX_SECONDS * REC_SAMPLE_RATE)

static int16_t *s_rec_buf = NULL;
static int s_rec_sample_count = 0;
static int s_rec_target_samples = 0;
static volatile bool s_is_recording = false;
static volatile bool s_is_playing = false;

bool start_recording(int seconds) {
  if (s_is_recording || s_is_playing)
    return false;
  if (!s_rec_buf) {
    s_rec_buf = (int16_t *)heap_caps_malloc(REC_MAX_SAMPLES * sizeof(int16_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rec_buf) {
      s_rec_buf = (int16_t *)malloc(REC_MAX_SAMPLES * sizeof(int16_t));
    }
  }
  if (!s_rec_buf) {
    ESP_LOGE(TAG, "Khong du RAM/PSRAM de ghi am!");
    return false;
  }
  if (seconds <= 0 || seconds > REC_MAX_SECONDS) {
    seconds = REC_MAX_SECONDS;
  }
  s_rec_target_samples = seconds * REC_SAMPLE_RATE;
  s_rec_sample_count = 0;
  s_is_recording = true;
  ESP_LOGI(TAG, "Bat dau ghi am %d giay...", seconds);
  return true;
}

bool is_recording(void) {
  return s_is_recording;
}

int get_recorded_samples(const int16_t **out_buf) {
  if (out_buf) {
    *out_buf = s_rec_buf;
  }
  return s_rec_sample_count;
}

static void speaker_playback_task(void *pv) {
  s_is_playing = true;
  if (s_rec_buf && s_rec_sample_count > 0) {
    ESP_LOGI(TAG, "Bat dau phat am thanh qua loa PAM8403 (So mau: %d)...",
             s_rec_sample_count);
    play_tone(1500, 50); // Beep báo hiệu bắt đầu phát
    vTaskDelay(pdMS_TO_TICKS(50));
    play_pcm_samples(s_rec_buf, s_rec_sample_count, REC_SAMPLE_RATE);
    play_tone(1000, 50); // Beep báo hiệu phát xong
    ESP_LOGI(TAG, "Phat am thanh hoan tat!");
  } else {
    ESP_LOGW(TAG, "Chua co ban ghi am de phat!");
    play_tone(400, 150); // Tiếng tít báo lỗi chưa có bản ghi
  }
  s_is_playing = false;
  vTaskDelete(NULL);
}

void play_recording_speaker_async(void) {
  if (s_is_playing || s_is_recording)
    return;
  xTaskCreate(speaker_playback_task, "spk_playback", 4096, NULL, 5, NULL);
}

static i2s_chan_handle_t rx_handle = NULL;

static void init_i2s_mic(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

  // BẮT BUỘC: INMP441 yêu cầu đúng 64 chu kỳ SCK clock trong mỗi khung WS (32 clock Left + 32 clock Right)
  // Do đó tầng cứng PHẢI chạy STEREO thì chip mic MEMS mới chịu khởi động!
  i2s_std_slot_config_t slot_cfg =
      I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = slot_cfg,
      .gpio_cfg =
          {
              .mclk = I2S_GPIO_UNUSED,
              .bclk = PIN_MIC_SCK,
              .ws = PIN_MIC_WS,
              .dout = I2S_GPIO_UNUSED,
              .din = PIN_MIC_SD,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
  ESP_LOGI(TAG, "Khoi tao Mic I2S STEREO (64 clock) thanh cong tren SCK:%d, WS:%d, SD:%d!",
           PIN_MIC_SCK, PIN_MIC_WS, PIN_MIC_SD);
}

void mic_touch_task(void *pvParameters) {
  QueueHandle_t emoji_queue = (QueueHandle_t)pvParameters;

  // Cấu hình chân Touch TTP223
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << PIN_TOUCH_SIG);
  io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);

  init_i2s_mic();

  int32_t i2s_buf[128];
  size_t bytes_read = 0;
  int last_touch_state = 0;
  int touch_mode_cycle = 1;
  int loud_cooldown = 0;

  while (1) {
    // 1. Đọc nút chạm TTP223 (Phát hiện sườn lên)
    int touch_state = gpio_get_level(PIN_TOUCH_SIG);
    if (touch_state == 1 && last_touch_state == 0) {
      touch_mode_cycle++;
      if (touch_mode_cycle > EMOJI_LOVE) {
        touch_mode_cycle = EMOJI_NORMAL;
      }
      emoji_mode_t next_mode = (emoji_mode_t)touch_mode_cycle;
      xQueueSend(emoji_queue, &next_mode, 0);
      play_touch_beep();
      ESP_LOGI(TAG, "Đã chạm Touch! Đổi sang mode: %d", next_mode);
    }
    last_touch_state = touch_state;

    // 2. Đọc âm thanh từ INMP441 qua bus I2S Stereo (64 clock / frame)
    esp_err_t read_err = i2s_channel_read(
        rx_handle, i2s_buf, sizeof(i2s_buf), &bytes_read, pdMS_TO_TICKS(100));

    if (read_err != ESP_OK) {
      static int err_tick = 0;
      if (++err_tick % 20 == 0) {
        ESP_LOGE(TAG, "Lỗi đọc I2S: %s", esp_err_to_name(read_err));
      }
    } else {
      int total_samples = bytes_read / sizeof(int32_t);
      int frames = total_samples / 2; // Mỗi frame gồm [Left, Right]

      if (frames > 0) {
        int64_t sum_amp = 0;

        for (int i = 0; i < frames; i++) {
          int32_t raw_l = i2s_buf[i * 2];     // Kênh Left (khi L/R nối GND)
          int32_t raw_r = i2s_buf[i * 2 + 1]; // Kênh Right (khi L/R nối VDD)

          // Lấy kênh có biên độ lớn hơn (hoặc kênh Left nếu chỉ cắm 1 mic)
          int32_t raw = (abs(raw_l) > abs(raw_r)) ? raw_l : raw_r;

          // INMP441 xuất dữ liệu 24-bit MSB, dịch 14 bit để về 16-bit PCM
          int32_t s = raw >> 14;

          if (s > 32767)
            s = 32767;
          if (s < -32768)
            s = -32768;

          sum_amp += abs(s);

          // 2.1. Nếu đang ghi âm: Lưu mẫu Mono chuẩn 16-bit
          if (s_is_recording && s_rec_buf != NULL) {
            if (s_rec_sample_count < s_rec_target_samples) {
              s_rec_buf[s_rec_sample_count] = (int16_t)s;
              s_rec_sample_count++;
            } else {
              s_is_recording = false;
              ESP_LOGI(TAG, "Ghi am Mono hoan tat! So mau: %d",
                       s_rec_sample_count);
            }
          }
        }

        int32_t avg_amp = sum_amp / frames;

        // In chẩn đoán phần cứng mỗi ~0.5 giây: hiển thị rõ ràng cả Left và Right
        static int diag_tick = 0;
        if (++diag_tick % 15 == 0) {
          ESP_LOGI(TAG,
                   "I2S RAW -> Left: %ld | Right: %ld | Amp: %ld | Vol: %d%%",
                   (long)i2s_buf[0], (long)i2s_buf[1], (long)avg_amp,
                   (int)g_mic_volume_percent);
        }

        // 2.2. Quy đổi sang % âm lượng và độ mở miệng
        int vol = (avg_amp - 200) / 100;
        if (vol < 0)
          vol = 0;
        if (vol > 100)
          vol = 100;
        g_mic_volume_percent = vol;
        g_mic_mouth_open = (vol * 35) / 100;

        // Nếu hét to (> 85%), robot giật mình
        if (vol > 85 && loud_cooldown == 0) {
          emoji_mode_t angry = EMOJI_ANGRY;
          xQueueSend(emoji_queue, &angry, 0);
          play_emoji_sound(EMOJI_ANGRY);
          loud_cooldown = 50;
        }
      }
    }

    if (loud_cooldown > 0)
      loud_cooldown--;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}