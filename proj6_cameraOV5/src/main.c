#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "OV5640_WEB";

// ================= THAY ĐỔI WIFI CỦA BẠN TẠI ĐÂY =================
#define WIFI_SSID "Gosu"
#define WIFI_PASS "123456789"
// =================================================================

// Sơ đồ chân chuẩn ESP32-S3 WROOM CAM
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5

#define CAM_PIN_D7 16
#define CAM_PIN_D6 17
#define CAM_PIN_D5 18
#define CAM_PIN_D4 12
#define CAM_PIN_D3 10
#define CAM_PIN_D2 8
#define CAM_PIN_D1 9
#define CAM_PIN_D0 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13

// Cấu hình khởi tạo Camera đã tối ưu chống lỗi NO-EOI / Timeout cho OV5640
static esp_err_t init_camera(void) {
  camera_config_t camera_config = {.pin_pwdn = CAM_PIN_PWDN,
                                   .pin_reset = CAM_PIN_RESET,
                                   .pin_xclk = CAM_PIN_XCLK,
                                   .pin_sccb_sda = CAM_PIN_SIOD,
                                   .pin_sccb_scl = CAM_PIN_SIOC,

                                   .pin_d7 = CAM_PIN_D7,
                                   .pin_d6 = CAM_PIN_D6,
                                   .pin_d5 = CAM_PIN_D5,
                                   .pin_d4 = CAM_PIN_D4,
                                   .pin_d3 = CAM_PIN_D3,
                                   .pin_d2 = CAM_PIN_D2,
                                   .pin_d1 = CAM_PIN_D1,
                                   .pin_d0 = CAM_PIN_D0,
                                   .pin_vsync = CAM_PIN_VSYNC,
                                   .pin_href = CAM_PIN_HREF,
                                   .pin_pclk = CAM_PIN_PCLK,

                                   // QUAN TRỌNG: Hạ XCLK xuống 10MHz để tránh
                                   // lỗi NO-EOI / NO-SOI trên OV5640
                                   .xclk_freq_hz = 10000000,
                                   .ledc_timer = LEDC_TIMER_0,
                                   .ledc_channel = LEDC_CHANNEL_0,

                                   .pixel_format = PIXFORMAT_JPEG,
                                   // Dùng SVGA (800x600) hoặc HD (1280x720) để
                                   // stream mượt và không bị tràn DMA
                                   .frame_size = FRAMESIZE_SVGA,
                                   .jpeg_quality =
                                       12, // 12-15 là mức tối ưu cho OV5640
                                   .fb_count = 2,
                                   .fb_location = CAMERA_FB_IN_PSRAM,
                                   .grab_mode = CAMERA_GRAB_WHEN_EMPTY};

  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Khởi tạo Camera thất bại: 0x%x", err);
    return err;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV5640_PID) {
    ESP_LOGI(TAG, "Đã nhận diện cảm biến OV5640!");
    s->set_vflip(s, 1);
    // Bỏ qua birkaç khung hình đầu tiên để bộ giải mã JPEG của OV5640 ổn định
    for (int i = 0; i < 3; i++) {
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb)
        esp_camera_fb_return(fb);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
  return ESP_OK;
}

// Endpoint 1: Xem luồng video trực tiếp tại http://<IP>/stream
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[128];

  static const char *STREAM_CONTENT_TYPE =
      "multipart/x-mixed-replace;boundary=frame";
  static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
  static const char *STREAM_PART =
      "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
    return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      ESP_LOGW(TAG, "Bỏ qua khung hình lỗi...");
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    }

    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      break; // Người dùng đã đóng tab trình duyệt
    }
  }
  return res;
}

// Endpoint 2: Chụp 1 tấm ảnh tĩnh tại http://<IP>/jpg
static esp_err_t jpg_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static void start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t stream_uri = {.uri = "/stream",
                              .method = HTTP_GET,
                              .handler = stream_handler,
                              .user_ctx = NULL};
    httpd_register_uri_handler(server, &stream_uri);

    httpd_uri_t jpg_uri = {.uri = "/jpg",
                           .method = HTTP_GET,
                           .handler = jpg_handler,
                           .user_ctx = NULL};
    httpd_register_uri_handler(server, &jpg_uri);
    ESP_LOGI(TAG, "HTTP Server đã khởi chạy!");
  }
}

// Sự kiện kết nối Wi-Fi và in ra địa chỉ IP
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Mất kết nối Wi-Fi, đang thử kết nối lại...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "======================================================");
    ESP_LOGI(TAG,
             "Mở trình duyệt truy cập Video Stream: http://" IPSTR "/stream",
             IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Hoặc chụp ảnh tĩnh tại:              http://" IPSTR "/jpg",
             IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "======================================================");
    start_webserver();
  }
}

static void init_wifi(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Dung lượng PSRAM: %zu Bytes", esp_psram_get_size());

  if (init_camera() == ESP_OK) {
    init_wifi();
  }
}