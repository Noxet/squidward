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
#include "esp_http_client.h"
#include "esp_https_ota.h"

#include "driver/gpio.h"

#include "nvs.h"

#include "nvs_flash.h"

#define UPDATE_BTN	GPIO_NUM_5
#define LED		GPIO_NUM_19

// a 0 means a non-shared interrupt level of 1, 2, or 3.
// see https://github.com/espressif/esp-idf/blob/ad3b820e7/components/esp32/include/esp_intr_alloc.h
// for the esp_intr_alloc_intrstatus function.
#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "fota_https";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;
static xQueueHandle gpio_evt_queue = NULL;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
	switch(evt->event_id) {
		case HTTP_EVENT_ERROR:
			ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
			break;
		case HTTP_EVENT_ON_CONNECTED:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
			break;
		case HTTP_EVENT_HEADER_SENT:
			ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
			break;
		case HTTP_EVENT_ON_HEADER:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
			break;
		case HTTP_EVENT_ON_DATA:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
			break;
		case HTTP_EVENT_ON_FINISH:
			ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
			break;
		case HTTP_EVENT_DISCONNECTED:
			ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
			break;
	}
	return ESP_OK;
}

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
			.ssid = CONFIG_EXAMPLE_WIFI_SSID,
			.password = CONFIG_EXAMPLE_WIFI_PASSWORD,
		},
	};
	ESP_LOGI(TAG, "Setting WiFi configuration SSID %s", wifi_config.sta.ssid);
	ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
	ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
	ESP_ERROR_CHECK( esp_wifi_start() );
}

void ota_update(void *pvParameter)
{
	uint32_t upd_btn;
	ESP_LOGI(TAG, "Starting OTA task");

	/* Wait for the callback to set the CONNECTED_BIT in the
	   event group.
	   */
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
			false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connected to WiFi network!");
	
	while (1) {
		ESP_LOGI(TAG, "Waiting for user to initiate update...");
		while (1) {
			if(xQueueReceive(gpio_evt_queue, &upd_btn, portMAX_DELAY)) break;
		}

		ESP_LOGI(TAG, "Update requested, updating...");

		esp_http_client_config_t config = {
			.url = CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL,
			.cert_pem = (char *)server_cert_pem_start,
			.event_handler = _http_event_handler,
		};
		esp_err_t ret = esp_https_ota(&config);
		if (ret == ESP_OK) {
			esp_restart();
		} else {
			ESP_LOGE(TAG, "Firmware upgrade failed");
		}
	}

	// should not get here, but if so, delete this task
	vTaskDelete(NULL);
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
	uint32_t gpio_num = (uint32_t) arg;
	if (gpio_num == UPDATE_BTN) {
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
	// Initialize NVS.
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		// 1.OTA app partition table has a smaller NVS partition size than the non-OTA
		// partition table. This size mismatch may cause NVS initialization to fail.
		// 2.NVS partition contains data in new format and cannot be recognized by this version of code.
		// If this happens, we erase NVS partition and initialize NVS again.
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK( err );

	initialise_wifi();
	
	// queue to handle gpio events from ISR
	gpio_evt_queue = xQueueCreate(2, sizeof(uint32_t));

	// set up button to trigger update on interrupt
	gpio_config_t io_conf = {
		.pin_bit_mask	= (1ULL << UPDATE_BTN),
		.mode		= GPIO_MODE_INPUT,
		.pull_up_en	= 0,
		.pull_down_en	= 1,
		.intr_type	= GPIO_PIN_INTR_POSEDGE
	};

	gpio_config(&io_conf);
	gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
	gpio_isr_handler_add(UPDATE_BTN, gpio_isr_handler, (void *) UPDATE_BTN);

	// set up LED
	io_conf.pin_bit_mask = (1ULL << LED);
	io_conf.mode = GPIO_MODE_OUTPUT;
	io_conf.pull_down_en = 0;
	io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
	gpio_config(&io_conf);

	xTaskCreate(&blink, "blink_task", 2048, NULL, 10, NULL);
	xTaskCreate(&ota_update, "ota_update_task", 8192, NULL, 5, NULL);
}
