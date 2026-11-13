#pragma once
#include "emoji_types.h"

// Task chạy Wi-Fi SoftAP và HTTP Server (nhận QueueHandle_t qua pvParameters)
void web_server_task(void *pvParameters);