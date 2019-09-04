#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_sleep.h"
#include "libcoap.h"
#include "coap_dtls.h"
#include "coap.h"
#include "driver/uart.h"
#include "driver/rtc_io.h"

#define COAP_DEFAULT_TIME_SEC 5
#define EXAMPLE_COAP_PSK_KEY CONFIG_EXAMPLE_COAP_PSK_KEY
#define EXAMPLE_COAP_PSK_IDENTITY CONFIG_EXAMPLE_COAP_PSK_IDENTITY
#define EXAMPLE_COAP_LOG_DEFAULT_LEVEL CONFIG_COAP_LOG_DEFAULT_LEVEL
#define COAP_DEFAULT_DEMO_URI CONFIG_EXAMPLE_TARGET_DOMAIN_URI

const static RTC_DATA_ATTR char *TAG = "CoAP_client";
static RTC_DATA_ATTR int resp_wait = 1;
static RTC_DATA_ATTR coap_optlist_t *optlist = NULL;
static RTC_DATA_ATTR int wait_ms;

RTC_DATA_ATTR struct hostent *hp;
RTC_DATA_ATTR coap_address_t    dst_addr;
RTC_DATA_ATTR static coap_uri_t uri;
RTC_DATA_ATTR const char       *server_uri;
RTC_DATA_ATTR char *phostname = NULL;
RTC_DATA_ATTR coap_context_t *ctx = NULL;
RTC_DATA_ATTR coap_session_t *session = NULL;
RTC_DATA_ATTR coap_pdu_t *request = NULL;

#define BUFSIZE 40
RTC_DATA_ATTR		unsigned char _buf[BUFSIZE];
RTC_DATA_ATTR		unsigned char *buf;
RTC_DATA_ATTR		size_t buflen;
RTC_DATA_ATTR		int res;
RTC_DATA_ATTR		char tmpbuf[INET6_ADDRSTRLEN];


static RTC_DATA_ATTR struct timeval sleep_enter_time;


int is_wakeUp_from_sllep;
int deepsleep=0;
int lightsleep=1;

static void message_handler(coap_context_t *ctx, coap_session_t *session,
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

				tid = coap_send(session, pdu);

				if (tid != COAP_INVALID_TID) {
					resp_wait = 1;
					wait_ms = COAP_DEFAULT_TIME_SEC * 1000;
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



static void coap_example_client(void *p)
{
	
	if (is_wakeUp_from_sllep && deepsleep)
		goto start_after_wakeup;

	server_uri = COAP_DEFAULT_DEMO_URI;
	phostname = NULL;

	coap_set_log_level(EXAMPLE_COAP_LOG_DEFAULT_LEVEL);
	ctx = NULL;
	session = NULL;
	request = NULL;
	optlist = NULL;
	while (1) {


		if (coap_split_uri((const uint8_t *)server_uri, strlen(server_uri), &uri) == -1) {
			ESP_LOGE(TAG, "CoAP server uri error");
			break;
		}

		if ((uri.scheme == COAP_URI_SCHEME_COAPS && !coap_dtls_is_supported()) ||
			(uri.scheme == COAP_URI_SCHEME_COAPS_TCP && !coap_tls_is_supported())) {
			ESP_LOGE(TAG, "CoAP server uri scheme is not supported");
		break;
	}

	phostname = (char *)calloc(1, uri.host.length + 1);
	if (phostname == NULL) {
		ESP_LOGE(TAG, "calloc failed");
		break;
	}

	memcpy(phostname, uri.host.s, uri.host.length);
	hp = gethostbyname(phostname);
	free(phostname);

	if (hp == NULL) {
		ESP_LOGE(TAG, "DNS lookup failed");
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		free(phostname);
		continue;
	}
	
	coap_address_init(&dst_addr);
	switch (hp->h_addrtype) {
		case AF_INET:
		dst_addr.addr.sin.sin_family      = AF_INET;
		dst_addr.addr.sin.sin_port        = htons(uri.port);
		memcpy(&dst_addr.addr.sin.sin_addr, hp->h_addr, sizeof(dst_addr.addr.sin.sin_addr));
		inet_ntop(AF_INET, &dst_addr.addr.sin.sin_addr, tmpbuf, sizeof(tmpbuf));
		ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", tmpbuf);
		break;
		case AF_INET6:
		dst_addr.addr.sin6.sin6_family      = AF_INET6;
		dst_addr.addr.sin6.sin6_port        = htons(uri.port);
		memcpy(&dst_addr.addr.sin6.sin6_addr, hp->h_addr, sizeof(dst_addr.addr.sin6.sin6_addr));
		inet_ntop(AF_INET6, &dst_addr.addr.sin6.sin6_addr, tmpbuf, sizeof(tmpbuf));
		ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", tmpbuf);
		break;
		default:
		ESP_LOGE(TAG, "DNS lookup response failed");
		goto clean_up;
	}

	if (uri.path.length) {
		buflen = BUFSIZE;
		buf = _buf;
		res = coap_split_path(uri.path.s, uri.path.length, buf, &buflen);

		while (res--) {
			coap_insert_optlist(&optlist,
				coap_new_optlist(COAP_OPTION_URI_PATH,
					coap_opt_length(buf),
					coap_opt_value(buf)));

			buf += coap_opt_size(buf);
		}
	}

	if (uri.query.length) {
		buflen = BUFSIZE;
		buf = _buf;
		res = coap_split_query(uri.query.s, uri.query.length, buf, &buflen);

		while (res--) {
			coap_insert_optlist(&optlist,
				coap_new_optlist(COAP_OPTION_URI_QUERY,
					coap_opt_length(buf),
					coap_opt_value(buf)));

			buf += coap_opt_size(buf);
		}
	}

	ctx = coap_new_context(NULL);
	if (!ctx) {
		ESP_LOGE(TAG, "coap_new_context() failed");
		goto clean_up;
	}

	if (uri.scheme == COAP_URI_SCHEME_COAPS || uri.scheme == COAP_URI_SCHEME_COAPS_TCP) {
		if (!session){
			session = coap_new_client_session_psk(ctx, NULL, &dst_addr,
				uri.scheme == COAP_URI_SCHEME_COAPS ? COAP_PROTO_DTLS : COAP_PROTO_TLS,
				EXAMPLE_COAP_PSK_IDENTITY,
				(const uint8_t *)EXAMPLE_COAP_PSK_KEY,
				sizeof(EXAMPLE_COAP_PSK_KEY) - 1);}


		} else {
			session = coap_new_client_session(ctx, NULL, &dst_addr,
				uri.scheme == COAP_URI_SCHEME_COAP_TCP ? COAP_PROTO_TCP :
				COAP_PROTO_UDP);
		}
		if (!session) {
			ESP_LOGE(TAG, "coap_new_client_session() failed");
			goto clean_up;
		}

		coap_register_response_handler(ctx, message_handler);

		start_after_wakeup:
		for (;;){
			request = coap_new_pdu(session);
			if (!request) {
				ESP_LOGE(TAG, "coap_new_pdu() failed");
				goto clean_up;
			}
			request->type = COAP_MESSAGE_CON;
			request->tid = coap_new_message_id(session);
			request->code = COAP_REQUEST_GET;
			coap_add_optlist_pdu(request, &optlist);
			resp_wait = 1;
			coap_send(session, request);
			wait_ms = COAP_DEFAULT_TIME_SEC * 1000;
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
		        if(deepsleep){
		        	vTaskDelay(1000 / portTICK_PERIOD_MS);
				    const int wakeup_time_sec = 20;
				    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
				    esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
				    printf("Entering deep sleep\n");
				    gettimeofday(&sleep_enter_time, NULL);
				    esp_deep_sleep_start();
				}
				else if (lightsleep)
				{
					/* Wake up in 20 seconds*/
					printf("Stop wifi\n");
					//esp_wifi_stop();
			        esp_sleep_enable_timer_wakeup(30000000);
			        printf("Entering light sleep\n");
			        uart_wait_tx_idle_polling(CONFIG_ESP_CONSOLE_UART_NUM);
			        esp_light_sleep_start();
			      	printf("wakeuped !");
			        esp_wifi_connect();
			        sleep(5);
				}
				else{
					printf("code sleep for 10 secounds");
					sleep(20);
					
				}



		}
		clean_up:
		if (optlist) {
			coap_delete_optlist(optlist);
			optlist = NULL;
		}
		if (session) {
            
		}
		if (ctx) {
           
		}
		break;
	}

	vTaskDelete(NULL);
}

void app_main(void)
{
	ESP_ERROR_CHECK( nvs_flash_init() );
	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(example_connect());


	struct timeval now;
    gettimeofday(&now, NULL);
    int sleep_time_ms = (now.tv_sec - sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - sleep_enter_time.tv_usec) / 1000;

    switch (esp_sleep_get_wakeup_cause()) {
        case ESP_SLEEP_WAKEUP_TIMER: {
            printf(">>Wake up from timer. Time spent in deep sleep: %dms\n", sleep_time_ms);
            is_wakeUp_from_sllep=1;

            break;
        }
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        default:
            printf("Not a deep sleep reset\n");
            is_wakeUp_from_sllep=0;
    }

	xTaskCreate(coap_example_client, "coap", 8 * 1024, NULL, 5, NULL);
}
