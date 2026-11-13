#include "web_server.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WEB_SERVER";

#define WIFI_SSID "Gosu"
#define WIFI_PASS "123456789"

static QueueHandle_t s_emoji_queue = NULL;
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static const char *HTML_PAGE = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Xiaozhi Emoji Controller</title>
  <style>
    body { background: #0f172a; color: #f8fafc; font-family: system-ui, sans-serif; text-align: center; padding: 20px; margin: 0; }
    h1 { color: #38bdf8; margin-bottom: 5px; font-size: 24px; }
    p { color: #94a3b8; margin-top: 0; font-size: 14px; }
    .grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 14px; max-width: 420px; margin: 20px auto; }
    button { background: #1e293b; color: #fff; border: 2px solid #334155; border-radius: 16px; padding: 18px 10px; font-size: 16px; font-weight: 600; cursor: pointer; transition: 0.2s; display: flex; flex-direction: column; align-items: center; gap: 8px; }
    button span { font-size: 32px; }
    button:active { border-color: #38bdf8; background: #0284c7; transform: scale(0.97); }
    .full { grid-column: span 2; background: #0369a1; border-color: #38bdf8; }
    #status { margin-top: 15px; color: #4ade80; font-weight: bold; min-height: 24px; }
  </style>
</head>
<body>
  <h1>🤖 Robot Xiaozhi</h1>
  <p>Chọn biểu cảm hiển thị trên màn hình LCD</p>
  <div class="grid">
    <button class="full" onclick="setMode(0, 'Tự động (Auto)')"><span>🔄</span>Chế độ Tự động</button>
    <button onclick="setMode(1, 'Bình thường')"><span>😐</span>Bình thường</button>
    <button onclick="setMode(2, 'Vui vẻ')"><span>😄</span>Vui vẻ</button>
    <button onclick="setMode(6, 'Đáng yêu')"><span>😍</span>Đáng yêu</button>
    <button onclick="setMode(3, 'Tức giận')"><span>😡</span>Tức giận</button>
    <button onclick="setMode(4, 'Buồn bã')"><span>😢</span>Buồn bã</button>
    <button onclick="setMode(5, 'Đi ngủ')"><span>😴</span>Đi ngủ</button>
  </div>
  <div id="status">Đang chạy: Tự động</div>

  <script>
    function setMode(m, name) {
      document.getElementById('status').innerText = 'Đang gửi: ' + name + '...';
      fetch('/emoji?m=' + m)
        .then(r => r.text())
        .then(() => {
          document.getElementById('status').innerText = 'Đang chạy: ' + name;
        })
        .catch(() => {
          document.getElementById('status').innerText = 'Lỗi kết nối!';
        });
    }
  </script>
</body>
</html>
)rawliteral";

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Mat ket noi WiFi, dang thu ket noi lai...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "Da ket noi thanh cong Wi-Fi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "Dia chi IP cua ESP32: " IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Mo trinh duyet (cung mang Wi-Fi Gosu) vao:");
    ESP_LOGI(TAG, "http://" IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "==================================================");
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
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

  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {};
  strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, WIFI_PASS,
          sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Dang ket noi toi Wi-Fi: %s ...", WIFI_SSID);

  xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                      portMAX_DELAY);

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = root_get_handler,
                            .user_ctx = NULL};
    httpd_register_uri_handler(server, &root_uri);

    httpd_uri_t emoji_uri = {.uri = "/emoji",
                             .method = HTTP_GET,
                             .handler = emoji_get_handler,
                             .user_ctx = NULL};
    httpd_register_uri_handler(server, &emoji_uri);
    ESP_LOGI(TAG, "HTTP Server da san sang nhan lenh!");
  }

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}