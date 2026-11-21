#include "csi_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/queue.h"
#include <math.h>
#include <string.h>

static const char *TAG = "CSI_SENSOR";

#define MAX_SUB 64
#define CAL_WARMUP_PKTS 15
#define CAL_TOTAL_PKTS 80
#define NF_MIN 0.004f
#define NF_MAX 0.08f

static portMUX_TYPE csi_mux = portMUX_INITIALIZER_UNLOCKED;
static csi_metrics_t current_metrics = {0};

static uint8_t target_mac[6] = {0};
static bool has_target_mac = false;
static bool mac_filter_bypassed = false;
static bool locked_256 = false;
static uint32_t mac_drop_cnt = 0;
static uint32_t raw_cb_cnt = 0;
static bool recalibrate_req = true;
static bool csi_initialized = false;

static float med_amp[MAX_SUB] = {0};
static float fast_amp[MAX_SUB] = {0};
static bool sub_init[MAX_SUB] = {0};
static bool is_calibrating = true;
static int cal_count = 0;
static float cal_dev_sum = 0.0f;
static int cal_dev_cnt = 0;
static int64_t cal_start_us = 0;
static float noise_floor = 0.015f;

static float ema_var = 0.0f;
static float ema_motion = 0.0f;
static int64_t prev_pkt_us = 0;

static int64_t pps_window_start_us = 0;
static uint16_t pps_counter = 0;
static uint16_t current_pps = 0;
static int64_t last_pkt_us = 0;

static QueueHandle_t frame_queue = NULL;
static volatile bool udp_stream_en = false;
static uint16_t frame_seq = 0;

// Callback chay moi khi ESP32-S3 nhan goi tin Wi-Fi chua du lieu CSI
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *info) {
  (void)ctx;
  if (!info || !info->buf || info->len < 32) {
    return;
  }

  raw_cb_cnt++;

  uint8_t local_target[6];
  bool filter_mac = false;
  bool bypassed = false;
  bool do_recal = false;

  taskENTER_CRITICAL(&csi_mux);
  filter_mac = has_target_mac;
  bypassed = mac_filter_bypassed;
  if (filter_mac) {
    memcpy(local_target, target_mac, 6);
  }
  if (recalibrate_req) {
    do_recal = true;
    recalibrate_req = false;
  }
  taskEXIT_CRITICAL(&csi_mux);

  if (filter_mac && !bypassed && memcmp(info->mac, local_target, 6) != 0) {
    bool just_bypassed = false;
    taskENTER_CRITICAL(&csi_mux);
    mac_drop_cnt++;
    current_metrics.raw_cb_count = raw_cb_cnt;
    current_metrics.last_len = (uint16_t)info->len;
    if (current_metrics.packet_count == 0 && mac_drop_cnt >= 20) {
      mac_filter_bypassed = true;
      bypassed = true;
      just_bypassed = true;
    }
    taskEXIT_CRITICAL(&csi_mux);

    if (!bypassed) {
      return;
    }
    if (just_bypassed) {
      ESP_LOGW(TAG, "Tu dong tat bo loc BSSID do %lu goi khong khop MAC",
               (unsigned long)mac_drop_cnt);
    }
  }

  // Khoa luong HT20 (len >= 256) de tranh nhieu do tron goi legacy 128B va 256B
  bool drop_short = false;
  taskENTER_CRITICAL(&csi_mux);
  if (!locked_256 && info->len >= 256) {
    locked_256 = true;
    do_recal = true;
  } else if (locked_256 && info->len < 256) {
    drop_short = true;
    current_metrics.raw_cb_count = raw_cb_cnt;
  }
  taskEXIT_CRITICAL(&csi_mux);

  if (drop_short) {
    return;
  }

  int64_t now_us = esp_timer_get_time();

  if (do_recal) {
    memset(med_amp, 0, sizeof(med_amp));
    memset(fast_amp, 0, sizeof(fast_amp));
    memset(sub_init, 0, sizeof(sub_init));
    is_calibrating = true;
    cal_count = 0;
    cal_dev_sum = 0.0f;
    cal_dev_cnt = 0;
    cal_start_us = now_us;
    prev_pkt_us = 0;
    ema_var = 0.0f;
    ema_motion = 0.0f;
  }

  const int8_t *buf = (info->len >= 256) ? (info->buf + 128) : info->buf;
  int num_sub = (info->len >= 256) ? 64 : (info->len / 2);
  if (num_sub > MAX_SUB) {
    num_sub = MAX_SUB;
  }

  float raw_amp[MAX_SUB] = {0};
  bool is_active[MAX_SUB] = {0};
  float energy = 0.0f;
  int valid_cnt = 0;

  // Bo 2 subcarrier dau (DC va guard bin)
  for (int i = 2; i < num_sub; i++) {
    float imag = (float)buf[i * 2];
    float real = (float)buf[i * 2 + 1];
    float amp = sqrtf(real * real + imag * imag);
    raw_amp[i] = amp;
    if (amp > 0.5f) {
      is_active[i] = true;
      energy += amp * amp;
      valid_cnt++;
    }
  }

  if (valid_cnt < 10 || energy < 1.0f) {
    taskENTER_CRITICAL(&csi_mux);
    current_metrics.raw_cb_count = raw_cb_cnt;
    current_metrics.last_len = (uint16_t)info->len;
    taskEXIT_CRITICAL(&csi_mux);
    return;
  }

  // Chuan hoa theo nang luong RMS cua tung goi de triet nhieu AGC
  float rms = sqrtf(energy / (float)valid_cnt);
  float norm[MAX_SUB] = {0};
  for (int i = 2; i < num_sub; i++) {
    if (is_active[i]) {
      norm[i] = raw_amp[i] / rms;
    }
  }

  // Day goi 62 subcarrier tho vao queue de stream UDP cho PC train/inference
  if (udp_stream_en && frame_queue != NULL) {
    csi_frame_t frame = {0};
    frame.ts_ms = (uint32_t)(now_us / 1000LL);
    frame.rssi = info->rx_ctrl.rssi;
    frame.valid_cnt = (uint8_t)(valid_cnt > 255 ? 255 : valid_cnt);
    frame.seq = frame_seq++;
    for (int i = 2; i < num_sub && (i - 2) < CSI_RAW_SUBS; i++) {
      if (is_active[i]) {
        float scaled = norm[i] * 1000.0f;
        if (scaled < 0.0f) {
          scaled = 0.0f;
        }
        if (scaled > 65535.0f) {
          scaled = 65535.0f;
        }
        frame.amps[i - 2] = (uint16_t)scaled;
      }
    }
    (void)xQueueSend(frame_queue, &frame, 0);
  }

  if (pps_window_start_us == 0) {
    pps_window_start_us = now_us;
  }
  pps_counter++;
  int64_t pps_elapsed = now_us - pps_window_start_us;
  if (pps_elapsed >= 1000000) {
    current_pps = (uint16_t)((pps_counter * 1000000LL) / pps_elapsed);
    pps_counter = 0;
    pps_window_start_us = now_us;
  }

  float dt =
      (prev_pkt_us > 0) ? ((float)(now_us - prev_pkt_us) * 1e-6f) : 0.05f;
  if (dt < 0.005f) {
    dt = 0.005f;
  }
  if (dt > 0.5f) {
    dt = 0.5f;
  }
  prev_pkt_us = now_us;

  // Dual-EMA dong (0.12s vs 1.80s): nhay voi chuyen dong & tu tra ve ~1.0 sau 2-3s dung yen
  float alpha_fast = 1.0f - expf(-dt / 0.12f);
  float alpha_med = 1.0f - expf(-dt / 1.80f);

  if (is_calibrating) {
    if (cal_start_us == 0) {
      cal_start_us = now_us;
    }
    cal_count++;

    if (cal_count <= CAL_WARMUP_PKTS) {
      for (int i = 2; i < num_sub; i++) {
        if (is_active[i]) {
          if (!sub_init[i]) {
            med_amp[i] = norm[i];
            fast_amp[i] = norm[i];
            sub_init[i] = true;
          } else {
            float w = 1.0f / (float)cal_count;
            med_amp[i] += w * (norm[i] - med_amp[i]);
            fast_amp[i] = med_amp[i];
          }
        }
      }
    } else {
      float sq_diff = 0.0f;
      int active = 0;

      for (int i = 2; i < num_sub; i++) {
        if (is_active[i]) {
          if (!sub_init[i]) {
            med_amp[i] = norm[i];
            fast_amp[i] = norm[i];
            sub_init[i] = true;
          } else {
            fast_amp[i] += alpha_fast * (norm[i] - fast_amp[i]);
            med_amp[i] += alpha_med * (norm[i] - med_amp[i]);
            float d = fast_amp[i] - med_amp[i];
            sq_diff += d * d;
            active++;
          }
        }
      }

      if (active > 0) {
        float dev = sqrtf(sq_diff / (float)active);
        cal_dev_sum += dev;
        cal_dev_cnt++;
      }
    }

    if (cal_count >= CAL_TOTAL_PKTS ||
        (now_us - cal_start_us >= 2500000 &&
         cal_count >= CAL_WARMUP_PKTS + 10)) {
      float nf =
          (cal_dev_cnt > 0) ? (cal_dev_sum / (float)cal_dev_cnt) : 0.015f;
      if (nf < NF_MIN) {
        nf = NF_MIN;
      }
      if (nf > NF_MAX) {
        nf = NF_MAX;
      }
      noise_floor = nf;
      is_calibrating = false;
      ema_var = 1.0f;
      ema_motion = 0.0f;
      ESP_LOGI(TAG, "Hieu chuan CSI hoan tat (%d goi, noise_floor=%.4f)",
               cal_count, (double)noise_floor);
    }
  } else {
    float sq_diff = 0.0f;
    int active = 0;

    for (int i = 2; i < num_sub; i++) {
      if (is_active[i] && sub_init[i]) {
        fast_amp[i] += alpha_fast * (norm[i] - fast_amp[i]);
        float d = fast_amp[i] - med_amp[i];
        sq_diff += d * d;
        active++;
      }
    }

    if (active > 0) {
      float dev = sqrtf(sq_diff / (float)active);
      float nf = fmaxf(noise_floor, NF_MIN);
      float raw_score = dev / nf;
      if (raw_score < 0.0f) {
        raw_score = 0.0f;
      }
      if (raw_score > 20.0f) {
        raw_score = 20.0f;
      }

      for (int i = 2; i < num_sub; i++) {
        if (is_active[i]) {
          if (!sub_init[i]) {
            med_amp[i] = norm[i];
            fast_amp[i] = norm[i];
            sub_init[i] = true;
          } else {
            med_amp[i] += alpha_med * (norm[i] - med_amp[i]);
          }
        }
      }

      if (dev < noise_floor) {
        noise_floor += (1.0f - expf(-dt / 4.0f)) * (dev - noise_floor);
      } else if (raw_score < 1.35f) {
        noise_floor += (1.0f - expf(-dt / 30.0f)) * (dev - noise_floor);
      }
      if (noise_floor < NF_MIN) {
        noise_floor = NF_MIN;
      }
      if (noise_floor > NF_MAX) {
        noise_floor = NF_MAX;
      }

      float alpha_v = (raw_score > ema_var) ? (1.0f - expf(-dt / 0.08f))
                                            : (1.0f - expf(-dt / 0.90f));
      ema_var += alpha_v * (raw_score - ema_var);
      float excess = fmaxf(0.0f, ema_var - 1.0f);
      ema_motion += (1.0f - expf(-dt / 0.35f)) * (excess - ema_motion);
    }
  }

  // Nen cac song mang phu hoat dong thanh 16 cot hien thi
  float active_vals[MAX_SUB];
  int active_len = 0;
  for (int i = 2; i < num_sub; i++) {
    if (is_active[i]) {
      active_vals[active_len++] = sub_init[i] ? fast_amp[i] : norm[i];
    }
  }

  uint8_t local_subs[CSI_SUB_BINS] = {0};
  if (active_len > 0) {
    for (int b = 0; b < CSI_SUB_BINS; b++) {
      int start = (b * active_len) / CSI_SUB_BINS;
      int end = ((b + 1) * active_len) / CSI_SUB_BINS;
      if (end <= start) {
        end = start + 1;
      }
      if (end > active_len) {
        end = active_len;
      }

      float sum = 0.0f;
      for (int k = start; k < end; k++) {
        sum += active_vals[k];
      }
      float mean = sum / (float)(end - start);
      int scaled = (int)(mean * 45.0f);
      if (scaled < 0) {
        scaled = 0;
      }
      if (scaled > 100) {
        scaled = 100;
      }
      local_subs[b] = (uint8_t)scaled;
    }
  }

  taskENTER_CRITICAL(&csi_mux);
  last_pkt_us = now_us;
  current_metrics.rssi = info->rx_ctrl.rssi;
  current_metrics.variance = is_calibrating ? 0.0f : ema_var;
  current_metrics.motion = is_calibrating ? 0.0f : ema_motion;
  current_metrics.noise_floor = noise_floor;
  memcpy(current_metrics.subs, local_subs, CSI_SUB_BINS);
  current_metrics.packet_count++;
  current_metrics.raw_cb_count = raw_cb_cnt;
  current_metrics.pps = (current_pps > 0) ? current_pps : pps_counter;
  current_metrics.last_len = (uint16_t)info->len;
  current_metrics.calibrating = is_calibrating;
  current_metrics.mac_filter_active = (has_target_mac && !mac_filter_bypassed);
  taskEXIT_CRITICAL(&csi_mux);
}

void csi_sensor_set_target_mac(const uint8_t mac[6]) {
  if (!mac) {
    return;
  }
  taskENTER_CRITICAL(&csi_mux);
  memcpy(target_mac, mac, 6);
  has_target_mac = true;
  mac_filter_bypassed = false;
  locked_256 = false;
  mac_drop_cnt = 0;
  current_metrics.mac_filter_active = true;
  taskEXIT_CRITICAL(&csi_mux);
  ESP_LOGI(TAG, "Loc CSI theo AP BSSID: %02x:%02x:%02x:%02x:%02x:%02x", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void csi_sensor_calibrate(void) {
  taskENTER_CRITICAL(&csi_mux);
  recalibrate_req = true;
  current_metrics.calibrating = true;
  current_metrics.variance = 0.0f;
  current_metrics.motion = 0.0f;
  taskEXIT_CRITICAL(&csi_mux);
}

void csi_sensor_init(void) {
  if (csi_initialized) {
    return;
  }
  if (frame_queue == NULL) {
    frame_queue = xQueueCreate(16, sizeof(csi_frame_t));
  }
  csi_sensor_calibrate();

  // Cau hinh chuan csi_recv_router cho ESP32-S3
  wifi_csi_config_t csi_config = {
      .lltf_en = true,
      .htltf_en = true,
      .stbc_htltf2_en = false,
      .ltf_merge_en = true,
      .channel_filter_en = true,
      .manu_scale = false,
      .shift = false,
  };

  ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
  ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL));
  ESP_ERROR_CHECK(esp_wifi_set_csi(true));
  csi_initialized = true;
  ESP_LOGI(TAG, "Da kich hoat Wi-Fi CSI Sensing (Dual-EMA + HT20 Lock)");
}

void csi_sensor_set_stream_enabled(bool enabled) {
  udp_stream_en = enabled;
  if (!enabled && frame_queue != NULL) {
    xQueueReset(frame_queue);
  }
}

bool csi_sensor_pop_frame(csi_frame_t *out, uint32_t wait_ms) {
  if (!out || frame_queue == NULL) {
    return false;
  }
  return xQueueReceive(frame_queue, out, pdMS_TO_TICKS(wait_ms)) == pdTRUE;
}

void csi_sensor_get_metrics(csi_metrics_t *out) {
  int64_t lpu;
  taskENTER_CRITICAL(&csi_mux);
  *out = current_metrics;
  lpu = last_pkt_us;
  taskEXIT_CRITICAL(&csi_mux);

  if (lpu > 0 && (esp_timer_get_time() - lpu) > 2000000) {
    out->pps = 0;
    out->variance = 0.0f;
    out->motion = 0.0f;
  }
}
