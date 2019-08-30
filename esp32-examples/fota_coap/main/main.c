/* OTA example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_flash_partitions.h"
#include "esp_partition.h"

#include "libcoap.h"
#include "coap_dtls.h"
#include "coap.h"

#include "driver/gpio.h"

#include "nvs.h"
#include "nvs_flash.h"

#define COAP_DEFAULT_TIME_SEC 5

#define EXAMPLE_WIFI_SSID CONFIG_WIFI_SSID
#define EXAMPLE_WIFI_PASS CONFIG_WIFI_PASSWORD
#define EXAMPLE_SERVER_URL CONFIG_FIRMWARE_UPG_URL
#define BUFFSIZE 1024
#define HASH_LEN 32 /* SHA-256 digest length */

#define UPD_BTN	GPIO_NUM_5
#define LED	GPIO_NUM_19

static const char *TAG = "fota_coap";
/*an ota data write buffer ready to write to the flash*/
static char ota_write_data[BUFFSIZE + 1] = { 0 };
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;
static xQueueHandle gpio_evt_queue = NULL;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
	switch (event->event_id) {
		case SYSTEM_EVENT_STA_START:
			esp_wifi_connect();
			break;
		case SYSTEM_EVENT_STA_GOT_IP:
			xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
			break;
		case SYSTEM_EVENT_STA_DISCONNECTED:
			/* This is a workaround as ESP32 WiFi libs don't currently
			   auto-reassociate. */
			esp_wifi_connect();
			xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
			break;
		default:
			break;
	}
	return ESP_OK;
}

static void initialise_wifi(void)
{
	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
	ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	wifi_config_t wifi_config = {
		.sta = {
			.ssid = EXAMPLE_WIFI_SSID,
			.password = EXAMPLE_WIFI_PASS,
		},
	};
	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK( esp_wifi_start() );
}

static void http_cleanup(esp_http_client_handle_t client)
{
	esp_http_client_close(client);
	esp_http_client_cleanup(client);
}

static void __attribute__((noreturn)) task_fatal_error()
{
	ESP_LOGE(TAG, "Exiting task due to fatal error...");
	(void)vTaskDelete(NULL);

	while (1) {
		;
	}
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

static void ota_example_task(void *pvParameter)
{
	uint32_t upd_btn;
	esp_err_t err;
	/* update handle : set by esp_ota_begin(), must be freed via esp_ota_end() */
	esp_ota_handle_t update_handle = 0 ;
	const esp_partition_t *update_partition = NULL;

	ESP_LOGI(TAG, "Starting OTA example...");

	const esp_partition_t *configured = esp_ota_get_boot_partition();
	const esp_partition_t *running = esp_ota_get_running_partition();

	if (configured != running) {
		ESP_LOGW(TAG, "Configured OTA boot partition at offset 0x%08x, but running from offset 0x%08x",
				configured->address, running->address);
		ESP_LOGW(TAG, "(This can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
	}
	ESP_LOGI(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
			running->type, running->subtype, running->address);

	/* Wait for the callback to set the CONNECTED_BIT in the
	   event group.
	   */
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
			false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connect to Wifi ! Start to Connect to Server....");

	ESP_LOGI(TAG, "Waiting for user to initiate update...");
	while (1) {
		if(xQueueReceive(gpio_evt_queue, &upd_btn, portMAX_DELAY)) break;
	}

	esp_http_client_config_t config = {
		.url = EXAMPLE_SERVER_URL,
		.cert_pem = (char *)server_cert_pem_start,
	};
	esp_http_client_handle_t client = esp_http_client_init(&config);
	if (client == NULL) {
		ESP_LOGE(TAG, "Failed to initialise HTTP connection");
		task_fatal_error();
	}
	err = esp_http_client_open(client, 0);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
		esp_http_client_cleanup(client);
		task_fatal_error();
	}
	esp_http_client_fetch_headers(client);

	update_partition = esp_ota_get_next_update_partition(NULL);
	ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
			update_partition->subtype, update_partition->address);
	assert(update_partition != NULL);

	err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
		http_cleanup(client);
		task_fatal_error();
	}
	ESP_LOGI(TAG, "esp_ota_begin succeeded");

	int binary_file_length = 0;
	/*deal with all receive packet*/
	while (1) {
		int data_read = esp_http_client_read(client, ota_write_data, BUFFSIZE);
		if (data_read < 0) {
			ESP_LOGE(TAG, "Error: SSL data read error");
			http_cleanup(client);
			task_fatal_error();
		} else if (data_read > 0) {
			err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
			if (err != ESP_OK) {
				http_cleanup(client);
				task_fatal_error();
			}
			binary_file_length += data_read;
			ESP_LOGD(TAG, "Written image length %d", binary_file_length);
		} else if (data_read == 0) {
			ESP_LOGI(TAG, "Connection closed,all data received");
			break;
		}
	}
	ESP_LOGI(TAG, "Total Write binary data length : %d", binary_file_length);

	if (esp_ota_end(update_handle) != ESP_OK) {
		ESP_LOGE(TAG, "esp_ota_end failed!");
		http_cleanup(client);
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
		http_cleanup(client);
		task_fatal_error();
	}
	ESP_LOGI(TAG, "Prepare to restart system!");
	esp_restart();
	return ;
}

static void update_isr_handler(void *arg)
{
	uint32_t gpio_num = (uint32_t) arg; // haxxy hax
	if (gpio_num == UPD_BTN) {
		xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
	}
}

void blink(void *pvParameter)
{
	ESP_LOGI(TAG, "Starting blink task");
	while (1) {
		gpio_set_level(LED, 1);
		vTaskDelay(500 / portTICK_RATE_MS);
		gpio_set_level(LED, 0);
		vTaskDelay(500 / portTICK_RATE_MS);
	}
}

void app_main()
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
	
	initialise_wifi();

	// queue to handle gpio events from ISR
	gpio_evt_queue = xQueueCreate(2, sizeof(uint32_t));

	gpio_config_t io_conf = {
		.pin_bit_mask	= (1ULL << UPD_BTN),
		.mode		= GPIO_MODE_INPUT,
		.pull_up_en	= 0,
		.pull_down_en	= 1,
		.intr_type	= GPIO_PIN_INTR_POSEDGE
	};

	gpio_config(&io_conf);
	gpio_install_isr_service(0); // TODO: Add a the ESP_INTR_FLAG_X flag
	gpio_isr_handler_add(UPD_BTN, update_isr_handler, (void *) UPD_BTN);

	// set up LED
	io_conf.pin_bit_mask = (1ULL << LED);
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pull_down_en = 0;
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	gpio_config(&io_conf);

	xTaskCreate(&blink, "blink_task", 2048, NULL, 10, NULL);
	xTaskCreate(&ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
}
