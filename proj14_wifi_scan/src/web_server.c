#include "csi_sensor.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "web_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "WS_SERVER";

#define CSI_UDP_PORT 5555

// Con tro tro toi file index.html duoc nhung trong Flash
extern const uint8_t index_html_start[] asm("_binary_index_html_start");

static httpd_handle_t server = NULL;
static volatile uint32_t ws_tx_ok_cnt = 0;
static volatile uint32_t ws_tx_err_cnt = 0;
static volatile uint32_t http_tx_cnt = 0;
static volatile uint32_t udp_tx_cnt = 0;

static portMUX_TYPE ai_mux = portMUX_INITIALIZER_UNLOCKED;
static char ai_label[24] = "";
static uint8_t ai_conf = 0;
static int64_t ai_ts_us = 0;

static int build_metrics_json(char *buf, size_t buf_size) {
  csi_metrics_t m;
  csi_sensor_get_metrics(&m);

  char act_copy[24];
  uint8_t conf_copy = 0;
  int64_t ts_copy = 0;

  taskENTER_CRITICAL(&ai_mux);
  memcpy(act_copy, ai_label, sizeof(act_copy));
  conf_copy = ai_conf;
  ts_copy = ai_ts_us;
  taskEXIT_CRITICAL(&ai_mux);
  act_copy[sizeof(act_copy) - 1] = '\0';

  if (ts_copy == 0 || (esp_timer_get_time() - ts_copy) > 3000000) {
    act_copy[0] = '\0';
    conf_copy = 0;
  }

  int len = snprintf(
      buf, buf_size,
      "{\"rssi\":%d,\"var\":%.2f,\"motion\":%.2f,\"pkts\":%lu,"
      "\"pps\":%u,\"cal\":%d,\"nf\":%.4f,\"raw\":%lu,"
      "\"act\":\"%s\",\"conf\":%u,\"subs\":[",
      (int)m.rssi, (double)m.variance, (double)m.motion,
      (unsigned long)m.packet_count, (unsigned)m.pps, m.calibrating ? 1 : 0,
      (double)m.noise_floor, (unsigned long)m.raw_cb_count, act_copy,
      (unsigned)conf_copy);

  if (len < 0 || (size_t)len >= buf_size) {
    return 0;
  }

  for (int i = 0; i < CSI_SUB_BINS; i++) {
    size_t rem = buf_size - (size_t)len;
    if (rem <= 4) {
      return 0;
    }
    int n = snprintf(buf + len, rem, "%u%s", (unsigned)m.subs[i],
                     (i < CSI_SUB_BINS - 1) ? "," : "]}");
    if (n < 0 || (size_t)n >= rem) {
      return 0;
    }
    len += n;
  }
  return len;
}

// Phuc vu trang chu giao dien 3D (tat cache trinh duyet)
static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control",
                     "no-store, no-cache, must-revalidate");
  httpd_resp_send(req, (const char *)index_html_start, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// Endpoint HTTP JSON du phong neu WebSocket bi chan/gian doan
static esp_err_t api_metrics_handler(httpd_req_t *req) {
  char tx_buf[512];
  int tx_len = build_metrics_json(tx_buf, sizeof(tx_buf));
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (tx_len > 0) {
    httpd_resp_send(req, tx_buf, tx_len);
    http_tx_cnt++;
  } else {
    httpd_resp_send(req, "{}", 2);
  }
  return ESP_OK;
}

// Xu ly WebSocket theo mo hinh request-reply truc tiep tren thread httpd
static esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "Client WebSocket da ket noi (fd=%d)",
             httpd_req_to_sockfd(req));
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;

  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    return ret;
  }

  if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
    return ESP_OK;
  }

  if (ws_pkt.len > 0) {
    uint8_t buf[64];
    if (ws_pkt.len >= sizeof(buf)) {
      return ESP_ERR_INVALID_SIZE;
    }
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      return ret;
    }
    buf[ws_pkt.len] = '\0';

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
      if (strncmp((const char *)buf, "cal", 3) == 0) {
        ESP_LOGI(TAG, "Nhan lenh hieu chuan nen tinh tu Web UI");
        csi_sensor_calibrate();
      }

      char tx_buf[512];
      int tx_len = build_metrics_json(tx_buf, sizeof(tx_buf));
      if (tx_len > 0) {
        httpd_ws_frame_t resp_pkt = {
            .final = true,
            .fragmented = false,
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)tx_buf,
            .len = (size_t)tx_len,
        };
        ret = httpd_ws_send_frame(req, &resp_pkt);
        if (ret == ESP_OK) {
          ws_tx_ok_cnt++;
        } else {
          ws_tx_err_cnt++;
          ESP_LOGW(TAG, "httpd_ws_send_frame failed: %s",
                   esp_err_to_name(ret));
          return ret;
        }
      }
    }
  }
  return ESP_OK;
}

// Task UDP port 5555: gui 62 subcarrier ve PC & nhan nhan AI "ACT:<label>:<conf>"
static void csi_udp_task(void *arg) {
  (void)arg;
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Khong tao duoc UDP socket");
    vTaskDelete(NULL);
    return;
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in bind_addr = {0};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_port = htons(CSI_UDP_PORT);
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
    ESP_LOGE(TAG, "Khong bind duoc UDP port %d", CSI_UDP_PORT);
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "UDP CSI Streamer lang nghe tai port %d", CSI_UDP_PORT);

  bool has_sub = false;
  struct sockaddr_in sub_addr = {0};
  int64_t last_sub_us = 0;

  while (1) {
    for (int i = 0; i < 8; i++) {
      char rx_buf[64];
      struct sockaddr_in from = {0};
      socklen_t from_len = sizeof(from);
      ssize_t n = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, MSG_DONTWAIT,
                           (struct sockaddr *)&from, &from_len);
      if (n <= 0) {
        break;
      }
      rx_buf[n] = '\0';

      if (strncmp(rx_buf, "SUB", 3) == 0) {
        bool was_sub = has_sub;
        sub_addr = from;
        last_sub_us = esp_timer_get_time();
        has_sub = true;
        if (!was_sub) {
          csi_sensor_set_stream_enabled(true);
          char ip_str[INET_ADDRSTRLEN] = {0};
          inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
          ESP_LOGI(TAG, "UDP client dang ky stream: %s:%u", ip_str,
                   (unsigned)ntohs(from.sin_port));
        }
      } else if (strncmp(rx_buf, "UNSUB", 5) == 0) {
        if (has_sub) {
          has_sub = false;
          csi_sensor_set_stream_enabled(false);
          ESP_LOGI(TAG, "UDP client da ngung stream");
        }
      } else if (strncmp(rx_buf, "ACT:", 4) == 0) {
        const char *p = rx_buf + 4;
        char clean_lbl[24] = {0};
        size_t li = 0;
        while (*p != '\0' && *p != ':' && *p != '\r' && *p != '\n') {
          char c = *p++;
          if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-') {
            if (li < 20) {
              clean_lbl[li++] = c;
            }
          }
        }
        clean_lbl[li] = '\0';
        int conf_val = (*p == ':') ? atoi(p + 1) : 100;
        if (conf_val < 0) {
          conf_val = 0;
        }
        if (conf_val > 100) {
          conf_val = 100;
        }
        if (li > 0) {
          taskENTER_CRITICAL(&ai_mux);
          memcpy(ai_label, clean_lbl, sizeof(ai_label));
          ai_conf = (uint8_t)conf_val;
          ai_ts_us = esp_timer_get_time();
          taskEXIT_CRITICAL(&ai_mux);
        }
      }
    }

    if (has_sub && (esp_timer_get_time() - last_sub_us > 15000000)) {
      has_sub = false;
      csi_sensor_set_stream_enabled(false);
      ESP_LOGW(TAG, "UDP client timeout (15s), dung stream");
    }

    if (has_sub) {
      csi_frame_t frame;
      if (csi_sensor_pop_frame(&frame, 20)) {
        int sent_burst = 0;
        do {
          if (sendto(sock, &frame, sizeof(frame), 0,
                     (struct sockaddr *)&sub_addr, sizeof(sub_addr)) > 0) {
            udp_tx_cnt++;
          }
          sent_burst++;
        } while (sent_burst < 8 && csi_sensor_pop_frame(&frame, 0));
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(40));
    }
  }
}

// Task log thong so CSI & bo dem gui dinh ky 2s len Serial Monitor
static void csi_log_task(void *arg) {
  (void)arg;
  csi_metrics_t m;

  while (1) {
    csi_sensor_get_metrics(&m);
    ESP_LOGI(TAG,
             "CSI rssi=%d var=%.2f nf=%.4f pps=%u pkts=%lu raw=%lu len=%u "
             "cal=%d ws_tx=%lu ws_err=%lu http_tx=%lu udp_tx=%lu",
             (int)m.rssi, (double)m.variance, (double)m.noise_floor,
             (unsigned)m.pps, (unsigned long)m.packet_count,
             (unsigned long)m.raw_cb_count, (unsigned)m.last_len,
             m.calibrating ? 1 : 0, (unsigned long)ws_tx_ok_cnt,
             (unsigned long)ws_tx_err_cnt, (unsigned long)http_tx_cnt,
             (unsigned long)udp_tx_cnt);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void ws_server_start(void) {
  if (server != NULL) {
    return;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_open_sockets = 7;
  config.lru_purge_enable = true;
  config.stack_size = 6144;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t api_uri = {.uri = "/api/metrics",
                           .method = HTTP_GET,
                           .handler = api_metrics_handler};
    httpd_uri_t ws_uri = {.uri = "/ws",
                          .method = HTTP_GET,
                          .handler = ws_handler,
                          .is_websocket = true};

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &api_uri);
    httpd_register_uri_handler(server, &ws_uri);

    xTaskCreate(csi_log_task, "csi_log", 3072, NULL, 4, NULL);
    xTaskCreate(csi_udp_task, "csi_udp", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Web & WebSocket Server da san sang!");
  }
}
