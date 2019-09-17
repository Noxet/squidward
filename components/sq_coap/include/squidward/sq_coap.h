#ifndef SQUIDWARD_COAP_H
#define SQUIDWARD_COAP_H

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

#if 1
#include "libcoap.h"
#include "coap_dtls.h"
#endif
#include "coap.h"

void coap_init(void *);

#endif
