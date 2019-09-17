#ifndef SQUIDWARD_COAP_H
#define SQUIDWARD_COAP_H

#include <string.h>
#include <stdlib.h>
#include <netdb.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "libcoap.h"
#include "coap_dtls.h"
#include "coap.h"


#define SQ_COAP_URI             CONFIG_SQ_COAP_URI
#define SQ_COAP_LOG_LEVEL       CONFIG_SQ_COAP_LOG_LEVEL
#define SQ_COAP_PSK_IDENTITY    CONFIG_SQ_COAP_PSK_IDENTITY
#define SQ_COAP_PSK_KEY         CONFIG_SQ_COAP_PSK_KEY
#define SQ_COAP_TIME_SEC        CONFIG_SQ_COAP_TIME_SEC

extern const char *TAG;
static int resp_wait = 1;
static coap_optlist_t *optlist = NULL;
static int wait_ms;

extern void coap_message_handler(coap_context_t *, coap_session_t *, coap_pdu_t *, coap_pdu_t *, const coap_tid_t);
void coap_init(void *);

#endif
