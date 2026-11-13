#include "web_server.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mic_touch.h"
#include "nvs_flash.h"
#include "sound_fx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";

#define WIFI_SSID "Gosu"
#define WIFI_PASS "123456789"

static QueueHandle_t s_emoji_queue = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *disconn =
        (wifi_event_sta_disconnected_t *)event_data;
    ESP_LOGW(TAG, "Ngat ket noi Wi-Fi, ma loi (reason code): %d",
             disconn->reason);
    if (disconn->reason == 201) { // WIFI_REASON_NO_AP_FOUND
      ESP_LOGE(TAG,
               "-> KHONG TIM THAY Wi-Fi '%s'! Hay kiem tra xem router co phat "
               "song 2.4GHz khong (ESP32 khong bat duoc 5GHz) hoac co viet "
               "hoa/thuong.",
               WIFI_SSID);
    } else if (disconn->reason == 202 || disconn->reason == 15) {
      ESP_LOGE(TAG, "-> SAI MAT KHAU hoac loi xac thuc Handshake!");
    }
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "DA KET NOI THANH CONG VAO WIFI: %s", WIFI_SSID);
    ESP_LOGI(TAG, "TRUY CAP WEB BANG LINK: http://" IPSTR,
             IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "=========================================");
    play_touch_beep();
  }
}

static const char *HTML_PAGE = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Xiaozhi Full Hardware</title>
  <style>
    body { background: #0f172a; color: #f8fafc; font-family: system-ui, sans-serif; text-align: center; padding: 18px; margin: 0; }
    h1 { color: #38bdf8; margin-bottom: 4px; font-size: 22px; }
    .box { max-width: 400px; margin: 12px auto; background: #1e293b; padding: 12px; border-radius: 12px; border: 1px solid #334155; }
    .bar-bg { width: 100%; height: 14px; background: #0f172a; border-radius: 7px; overflow: hidden; margin-top: 6px; }
    .bar-fill { height: 100%; width: 0%; background: linear-gradient(90deg, #22c55e, #eab308, #ef4444); transition: width 0.15s; }
    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 12px; max-width: 400px; margin: 15px auto; }
    button { background: #1e293b; color: #fff; border: 2px solid #334155; border-radius: 14px; padding: 14px 8px; font-size: 15px; font-weight: 600; cursor: pointer; display: flex; flex-direction: column; align-items: center; gap: 6px; }
    button span { font-size: 28px; }
    button:active { border-color: #38bdf8; background: #0284c7; }
    .full { grid-column: span 2; background: #0369a1; border-color: #38bdf8; }
    .sound-btn { grid-column: span 2; background: #4f46e5; border-color: #818cf8; }
    .rec-box { max-width: 400px; margin: 15px auto; background: #1e1e38; padding: 14px; border-radius: 14px; border: 2px solid #6366f1; }
    .rec-btn { background: #dc2626; border-color: #ef4444; flex: 1; padding: 10px; font-size: 14px; }
    .play-btn { background: #2563eb; border-color: #3b82f6; flex: 1; padding: 10px; font-size: 14px; }
    #status { margin-top: 10px; color: #4ade80; font-weight: bold; }
  </style>
</head>
<body>
  <h1>🤖 Robot Xiaozhi (LCD + Mic + Touch + Speaker)</h1>

  <!-- 1. Thanh đo thời gian thực Mic INMP441 -->
  <div class="box">
    <div>🎙️ Cường độ Mic INMP441: <b id="micVal">0%</b></div>
    <div class="bar-bg"><div class="bar-fill" id="micBar"></div></div>
  </div>

  <!-- 2. Tính năng Ghi âm & Nghe lại (Test Mic + Loa) -->
  <div class="rec-box">
    <div style="font-weight: bold; color: #a5b4fc; font-size: 16px; margin-bottom: 6px;">🎙️ Thử Nghiệm Thu & Phát Âm Thanh</div>
    <div style="font-size: 13px; color: #94a3b8; margin-bottom: 12px;">Bấm Ghi âm (5s), nói vào Mic, sau đó nghe lại qua Loa Robot hoặc Điện thoại!</div>
    <div style="display: flex; gap: 10px; justify-content: center;">
      <button class="rec-btn" id="btnRec" onclick="startRecord()"><span>🔴</span>Ghi âm (5s)</button>
      <button class="play-btn" id="btnPlaySpk" onclick="playSpeaker()"><span>🔊</span>Phát ra Loa</button>
    </div>
    <div id="recStatus" style="margin-top: 10px; font-size: 14px; color: #38bdf8;">Chưa có bản ghi</div>
    <div style="margin-top: 10px;">
      <audio id="audioPlayer" controls style="width: 100%; height: 36px; display: none;"></audio>
    </div>
  </div>

  <!-- 3. Bảng điều khiển biểu cảm LCD -->
  <div class="grid">
    <button class="full" onclick="setMode(0, 'Tự động (Auto)')"><span>🔄</span>Chế độ Tự động</button>
    <button onclick="setMode(1, 'Bình thường')"><span>😐</span>Bình thường</button>
    <button onclick="setMode(2, 'Vui vẻ')"><span>😄</span>Vui vẻ</button>
    <button onclick="setMode(6, 'Đáng yêu')"><span>😍</span>Đáng yêu</button>
    <button onclick="setMode(3, 'Tức giận')"><span>😡</span>Tức giận</button>
    <button onclick="setMode(4, 'Buồn bã')"><span>😢</span>Buồn bã</button>
    <button onclick="setMode(5, 'Đi ngủ')"><span>😴</span>Đi ngủ</button>
    <button class="sound-btn" onclick="playBeep()"><span>🔊</span>Phát tiếng Beep Robot</button>
  </div>
  <div id="status">Sẵn sàng tương tác!</div>

  <script>
    function setMode(m, name) {
      fetch('/emoji?m=' + m).then(() => {
        document.getElementById('status').innerText = 'Đang chạy: ' + name;
      });
    }
    function playBeep() {
      fetch('/beep');
    }

    let recTimer = null;
    function startRecord() {
      const btn = document.getElementById('btnRec');
      const st = document.getElementById('recStatus');
      const player = document.getElementById('audioPlayer');
      btn.disabled = true;
      btn.style.opacity = '0.5';
      player.style.display = 'none';
      st.innerText = 'Đang chuẩn bị ghi âm...';

      fetch('/rec_start').then(res => {
        if (!res.ok) throw new Error();
        recTimer = setInterval(() => {
          fetch('/rec_status').then(r => r.json()).then(data => {
            if (data.recording) {
              const sec = (data.count / 16000).toFixed(1);
              st.innerText = '🔴 Đang ghi âm: ' + sec + 's / 5.0s (Hãy nói vào Mic!)';
            } else {
              clearInterval(recTimer);
              btn.disabled = false;
              btn.style.opacity = '1';
              st.innerText = '✅ Đã ghi âm xong 5 giây!';
              player.src = '/rec.wav?t=' + Date.now();
              player.style.display = 'block';
            }
          }).catch(() => {});
        }, 200);
      }).catch(() => {
        btn.disabled = false;
        btn.style.opacity = '1';
        st.innerText = '❌ Không thể bắt đầu ghi âm!';
      });
    }

    function playSpeaker() {
      const st = document.getElementById('recStatus');
      st.innerText = '🔊 Đang phát qua Loa Robot (PAM8403)...';
      fetch('/rec_play').then(() => {
        setTimeout(() => {
          st.innerText = '✅ Phát qua Loa xong!';
        }, 5500);
      });
    }

    setInterval(() => {
      fetch('/mic').then(r => r.text()).then(val => {
        document.getElementById('micVal').innerText = val + '%';
        document.getElementById('micBar').style.width = val + '%';
      }).catch(() => {});
    }, 250);
  </script>
</body>
</html>
)rawliteral";

#pragma pack(push, 1)
struct wav_header_t {
  char riff_header[4];
  uint32_t wav_size;
  char wave_header[4];
  char fmt_header[4];
  uint32_t fmt_chunk_size;
  uint16_t audio_format;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t sample_alignment;
  uint16_t bit_depth;
  char data_header[4];
  uint32_t data_bytes;
};
#pragma pack(pop)

static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t emoji_get_handler(httpd_req_t *req) {
  char buf[32];
  if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
    char param[8];
    if (httpd_query_key_value(buf, "m", param, sizeof(param)) == ESP_OK) {
      emoji_mode_t mode = (emoji_mode_t)atoi(param);
      if (s_emoji_queue != NULL) {
        xQueueSend(s_emoji_queue, &mode, 0);
      }
    }
  }
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t beep_get_handler(httpd_req_t *req) {
  play_touch_beep();
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t mic_get_handler(httpd_req_t *req) {
  char resp[16];
  snprintf(resp, sizeof(resp), "%d", g_mic_volume_percent);
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t rec_start_handler(httpd_req_t *req) {
  if (start_recording(5)) {
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  } else {
    httpd_resp_send_500(req);
  }
  return ESP_OK;
}

static esp_err_t rec_status_handler(httpd_req_t *req) {
  const int16_t *buf = NULL;
  int count = get_recorded_samples(&buf);
  char resp[64];
  snprintf(resp, sizeof(resp),
           "{\"recording\":%s,\"count\":%d,\"total\":80000}",
           is_recording() ? "true" : "false", count);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t rec_play_handler(httpd_req_t *req) {
  play_recording_speaker_async();
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t rec_wav_handler(httpd_req_t *req) {
  const int16_t *buf = NULL;
  int count = get_recorded_samples(&buf);
  if (!buf || count <= 0) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  uint32_t data_bytes = count * sizeof(int16_t);
  wav_header_t hdr = {
      {'R', 'I', 'F', 'F'},
      36 + data_bytes,
      {'W', 'A', 'V', 'E'},
      {'f', 'm', 't', ' '},
      16,
      1,
      1,                // Mono 1 kênh chuẩn
      16000,
      16000 * 1 * 2,    // 32000 bytes/sec
      2,                // Block align: 1 kênh * 2 bytes = 2
      16,
      {'d', 'a', 't', 'a'},
      data_bytes};

  httpd_resp_set_type(req, "audio/wav");
  httpd_resp_send_chunk(req, (const char *)&hdr, sizeof(hdr));

  const char *data_ptr = (const char *)buf;
  size_t remaining = data_bytes;
  while (remaining > 0) {
    size_t chunk_len = remaining > 2048 ? 2048 : remaining;
    if (httpd_resp_send_chunk(req, data_ptr, chunk_len) != ESP_OK) {
      return ESP_FAIL;
    }
    data_ptr += chunk_len;
    remaining -= chunk_len;
  }
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

void web_server_task(void *pvParameters) {
  s_emoji_queue = (QueueHandle_t)pvParameters;

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {};
  strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
  strcpy((char *)wifi_config.sta.password, WIFI_PASS);
  wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Dang ket noi vao Wi-Fi: %s ...", WIFI_SSID);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_uri_t emoji_uri = {.uri = "/emoji",
                             .method = HTTP_GET,
                             .handler = emoji_get_handler,
                             .user_ctx = NULL};
    httpd_uri_t beep_uri = {.uri = "/beep",
                            .method = HTTP_GET,
                            .handler = beep_get_handler,
                            .user_ctx = NULL};
    httpd_uri_t mic_uri = {.uri = "/mic",
                           .method = HTTP_GET,
                           .handler = mic_get_handler,
                           .user_ctx = NULL};
    httpd_uri_t rec_start_uri = {.uri = "/rec_start",
                                 .method = HTTP_GET,
                                 .handler = rec_start_handler,
                                 .user_ctx = NULL};
    httpd_uri_t rec_status_uri = {.uri = "/rec_status",
                                  .method = HTTP_GET,
                                  .handler = rec_status_handler,
                                  .user_ctx = NULL};
    httpd_uri_t rec_play_uri = {.uri = "/rec_play",
                                .method = HTTP_GET,
                                .handler = rec_play_handler,
                                .user_ctx = NULL};
    httpd_uri_t rec_wav_uri = {.uri = "/rec.wav",
                               .method = HTTP_GET,
                               .handler = rec_wav_handler,
                               .user_ctx = NULL};

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &emoji_uri);
    httpd_register_uri_handler(server, &beep_uri);
    httpd_register_uri_handler(server, &mic_uri);
    httpd_register_uri_handler(server, &rec_start_uri);
    httpd_register_uri_handler(server, &rec_status_uri);
    httpd_register_uri_handler(server, &rec_play_uri);
    httpd_register_uri_handler(server, &rec_wav_uri);
  }

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}