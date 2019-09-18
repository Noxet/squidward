
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "nvs_flash.h"

#include "squidward/sq_wifi.h"
#include "squidward/sq_coap.h"

#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

const int CONNECTED_BIT = BIT0;
EventGroupHandle_t wifi_event_group;

static int resp_wait = 1;
static int wait_ms;

const char *TAG = "coaps_fota";

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

#ifdef CONFIG_SQ_COAP_DBG
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

				tid = coap_send(session, pdu);

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

void print_sha256 (const uint8_t *image_hash, const char *label)
{
	char hash_print[HASH_LEN * 2 + 1];
	hash_print[HASH_LEN * 2] = 0;
	for (int i = 0; i < HASH_LEN; ++i) {
		sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
	}
	ESP_LOGI(TAG, "%s: %s", label, hash_print);
}


// static void ota_example_task(void *pvParameter)
// {
// 	uint32_t upd_btn;
// 	esp_err_t err;
// 	/* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
// 	esp_ota_handle_t update_handle = 0 ;
// 	const esp_partition_t *update_partition = NULL;

// 	ESP_LOGI(TAG, "Starting OTA example...");

// 	const esp_partition_t *configured = esp_ota_get_boot_partition();
// 	const esp_partition_t *running = esp_ota_get_running_partition();

// 	if (configured != running) {
// 		ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
// 				configured->address, running->address);
// 		ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
// 	}
// 	ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
// 			running->type, running->subtype, running->address);

// 	/* Wait for the callback to set the CONNECTED_BIT in the
// 	   event group.
// 	   */
// 	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
// 			false, true, portMAX_DELAY);
// 	ESP_LOGI(TAG, "Connect to Wifi ! Start to Connect to Server....");

//     /*
// 	ESP_LOGI(TAG, "Waiting for user to initiate update...");
// 	while (1) {
// 		if (xQueueReceive(gpio_evt_queue, &upd_btn, portMAX_DELAY)) break;
// 	}
//     */

//    /*
// 	esp_http_client_config_t config = {
// 		.url = EXAMPLE_SERVER_URL,
// 		.cert_pem = (char *)server_cert_pem_start,
// 	};
// 	esp_http_client_handle_t client = esp_http_client_init(&config);
// 	if (client == NULL) {
// 		ESP_LOGE(TAG, "Failed to initialise HTTP connection");
// 		task_fatal_error();
// 	}
// 	err = esp_http_client_open(client, 0);
// 	if (err != ESP_OK) {
// 		ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
// 		esp_http_client_cleanup(client);
// 		task_fatal_error();
// 	}
// 	esp_http_client_fetch_headers(client);
//     */

// 	update_partition = esp_ota_get_next_update_partition(NULL);
// 	ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
// 			update_partition->subtype, update_partition->address);
// 	assert(update_partition != NULL);

// 	err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
// 	if (err != ESP_OK) {
// 		ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
// 		http_cleanup(client);
// 		task_fatal_error();
// 	}
// 	ESP_LOGI(TAG, "esp_ota_begin succeeded");

// 	int binary_file_length = 0;
// 	/*deal with all receive packet*/
// 	while (1) {
// 		int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
// 		if (data_read < 0) {
// 			ESP_LOGE(TAG, "Error: SSL data read error");
// 			http_cleanup(client);
// 			task_fatal_error();
// 		} else if (data_read > 0) {
// 			err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
// 			if (err != ESP_OK) {
// 				http_cleanup(client);
// 				task_fatal_error();
// 			}
// 			binary_file_length += data_read;
// 			ESP_LOGI(TAG, "Written image length %d", binary_file_length);
// 		} else if (data_read == 0) {
// 			ESP_LOGI(TAG, "Connection closed,all data received");
// 			break;
// 		}
// 	}
// 	ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

// 	if (esp_ota_end(update_handle) != ESP_OK) {
// 		ESP_LOGE(TAG, "esp_ota_end failed!");
// 		http_cleanup(client);
// 		task_fatal_error();
// 	}

// 	if (esp_partition_check_identity(esp_ota_get_running_partition(), update_partition) == true) {
// 		ESP_LOGI(TAG, "The current running firmware is same as the firmware just downloaded");
// 		int i = 0;
// 		ESP_LOGI(TAG, "When a new firmware is available on the server, press the reset button to download it");
// 		while(1) {
// 			ESP_LOGI(TAG, "Waiting for a new firmware ... %d", ++i);
// 			vTaskDelay(2000 / portTICK_PERIOD_MS);
// 		}
// 	}

// 	err = esp_ota_set_boot_partition(update_partition);
// 	if (err != ESP_OK) {
// 		ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
// 		http_cleanup(client);
// 		task_fatal_error();
// 	}
// 	ESP_LOGI(TAG, "Prepare to restart system!");
// 	esp_restart();
// 	return ;
// }

void sq_main(void *p)
{
	coap_context_t  *ctx = NULL;
	coap_session_t  *session = NULL;
	coap_pdu_t      *request = NULL;
	
	int res;

	while (1) {
		res = sq_coap_init(&ctx, &session);
		if (res == SQ_COAP_OK) {
#ifdef CONFIG_SQ_COAP_DBG
			ESP_LOGI(TAG, "[%s] - CoAP init OK", __FUNCTION__);
			ESP_LOGI(TAG, "[%s] - ctx: %p, session: %p", __FUNCTION__, ctx, session);
#endif
			break;
		} else if (res == SQ_COAP_ERR_DNS) {
			/* Wait a while, the retry */
#ifdef CONFIG_SQ_COAP_DBG
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
#ifdef CONFIG_SQ_COAP_DBG
	ESP_LOGI(TAG, "[%s] - Registered response handler", __FUNCTION__);
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
	coap_send(session, request);

#ifdef CONFIG_SQ_COAP_DBG
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

#ifdef CONFIG_SQ_COAP_DBG
	ESP_LOGI(TAG, "[%s] - Response handled, exiting", __FUNCTION__);
#endif

exit:
	vTaskDelete(NULL);
}

void app_main(void)
{
	uint8_t sha_256[HASH_LEN] = { 0 };
	esp_partition_t partition;

	// get sha256 digest for the partition table
	partition.address   = ESP_PARTITION_TABLE_OFFSET;
	partition.size      = ESP_PARTITION_TABLE_MAX_LEN;
	partition.type      = ESP_PARTITION_TYPE_DATA;
	esp_partition_get_sha256(&partition, sha_256);
	print_sha256(sha_256, "SHA-256 for the partition table: ");

	// get sha256 digest for bootloader
	partition.address   = ESP_BOOTLOADER_OFFSET;
	partition.size      = ESP_PARTITION_TABLE_OFFSET;
	partition.type      = ESP_PARTITION_TYPE_APP;
	esp_partition_get_sha256(&partition, sha_256);
	print_sha256(sha_256, "SHA-256 for bootloader: ");

	// get sha256 digest for running partition
	esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
	print_sha256(sha_256, "SHA-256 for current firmware: ");

	// Initialize NVS.
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

	xTaskCreate(sq_main, "coaps_fota", 8 * 1024, NULL, 5, NULL);
}
