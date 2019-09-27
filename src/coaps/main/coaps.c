
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"

#include "nvs_flash.h"

#include "squidward/sq_wifi.h"
#include "squidward/sq_coap.h"
#include "squidward/sq_uart.h"


const int CONNECTED_BIT = BIT0;
EventGroupHandle_t wifi_event_group;

static int resp_wait = 1;
static int wait_ms;

const char *TAG = "coaps";

/* Annotation strings */
const char ant_post_send[32]			= "CoAP POST send";
const char ant_post_send_done[]			= "CoAP POST send done\n";
const char ant_get_block_send[]			= "CoAP GET block send\n";
const char ant_get_block_send_done[]	= "CoAP GET block send done\n";

unsigned char post_data[1024];

static void coap_message_handler(coap_context_t *ctx, coap_session_t *session,
							coap_pdu_t *sent, coap_pdu_t *received,
							const coap_tid_t id)
{
	unsigned char *data = NULL;
	size_t data_len;
	coap_pdu_t *pdu = NULL;
	coap_opt_t *block_opt;
	coap_opt_iterator_t opt_iter;
	unsigned char buf[4];
	coap_optlist_t *option;
	coap_tid_t tid;

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Got response", __FUNCTION__);
#endif

	if (COAP_RESPONSE_CLASS(received->code) == 2) {
		/* Need to see if blocked response */
		block_opt = coap_check_option(received, COAP_OPTION_BLOCK2, &opt_iter);
		if (block_opt) {
			uint16_t blktype = opt_iter.type;

			if (coap_opt_block_num(block_opt) == 0) {
				printf("Received:\n");
			}
			if (coap_get_data(received, &data_len, &data)) {
				printf("%.*s", (int)data_len, data);
			}
			if (COAP_OPT_BLOCK_MORE(block_opt)) {
				/* more bit is set */

				/* create pdu with request for next block */
				pdu = coap_new_pdu(session);
				if (!pdu) {
					ESP_LOGE(TAG, "coap_new_pdu() failed");
					goto clean_up;
				}
				pdu->type = COAP_MESSAGE_CON;
				pdu->tid = coap_new_message_id(session);
				pdu->code = COAP_REQUEST_GET;

				/* add URI components from optlist */
				for (option = optlist; option; option = option->next ) {
					switch (option->number) {
					case COAP_OPTION_URI_HOST :
					case COAP_OPTION_URI_PORT :
					case COAP_OPTION_URI_PATH :
					case COAP_OPTION_URI_QUERY :
						coap_add_option(pdu, option->number, option->length,
										option->data);
						break;
					default:
						;     /* skip other options */
					}
				}

				/* finally add updated block option from response, clear M bit */
				/* blocknr = (blocknr & 0xfffffff7) + 0x10; */
				coap_add_option(pdu,
								blktype,
								coap_encode_var_safe(buf, sizeof(buf),
													 ((coap_opt_block_num(block_opt) + 1) << 4) |
													 COAP_OPT_BLOCK_SZX(block_opt)), buf);
				sq_uart_send(ant_get_block_send, sizeof(ant_get_block_send));
				tid = coap_send(session, pdu);
				sq_uart_send(ant_get_block_send_done, sizeof(ant_get_block_send_done));

				if (tid != COAP_INVALID_TID) {
					resp_wait = 1;
					wait_ms = SQ_COAP_TIME_SEC * 1000;
					return;
				}
			}
			printf("\n");
		} else {
			if (coap_get_data(received, &data_len, &data)) {
				printf("Received: %.*s\n", (int)data_len, data);
			}
		}
	}
clean_up:
	resp_wait = 0;
}

void sq_main(void *p)
{
	coap_context_t  *ctx = NULL;
	coap_session_t  *session = NULL;
	coap_pdu_t      *request = NULL;

	/* Initialize data to be sent */
	for (int i = 0; i < 1024; i++) {
		post_data[i] = 'a';
	}

#define ANT_BUF_SIZE 64
	char annotation_msg[ANT_BUF_SIZE];
	
	int res;

	/* Wait for WiFi connection */
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Wait for WiFi...", __FUNCTION__);
#endif

	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Connected to WiFi", __FUNCTION__);
#endif

	while (1) {
		res = sq_coap_init(&ctx, &session);
		if (res == SQ_COAP_OK) {
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "[%s] - CoAP init OK", __FUNCTION__);
			ESP_LOGI(TAG, "[%s] - ctx: %p, session: %p", __FUNCTION__, ctx, session);
#endif
			break;
		} else if (res == SQ_COAP_ERR_DNS) {
			/* Wait a while, the retry */
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "[%s] - DNS lookup error, wait and try again...", __FUNCTION__);
#endif
			vTaskDelay(1000 / portTICK_PERIOD_MS);
			continue;
		} else if (res == SQ_COAP_ERR_FAIL) {
			ESP_LOGE(TAG, "Caught unrecoverable error when initializing CoAP, exiting...");
			goto exit;
		}
	}

	coap_register_response_handler(ctx, coap_message_handler);
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Registered response handler", __FUNCTION__);
#endif

	int post_len = 1;
	for (int i = 0; i <= 10; i++) {
		request = coap_new_pdu(session);
		if (!request) {
			ESP_LOGE(TAG, "coap_new_pdu() failed");
			sq_coap_cleanup(ctx, session);
			goto exit;
		}
		request->type = COAP_MESSAGE_CON;
		request->tid = coap_new_message_id(session);
		request->code = COAP_REQUEST_POST;
		coap_add_optlist_pdu(request, &optlist);

		/* Add POST data, double the size each time */
		coap_add_data(request, post_len, post_data);

		resp_wait = 1;
		snprintf(annotation_msg, ANT_BUF_SIZE, "%s %d bytes\n", ant_post_send, post_len);
		sq_uart_send(annotation_msg, strlen(annotation_msg));
		coap_send(session, request);
		sq_uart_send(ant_post_send_done, sizeof(ant_post_send_done));

		post_len *= 2;

	#ifdef CONFIG_SQ_MAIN_DBG
		ESP_LOGI(TAG, "[%s] - CoAP message sent, awaiting response", __FUNCTION__);
	#endif

		wait_ms = SQ_COAP_TIME_SEC * 1000;

		while (resp_wait) {
			int result = coap_run_once(ctx, wait_ms > 1000 ? 1000 : wait_ms);
			if (result >= 0) {
				if (result >= wait_ms) {
					ESP_LOGE(TAG, "select timeout");
					break;
				} else {
					wait_ms -= result;
				}
			}
		}
		sleep(1);
	}

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Response handled, exiting", __FUNCTION__);
#endif

exit:
	vTaskDelete(NULL);
}

void app_main(void)
{
#ifndef CONFIG_SQ_MAIN_DBG
	/* Turn of all debugging. */
	ESP_LOGW(TAG, "Turning off all debugging logs.");
	esp_log_level_set("*", ESP_LOG_NONE);
#endif

	ESP_ERROR_CHECK(nvs_flash_init());

	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init();

	sq_uart_init();

	xTaskCreate(sq_main, "coaps", 8 * 1024, NULL, 5, NULL);
}
