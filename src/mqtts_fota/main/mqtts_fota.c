#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "esp_tls.h"
#include "mqtt_client.h"

#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"

#include "driver/gpio.h"

#include "squidward/sq_wifi.h"
#include "squidward/sq_uart.h"

const char *TAG = "mqtts_fota";

#define MQTT_TOPIC "/squidward"

#define OTA_BUFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

static volatile int fota_wait = 1;

#define LED GPIO_NUM_18

/* 0 means a non-shared interrupt level of 1, 2, or 3.
 * see the esp_intr_alloc.h file for the esp_alloc_intrstatus function.
 */
#define ESP_INTR_FLAG_DEFAULT 0

static esp_ota_handle_t update_handle = 0;
//static char ota_write_data[OTA_BUFSIZE + 1] = { 0 };

EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

const char ant_sub[]		= "MQTT subscribe\n";
const char ant_sub_done[]	= "MQTT subscribe done\n";
const char ant_pub[]		= "MQTT publish";
const char ant_pub_done[]	= "MQTT publish done\n";
const char ant_mqtt_setup[]	= "MQTT setup\n";
const char ant_mqtt_conn[]	= "MQTT connected\n";

const char ant_ota_write[]		= "OTA write block\n";
const char ant_ota_write_done[]	= "OTA write done\n";


//#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
//static const uint8_t mqtt_server_ca_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
//#else
#ifdef CONFIG_SQ_MQTT_CERT_RSA
extern const uint8_t mqtt_server_ca_pem_start[]	asm("_binary_mqtt_server_ca_rsa_pem_start");
extern const uint8_t mqtt_server_ca_pem_end[]	asm("_binary_mqtt_server_ca_rsa_pem_end");
#endif
#ifdef CONFIG_SQ_MQTT_CERT_ECDSA
extern const uint8_t mqtt_server_ca_pem_start[]	asm("_binary_mqtt_server_ca_ecdsa_pem_start");
extern const uint8_t mqtt_server_ca_pem_end[]	asm("_binary_mqtt_server_ca_ecdsa_pem_end");
#endif
//#endif

#ifdef CONFIG_SQ_MQTT_PSK
	struct psk_key_hint psk_key = {
		.key = (uint8_t *) "password",
		.key_size = 8,
		.hint = "esp32"
	};
#endif

static void __attribute__((noreturn)) task_fatal_error()
{
	ESP_LOGE(TAG, "Exiting task due to fatal error...");
	(void)vTaskDelete(NULL);

	while (1) {
		;
	}
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;
	esp_err_t err;
	static int total_written = 0;
	int total_fota_size = 0;

	// your_context_t *context = event->context;
	switch (event->event_id) {
		case MQTT_EVENT_CONNECTED:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
			ESP_LOGI(TAG, "Subscribing to /updates");
#endif

			sq_uart_send(ant_sub, sizeof(ant_sub));
			msg_id = esp_mqtt_client_subscribe(client, "/updates", 0);
			sq_uart_send(ant_sub_done, sizeof(ant_sub_done));
			break;
		case MQTT_EVENT_DISCONNECTED:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
#endif
			break;

		case MQTT_EVENT_SUBSCRIBED:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
#endif

			break;
		case MQTT_EVENT_UNSUBSCRIBED:
			break;
		case MQTT_EVENT_PUBLISHED:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
#endif
			break;
		case MQTT_EVENT_DATA:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_DATA");
#endif
			//printf("Total length: %d\r\n", event->total_data_len);
			//printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
			//printf("DATA=%.*s\r\n", event->data_len, event->data);

			total_fota_size = event->total_data_len;

			/* Write firmware to FLASH */
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "[%s] - Writing %d bytes of OTA data", __FUNCTION__, event->data_len);
#endif
			sq_uart_send(ant_ota_write, sizeof(ant_ota_write));
			err = esp_ota_write(update_handle, (const void *) event->data, event->data_len);
			if (err != ESP_OK) {
				ESP_ERROR_CHECK(err);
				task_fatal_error();
			}

			total_written += event->data_len;

			/* All data written, let the main function know */
			if (total_written >= total_fota_size) {
				fota_wait = 0;
			}

			break;
		case MQTT_EVENT_ERROR:
			ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
			break;
		default:
			ESP_LOGI(TAG, "Other event id:%d", event->event_id);
			break;
	}
	return ESP_OK;
}


static void mqtt_app_start(void)
{

	/* wait for WiFi connection */
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Connected to WiFi", __FUNCTION__);
#endif

	const esp_mqtt_client_config_t mqtt_cfg = {
		.uri = CONFIG_BROKER_URI,
		.event_handle = mqtt_event_handler,
#ifdef CONFIG_SQ_MQTT_PKI
		.cert_pem = (const char *)mqtt_server_ca_pem_start,
#endif
#ifdef CONFIG_SQ_MQTT_PSK
		.psk_hint_key = &psk_key,
#endif
	};

	ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	sq_uart_send(ant_mqtt_setup, sizeof(ant_mqtt_setup));
	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(client);
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


void mqtts_fota(void *pvParameter)
{
	esp_err_t err;

	const esp_partition_t	*update_partition = NULL;
	const esp_partition_t	*configured = esp_ota_get_boot_partition();
	const esp_partition_t	*running = esp_ota_get_running_partition();

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
		task_fatal_error();
	}
#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - esp_ota_begin succeeded", __FUNCTION__);
	ESP_LOGI(TAG, "[%s] - Awaiting firmware ...", __FUNCTION__);
#endif

	/* Wait until firmware is received */

	while (fota_wait) {
		esp_task_wdt_reset();
	}

	sq_uart_send(ant_ota_write_done, sizeof(ant_ota_write_done));

#ifdef CONFIG_SQ_MAIN_DBG
	ESP_LOGI(TAG, "[%s] - Firmware received, checking ...", __FUNCTION__);
#endif

	if (esp_ota_end(update_handle) != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end failed!");
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
		task_fatal_error();
	}

	ESP_LOGI(TAG, "Prepare to restart system!");
	esp_restart();

}

void app_main()
{
	ESP_LOGI(TAG, "[APP] Startup..");
	ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

	esp_log_level_set("*", ESP_LOG_INFO);
	esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
	esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
	esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
	esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
	esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

#ifndef CONFIG_SQ_MAIN_DBG
	/* Turn of all debugging. */
	ESP_LOGW(TAG, "Turning off all debugging logs.");
	esp_log_level_set("*", ESP_LOG_NONE);
#endif

	sq_uart_init();

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

	wifi_init();

	xTaskCreate(blink, "blink_task", 2048, NULL, 10, NULL);
	xTaskCreate(mqtts_fota, "mqtts_fota", 8 * 1024, NULL, 5, NULL);
	mqtt_app_start();
}
