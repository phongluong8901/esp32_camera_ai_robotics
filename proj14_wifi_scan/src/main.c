#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/ip_addr.h"
#include "nvs_flash.h"
#include "ping/ping_sock.h"
#include <string.h>

#include "csi_sensor.h"
#include "web_server.h"

static const char *TAG = "MAIN";

#define WIFI_SSID "Gosu"
#define WIFI_PASS "123456789"

static esp_ping_handle_t ping_handle = NULL;
static uint32_t ping_ok_cnt = 0;
static uint32_t ping_to_cnt = 0;

static void on_ping_success(esp_ping_handle_t hdl, void *args) {
  (void)hdl;
  (void)args;
  ping_ok_cnt++;
  if (ping_ok_cnt == 1) {
    ESP_LOGI(TAG, "Router da phan hoi ICMP ping (luong CSI ~40Hz hoat dong)");
  }
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args) {
  (void)hdl;
  (void)args;
  ping_to_cnt++;
  if (ping_to_cnt == 5 && ping_ok_cnt == 0) {
    ESP_LOGW(
        TAG,
        "Router khong phan hoi ICMP ping - se dung luong WebSocket du phong");
  }
}

static void stop_gateway_ping(void) {
  if (ping_handle != NULL) {
    esp_ping_stop(ping_handle);
    esp_ping_delete_session(ping_handle);
    ping_handle = NULL;
  }
}

static void start_gateway_ping(uint32_t gw_ip_u32) {
  stop_gateway_ping();
  if (gw_ip_u32 == 0) {
    return;
  }

  ping_ok_cnt = 0;
  ping_to_cnt = 0;

  ip_addr_t target_addr;
  memset(&target_addr, 0, sizeof(target_addr));
  ip_addr_set_ip4_u32_val(target_addr, gw_ip_u32);

  esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
  ping_config.target_addr = target_addr;
  ping_config.count = ESP_PING_COUNT_INFINITE;
  ping_config.interval_ms = 25; // ~40Hz tao luong CSI lien tuc tu Router tang 4
  ping_config.timeout_ms = 200;
  ping_config.data_size = 16;
  ping_config.task_stack_size = 3072;

  esp_ping_callbacks_t cbs = {
      .cb_args = NULL,
      .on_ping_success = on_ping_success,
      .on_ping_timeout = on_ping_timeout,
  };
  if (esp_ping_new_session(&ping_config, &cbs, &ping_handle) == ESP_OK) {
    esp_ping_start(ping_handle);
    ESP_LOGI(TAG, "Da bat ICMP ping tao luong CSI (~40Hz)");
  } else {
    ESP_LOGW(TAG, "Khong the khoi tao ping session");
  }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  (void)arg;
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGW(TAG, "Mat Wi-Fi, dang thu ket noi lai...");
    stop_gateway_ping();
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "Mo trinh duyet: http://" IPSTR, IP2STR(&event->ip_info.ip));
    ESP_LOGI(TAG, "Gateway (AP): " IPSTR, IP2STR(&event->ip_info.gw));
    ESP_LOGI(TAG, "=========================================");

    esp_wifi_set_ps(WIFI_PS_NONE);

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      csi_sensor_set_target_mac(ap_info.bssid);
    }

    csi_sensor_init();
    start_gateway_ping(event->ip_info.gw.addr);
    ws_server_start();
  }
}

static void wifi_init_sta(void) {
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
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
          },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_init_sta();
}
