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


#define SQ_COAP_URI				CONFIG_SQ_COAP_URI
#define SQ_COAP_LOG_LEVEL		CONFIG_SQ_COAP_LOG_LEVEL
#define SQ_COAP_PSK_IDENTITY	CONFIG_SQ_COAP_PSK_IDENTITY
#define SQ_COAP_PSK_KEY			CONFIG_SQ_COAP_PSK_KEY
#define SQ_COAP_TIME_SEC		CONFIG_SQ_COAP_TIME_SEC

#ifdef CONFIG_COAP_MBEDTLS_PKI
/* CA cert, taken from coap_ca.pem
   Client cert, taken from coap_client.crt
   Client key, taken from coap_client.key

   The PEM, CRT and KEY file are examples taken from the wpa2 enterprise
   example.

   To embed it in the app binary, the PEM, CRT and KEY file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
 */
extern uint8_t ca_pem_start[]		asm("_binary_coap_ca_pem_start");
extern uint8_t ca_pem_end[]			asm("_binary_coap_ca_pem_end");
extern uint8_t client_crt_start[]	asm("_binary_coap_client_crt_start");
extern uint8_t client_crt_end[]		asm("_binary_coap_client_crt_end");
extern uint8_t client_key_start[]	asm("_binary_coap_client_key_start");
extern uint8_t client_key_end[]		asm("_binary_coap_client_key_end");
#endif /* CONFIG_COAP_MBEDTLS_PKI */

#define SQ_COAP_OK			(0)
#define SQ_COAP_ERR_DNS		(1)
#define SQ_COAP_ERR_FAIL	(2)

extern const char *TAG;
static coap_optlist_t *optlist = NULL;

int sq_coap_init(coap_context_t **, coap_session_t **);
void sq_coap_cleanup(coap_context_t *, coap_session_t *);

#endif
