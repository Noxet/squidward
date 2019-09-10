#ifndef SQUIDWARD_ESP_UTILS_H
#define SQUIDWARD_ESP_UTILS_H

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD

extern EventGroupHandle_t wifi_event_group;
extern const int CONNECTED_BIT;
extern const char *TAG;

esp_err_t event_handler(void *ctx, system_event_t *event);
void initialise_wifi(void);

#endif