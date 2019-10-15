
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "driver/gpio.h"

#include "squidward/sq_wifi.h"
#include "squidward/sq_coap.h"
#include "squidward/sq_uart.h"

#define OTA_BUFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

static xQueueHandle gpio_evt_queue = NULL;
#define UPDATE_BTN	GPIO_NUM_5
#define LED 		GPIO_NUM_18

/* 0 means a non-shared interrupt level of 1, 2, or 3.
 * see the esp_intr_alloc.h file for the esp_alloc_intrstatus function.
 */
#define ESP_INTR_FLAG_DEFAULT 0

static esp_ota_handle_t update_handle = 0;
//static char ota_write_data[OTA_BUFSIZE + 1] = { 0 };

const int CONNECTED_BIT = BIT0;
EventGroupHandle_t wifi_event_group;

static int resp_wait = 1;
static int wait_ms;
static unsigned int next_block = 0;

const char *TAG = "coaps_fota";

/* Annotation strings */
const char ant_main[]					= "Device started\n";
const char ant_get_send[]				= "CoAP GET send\n";
const char ant_get_send_done[]			= "CoAP GET send done\n";
const char ant_get_resp[]				= "CoAP Got Response\n";
const char ant_get_block_send[]			= "CoAP GET block send\n";
const char ant_get_block_send_done[]	= "CoAP GET block send done\n";
const char ant_ota_write[]				= "OTA write block\n";
const char ant_ota_write_done[]			= "OTA write done\n";

static void __attribute__((noreturn)) task_fatal_error()
{
	ESP_LOGE(TAG, "Exiting task due to fatal error...");
	(void)vTaskDelete(NULL);

	while (1) {
		;
	}
}

static void coap_message_handler(coap_context_t *ctx, coap_session_t *session,
							coap_pdu_t *sent, coap_pdu_t *received,
							const coap_tid_t id)
{
	
	sq_uart_send(ant_get_resp, sizeof(ant_get_resp));

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Got response", __FUNCTION__);
#endif

	unsigned char *data = NULL;
	size_t data_len;
	coap_pdu_t *pdu = NULL;
	coap_opt_t *block_opt;
	coap_opt_iterator_t opt_iter;
	unsigned char buf[4];
	coap_optlist_t *option;
	coap_tid_t tid;
	esp_err_t err;
	unsigned int block_num;

	if (COAP_RESPONSE_CLASS(received->code) == 2) {
		/* Need to see if blocked response */
		block_opt = coap_check_option(received, COAP_OPTION_BLOCK2, &opt_iter);
		if (block_opt) {
			uint16_t blktype = opt_iter.type;

			block_num = coap_opt_block_num(block_opt);

			if (block_num == 0) {
#ifdef CONFIG_SQ_MAIN_DBG
				ESP_LOGI(TAG, "[%s] - Got block message", __FUNCTION__);
#endif
			}

			if (block_num != next_block) {
#ifdef CONFIG_SQ_MAIN_DBG
				ESP_LOGE(TAG, "[%s] - Got wrong block nr %d, expected block nr %d", __FUNCTION__, block_num, next_block);
#endif
				return;
			}

			if (coap_get_data(received, &data_len, &data)) {
				//printf("%.*s", (int)data_len, data);

				/* Write firmware to FLASH */
#ifdef CONFIG_SQ_MAIN_DBG
				ESP_LOGI(TAG, "Writing %d bytes of OTA data", data_len);
#endif
				sq_uart_send(ant_ota_write, sizeof(ant_ota_write));
				err = esp_ota_write(update_handle, (const void *) data, data_len);
				sq_uart_send(ant_get_block_send_done, sizeof(ant_ota_write_done));
				if (err != ESP_OK) {
					ESP_ERROR_CHECK(err);
					sq_coap_cleanup(ctx, session);
					task_fatal_error();
				}

				next_block++;
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
				coap_add_option(pdu, blktype, coap_encode_var_safe(buf, sizeof(buf),
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
				//printf("Received: %.*s\n", (int)data_len, data);
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

	esp_err_t err;

	const esp_partition_t	*update_partition = NULL;
	const esp_partition_t	*configured = esp_ota_get_boot_partition();
	const esp_partition_t	*running = esp_ota_get_running_partition();

	uint32_t upd_btn;

	if (configured != running) {
		ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
				configured->address, running->address);
		ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
	}
	ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
			running->type, running->subtype, running->address);

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

	/* Setup OTA handlers etc. */

	update_partition = esp_ota_get_next_update_partition(NULL);
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
			update_partition->subtype, update_partition->address);
#endif
	assert(update_partition != NULL);

	err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
		sq_coap_cleanup(ctx, session);
		task_fatal_error();
	}
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "esp_ota_begin succeeded");
#endif

	request = coap_new_pdu(session);
	if (!request) {
		ESP_LOGE(TAG, "coap_new_pdu() failed");
		sq_coap_cleanup(ctx, session);
		goto exit;
	}
	request->type = COAP_MESSAGE_CON;
	request->tid = coap_new_message_id(session);
	request->code = COAP_REQUEST_GET;
	coap_add_optlist_pdu(request, &optlist);

	resp_wait = 1;

	/* Wait for user input before retrieving the firmware */
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "Waiting for user to initiate update...");
#endif
	while (1) {
		if (xQueueReceive(gpio_evt_queue, &upd_btn, portMAX_DELAY)) break;
	}

	/* Perform GET request to retrieve new firmware.
	 * The data retrieval is done in the message handler.
	 */
	sq_uart_send(ant_get_send, sizeof(ant_get_send));
	coap_send(session, request);
	sq_uart_send(ant_get_send_done, sizeof(ant_get_send_done));

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

	if (esp_ota_end(update_handle) != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end failed!");
		sq_coap_cleanup(ctx, session);
		task_fatal_error();
	}

	if (esp_partition_check_identity(esp_ota_get_running_partition(), update_partition) == true) {
		ESP_LOGI(TAG, "The current running firmware is same as the firmware just downloaded");
		int i = 0;
		ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
		while(1) {
			ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
			vTaskDelay(2000 / portTICK_PERIOD_MS);
		}
	}

	err = esp_ota_set_boot_partition(update_partition);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
		sq_coap_cleanup(ctx, session);
		task_fatal_error();
	}

	ESP_LOGI(TAG, "Prepare to restart system!");
	esp_restart();

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Response handled, exiting", __FUNCTION__);
#endif

exit:
	vTaskDelete(NULL);
}

void print_sha256 (const uint8_t *image_hash, const char *label)
{
	char hash_print[HASH_LEN * 2 + 1];
	hash_print[HASH_LEN * 2] = 0;
	for (int i = 0; i < HASH_LEN; ++i) {
		sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
	}
	ESP_LOGI(TAG, "%s: %s", label, hash_print);
}

void blink(void *pvParameter)
{
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "Starting blink task...");
#endif

	gpio_config_t io_conf = {
		.pin_bit_mask	= (1ULL << LED),
		.mode			= GPIO_MODE_OUTPUT,
		.pull_up_en		= 0,
		.pull_down_en	= 0,
		.intr_type		= GPIO_PIN_INTR_DISABLE
	};

	gpio_config(&io_conf);

	while (1) {
		gpio_set_level(LED, 1);
		vTaskDelay(500 / portTICK_RATE_MS);
		gpio_set_level(LED, 0);
		vTaskDelay(500 / portTICK_RATE_MS);
	}
}

static void gpio_isr_handler(void *arg)
{
	uint32_t gpio_num = (uint32_t) arg;
	if (gpio_num == UPDATE_BTN) {
		xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
	}
}

void app_main(void)
{
#ifndef CONFIG_SQ_MAIN_DBG
	/* Turn of all debugging. */
	ESP_LOGW(TAG, "Turning off all debugging logs.");
	esp_log_level_set("*", ESP_LOG_NONE);
#endif

	/* Initialize NVS. */
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// OTA app partition table has a smaller NVS partition size than the non-OTA
		// partition table. This size mismatch may cause NVS initialization to fail.
		// If this happens, we erase NVS partition and initialize NVS again.
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );

	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init();

	sq_uart_init();
	sq_uart_send(ant_main, sizeof(ant_main));

	/* Set up and configure button interrupt */
	gpio_evt_queue = xQueueCreate(2,  sizeof(uint32_t));

	gpio_config_t io_conf = {
		.pin_bit_mask	= (1ULL << UPDATE_BTN),
		.mode			= GPIO_MODE_INPUT,
		.pull_up_en		= 0,
		.pull_down_en	= 1,
		.intr_type		= GPIO_PIN_INTR_POSEDGE
	};

	gpio_config(&io_conf);
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(UPDATE_BTN, gpio_isr_handler, (void *) UPDATE_BTN);

	xTaskCreate(blink, "blink_task", 2048, NULL, 10, NULL);
	xTaskCreate(sq_main, "coaps_fota", 8 * 1024, NULL, 5, NULL);
}
