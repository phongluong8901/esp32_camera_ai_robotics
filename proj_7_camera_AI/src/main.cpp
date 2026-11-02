#include "esp_camera.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "img_converters.h"
#include "nvs_flash.h"
#include <algorithm>
#include <atomic>
#include <stdio.h>
#include <string.h>

// ESP-DL Face Detection
#include "dl_image_define.hpp"
#include "human_face_detect.hpp"

static const char *TAG = "ESP32S3_FACE_AI";

// ================= THAY WIFI CỦA BẠN =================
#define WIFI_SSID "Gosu"
#define WIFI_PASS "123456789"
// =====================================================

// Chân camera ESP32-S3 WROOM CAM (Freenove / WROOM-1)
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

// ================= STREAM BUFFER SYNCHRONIZATION =================
static uint8_t *s_latest_jpg_buf = NULL;
static size_t s_latest_jpg_len = 0;
static SemaphoreHandle_t s_jpg_mutex = NULL;
static SemaphoreHandle_t s_jpg_ready_sem = NULL;
static std::atomic<int> s_client_count{0};

static void update_latest_jpeg(uint8_t *jpg_buf, size_t jpg_len) {
  if (xSemaphoreTake(s_jpg_mutex, pdMS_TO_TICKS(40)) == pdTRUE) {
    if (s_latest_jpg_buf) {
      free(s_latest_jpg_buf);
    }
    s_latest_jpg_buf = jpg_buf;
    s_latest_jpg_len = jpg_len;
    xSemaphoreGive(s_jpg_mutex);
    xSemaphoreGive(s_jpg_ready_sem);
  } else {
    free(jpg_buf);
  }
}

// ================= VẼ BOUNDING BOX TRỰC TIẾP LÊN RGB565 =================
static void draw_box_rgb565(uint8_t *buf, int width, int height,
                            int x1, int y1, int x2, int y2, uint16_t color) {
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= width) x2 = width - 1;
  if (y2 >= height) y2 = height - 1;

  uint16_t *pixels = (uint16_t *)buf;

  // Vẽ 2 đường viền (độ dày 2 pixel) cho cạnh trên/dưới
  for (int t = 0; t < 2; t++) {
    int ty = y1 + t;
    int by = y2 - t;
    if (ty < height) {
      for (int x = x1; x <= x2; x++) pixels[ty * width + x] = color;
    }
    if (by >= 0) {
      for (int x = x1; x <= x2; x++) pixels[by * width + x] = color;
    }
  }

  // Vẽ 2 đường viền (độ dày 2 pixel) cho cạnh trái/phải
  for (int t = 0; t < 2; t++) {
    int lx = x1 + t;
    int rx = x2 - t;
    if (lx < width) {
      for (int y = y1; y <= y2; y++) pixels[y * width + lx] = color;
    }
    if (rx >= 0) {
      for (int y = y1; y <= y2; y++) pixels[y * width + rx] = color;
    }
  }
}

static void draw_point_rgb565(uint8_t *buf, int width, int height,
                              int px, int py, uint16_t color) {
  if (px >= 1 && px < width - 1 && py >= 1 && py < height - 1) {
    uint16_t *pixels = (uint16_t *)buf;
    pixels[py * width + px] = color;
    pixels[(py - 1) * width + px] = color;
    pixels[(py + 1) * width + px] = color;
    pixels[py * width + (px - 1)] = color;
    pixels[py * width + (px + 1)] = color;
  }
}

// ================= CAMERA INIT =================
static esp_err_t init_camera(void) {
  camera_config_t config = {};
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;

  config.pin_d7 = CAM_PIN_D7;
  config.pin_d6 = CAM_PIN_D6;
  config.pin_d5 = CAM_PIN_D5;
  config.pin_d4 = CAM_PIN_D4;
  config.pin_d3 = CAM_PIN_D3;
  config.pin_d2 = CAM_PIN_D2;
  config.pin_d1 = CAM_PIN_D1;
  config.pin_d0 = CAM_PIN_D0;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_pclk = CAM_PIN_PCLK;

  config.xclk_freq_hz = 20000000;
  config.ledc_timer = LEDC_TIMER_0;
  config.ledc_channel = LEDC_CHANNEL_0;

  // Quan trọng: RGB565 cho AI và hiển thị
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA; // 320x240 (kích thước tối ưu cho AI trên ESP32-S3)
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
    return err;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    ESP_LOGI(TAG, "Camera sensor PID: 0x%02x", s->id.PID);
    // Lưu ý: Nếu xem trên stream bị ngược đầu, đổi set_vflip thành 0
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
  }

  // Chờ cảm biến ổn định
  for (int i = 0; i < 5; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  return ESP_OK;
}

// ================= FACE DETECTION TASK (AI CHẠY EDGE TRÊN S3) =================
static void face_detection_task(void *arg) {
  ESP_LOGI(TAG, "Initializing Face Detection model...");

  // Khởi tạo model phát hiện khuôn mặt HumanFaceDetect
  HumanFaceDetect *detect = new HumanFaceDetect();
  // Giảm nhẹ ngưỡng tin cậy để dễ bắt mặt hơn (mặc định 0.5)
  detect->set_score_thr(0.35, 0);
  detect->set_score_thr(0.35, 1);
  ESP_LOGI(TAG, "Face Detection model initialized successfully (score_thr=0.35)!");

  uint32_t frame_count = 0;
  uint32_t detected_count = 0;

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(pdMS_TO_TICKS(15));
      continue;
    }

    dl::image::img_t img;
    img.data = fb->buf;
    img.width = fb->width;
    img.height = fb->height;
    // QUAN TRỌNG: esp_camera xuất RGB565 dạng Big-Endian (MSB trước)
    img.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;

    // Chạy model suy luận trực tiếp trên ESP32-S3
    auto &results = detect->run(img);

    frame_count++;
    if (!results.empty()) {
      detected_count++;
      for (auto &res : results) {
        ESP_LOGI(TAG, ">>> FACE DETECTED: score=%.2f box=[%d,%d,%d,%d]",
                 res.score, res.box[0], res.box[1], res.box[2], res.box[3]);

        // Vẽ hộp màu xanh lá cây (RGB565: 0x07E0) lên ảnh
        draw_box_rgb565(fb->buf, fb->width, fb->height,
                        res.box[0], res.box[1], res.box[2], res.box[3], 0x07E0);

        // Vẽ các điểm mốc (mắt, mũi) nếu có
        if (res.keypoint.size() >= 10) {
          // Mắt trái: red (0xF800), Mắt phải: red, Mũi: yellow (0xFFE0)
          draw_point_rgb565(fb->buf, fb->width, fb->height, res.keypoint[0], res.keypoint[1], 0xF800);
          draw_point_rgb565(fb->buf, fb->width, fb->height, res.keypoint[6], res.keypoint[7], 0xF800);
          draw_point_rgb565(fb->buf, fb->width, fb->height, res.keypoint[4], res.keypoint[5], 0xFFE0);
        }
      }
    } else {
      if (frame_count % 30 == 0) {
        ESP_LOGI(TAG, "AI scanning... (Frame %u: Chua thay mat. Hay dat mat thang dung cach camera 30-60cm, du sang)", (unsigned)frame_count);
      }
    }

    // Nếu có người đang xem stream qua WiFi, chuyển đổi RGB565 -> JPEG
    if (s_client_count > 0) {
      uint8_t *jpg_buf = NULL;
      size_t jpg_len = 0;
      bool ok = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565,
                        80, &jpg_buf, &jpg_len);
      if (ok && jpg_buf) {
        update_latest_jpeg(jpg_buf, jpg_len);
      }
    }

    esp_camera_fb_return(fb);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================= HTTP STREAM HANDLER =================
static esp_err_t stream_handler(httpd_req_t *req) {
  s_client_count++;
  esp_err_t res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  if (res != ESP_OK) {
    s_client_count--;
    return res;
  }

  static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
  static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
  char part_buf[128];

  while (true) {
    // Đợi frame JPEG mới nhất được AI task xử lý xong
    if (xSemaphoreTake(s_jpg_ready_sem, pdMS_TO_TICKS(400)) != pdTRUE) {
      continue;
    }

    uint8_t *send_buf = NULL;
    size_t send_len = 0;

    if (xSemaphoreTake(s_jpg_mutex, pdMS_TO_TICKS(40)) == pdTRUE) {
      if (s_latest_jpg_buf && s_latest_jpg_len > 0) {
        send_buf = (uint8_t *)malloc(s_latest_jpg_len);
        if (send_buf) {
          memcpy(send_buf, s_latest_jpg_buf, s_latest_jpg_len);
          send_len = s_latest_jpg_len;
        }
      }
      xSemaphoreGive(s_jpg_mutex);
    }

    if (!send_buf || send_len == 0) {
      if (send_buf) free(send_buf);
      continue;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, (unsigned)send_len);
      res = httpd_resp_send_chunk(req, part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)send_buf, send_len);
    }

    free(send_buf);

    if (res != ESP_OK) {
      break;
    }
  }

  s_client_count--;
  return res;
}

static esp_err_t jpg_handler(httpd_req_t *req) {
  uint8_t *send_buf = NULL;
  size_t send_len = 0;

  if (xSemaphoreTake(s_jpg_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (s_latest_jpg_buf && s_latest_jpg_len > 0) {
      send_buf = (uint8_t *)malloc(s_latest_jpg_len);
      if (send_buf) {
        memcpy(send_buf, s_latest_jpg_buf, s_latest_jpg_len);
        send_len = s_latest_jpg_len;
      }
    }
    xSemaphoreGive(s_jpg_mutex);
  }

  if (!send_buf) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  esp_err_t res = httpd_resp_send(req, (const char *)send_buf, send_len);
  free(send_buf);
  return res;
}

static void start_webserver(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &stream_uri);

    httpd_uri_t jpg_uri = {
        .uri = "/jpg",
        .method = HTTP_GET,
        .handler = jpg_handler,
        .user_ctx = NULL};
    httpd_register_uri_handler(server, &jpg_uri);

    ESP_LOGI(TAG, "HTTP Server started successfully!");
  }
}

// ================= WIFI =================
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "======================================================");
    ESP_LOGI(TAG, "Live AI Stream: http://" IPSTR "/stream",
             IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Capture snapshot: http://" IPSTR "/jpg",
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

  wifi_config_t wifi_config = {};
  strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
  strcpy((char *)wifi_config.sta.password, WIFI_PASS);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

// ================= APP MAIN =================
extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "PSRAM size: %zu bytes", esp_psram_get_size());

  if (init_camera() != ESP_OK) {
    ESP_LOGE(TAG, "Camera init failed, halt");
    return;
  }

  // Khởi tạo các Semaphore đồng bộ hình ảnh giữa AI và Webserver
  s_jpg_mutex = xSemaphoreCreateMutex();
  s_jpg_ready_sem = xSemaphoreCreateBinary();

  // Chạy Face Detection trên core 0 với Stack 32KB cho suy luận mạng neural
  xTaskCreatePinnedToCore(face_detection_task, "face_det", 32768, NULL, 5, NULL, 0);

  // Khởi động kết nối WiFi
  init_wifi();
}
