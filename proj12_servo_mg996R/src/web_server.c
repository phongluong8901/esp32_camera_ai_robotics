#include "web_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "servo_ctrl.h"
#include <stdlib.h>


static const char *TAG = "WEB_SERVER";
static httpd_handle_t server = NULL;

static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head><meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:sans-serif;text-align:center;background:#121212;color:#"
    "fff;padding:20px;}"
    ".card{background:#1e1e1e;padding:20px;border-radius:12px;max-width:360px;"
    "margin:15px auto;}"
    "input[type=range]{width:100%;height:30px;}"
    "button{padding:10px "
    "16px;margin:5px;border:none;border-radius:6px;background:#2196F3;color:#"
    "fff;font-size:15px;cursor:pointer;}"
    "button:active{background:#1976D2;}"
    "</style></head><body>"
    "<h2>ESP32-S3 Servo Control</h2>"
    "<div class='card'><h3>Servo 1 (GPIO 15): <span id='v1'>90</span>&deg;</h3>"
    "<input type='range' id='s1' min='0' max='180' value='90' "
    "oninput='set(1,this.value)'>"
    "<div><button onclick='btn(1,0)'>0&deg;</button><button "
    "onclick='btn(1,90)'>90&deg;</button><button "
    "onclick='btn(1,180)'>180&deg;</button></div></div>"
    "<div class='card'><h3>Servo 2 (GPIO 16): <span id='v2'>90</span>&deg;</h3>"
    "<input type='range' id='s2' min='0' max='180' value='90' "
    "oninput='set(2,this.value)'>"
    "<div><button onclick='btn(2,0)'>0&deg;</button><button "
    "onclick='btn(2,90)'>90&deg;</button><button "
    "onclick='btn(2,180)'>180&deg;</button></div></div>"
    "<script>"
    "let t={};"
    "function set(s,a){"
    "  document.getElementById('v'+s).innerText=a;"
    "  clearTimeout(t[s]);"
    "  t[s]=setTimeout(()=>{fetch(`/set?s=${s}&a=${a}`);},30);"
    "}"
    "function btn(s,a){document.getElementById('s'+s).value=a;set(s,a);}"
    "</script></body></html>";

static esp_err_t root_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t set_get_handler(httpd_req_t *req) {
  char query[64];
  char s_val[8], a_val[8];

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "s", s_val, sizeof(s_val)) == ESP_OK &&
        httpd_query_key_value(query, "a", a_val, sizeof(a_val)) == ESP_OK) {
      servo_ctrl_set_angle(atoi(s_val), atoi(a_val));
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
    httpd_uri_t set_uri = {
        .uri = "/set", .method = HTTP_GET, .handler = set_get_handler};

    httpd_register_uri_handler(server, &root_uri);
    httpd_register_uri_handler(server, &set_uri);
    ESP_LOGI(TAG, "Web Server da khoi dong!");
  }
}