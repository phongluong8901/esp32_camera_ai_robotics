#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "motor_ctrl.h"
#include <stdlib.h>


static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1,user-scalable=no'>"
    "<style>"
    "body{font-family:sans-serif;text-align:center;background:#121212;color:#"
    "fff;padding:20px;}"
    ".card{background:#1e1e1e;padding:20px;border-radius:12px;max-width:340px;"
    "margin:15px auto;}"
    "input[type=range]{width:100%;height:30px;}"
    ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-"
    "top:15px;}"
    "button{padding:18px "
    "0;border:none;border-radius:10px;background:#2196F3;color:#fff;font-size:"
    "16px;font-weight:bold;user-select:none;}"
    "button:active{background:#1565C0;}"
    ".stop{background:#e53935;}"
    ".stop:active{background:#b71c1c;}"
    "</style></head><body>"
    "<h2>ESP32-S3 N20 Control</h2>"
    "<div class='card'>"
    "<h3>Toc do: <span id='spdVal'>70</span>%</h3>"
    "<input type='range' id='spd' min='20' max='100' value='70' "
    "oninput='document.getElementById(\"spdVal\").innerText=this.value'>"
    "<div class='grid'>"
    "<div></div>"
    "<button onpointerdown='go(1,1)' onpointerup='stop()' "
    "onpointerleave='stop()'>TIEN</button>"
    "<div></div>"
    "<button onpointerdown='go(-1,1)' onpointerup='stop()' "
    "onpointerleave='stop()'>TRAI</button>"
    "<button class='stop' onclick='stop()'>DUNG</button>"
    "<button onpointerdown='go(1,-1)' onpointerup='stop()' "
    "onpointerleave='stop()'>PHAI</button>"
    "<div></div>"
    "<button onpointerdown='go(-1,-1)' onpointerup='stop()' "
    "onpointerleave='stop()'>LUI</button>"
    "<div></div>"
    "</div></div>"
    "<script>"
    "function send(l,r){fetch(`/motor?l=${l}&r=${r}`);}"
    "function go(dl,dr){let "
    "s=parseInt(document.getElementById('spd').value);send(dl*s,dr*s);}"
    "function stop(){send(0,0);}"
    "</script></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

// GET /motor?l=70&r=70
static esp_err_t motor_get_handler(httpd_req_t *req) {
  char query[64];
  char l_val[8], r_val[8];

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "l", l_val, sizeof(l_val)) == ESP_OK &&
        httpd_query_key_value(query, "r", r_val, sizeof(r_val)) == ESP_OK) {
      motor_ctrl_set(atoi(l_val), atoi(r_val));
    }
  }
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

void web_server_start(void) {
  if (server != NULL)
    return;
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t root_uri = {
        .uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t motor_uri = {
        .uri = "/motor", .method = HTTP_GET, .handler = motor_get_handler};

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &motor_uri);
    ESP_LOGI(TAG, "Web Server da khoi dong!");
  }
}