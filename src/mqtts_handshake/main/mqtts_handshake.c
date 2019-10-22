#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
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

#include "squidward/sq_wifi.h"
#include "squidward/sq_uart.h"

const char *TAG = "mqtts";

#define MQTT_TOPIC "/squidward"

#define MQTT_DATA_BUF_SIZE 16
static char mqtt_data[MQTT_DATA_BUF_SIZE];

EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

const char ant_pub[]		= "MQTT publish";
const char ant_pub_done[]	= "MQTT publish done\n";
const char ant_mqtt_setup[]	= "MQTT setup\n";
const char ant_mqtt_conn[]	= "MQTT connected\n";


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

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
	esp_mqtt_client_handle_t client = event->client;
	int msg_id;

#define ANT_BUF_SIZE 64
	char annotation_msg[ANT_BUF_SIZE];

	// your_context_t *context = event->context;
	switch (event->event_id) {
		case MQTT_EVENT_CONNECTED:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
#endif

			sq_uart_send(ant_mqtt_conn, sizeof(ant_mqtt_conn));

			snprintf(annotation_msg, ANT_BUF_SIZE, "%s %d bytes\n", ant_pub, MQTT_DATA_BUF_SIZE);
			sq_uart_send(annotation_msg, strlen(annotation_msg));
			msg_id = esp_mqtt_client_publish(client, MQTT_TOPIC, mqtt_data, MQTT_DATA_BUF_SIZE, 0, 0);
			sq_uart_send(ant_pub_done, sizeof(ant_pub_done));

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
			ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
			break;
		case MQTT_EVENT_PUBLISHED:
#ifdef CONFIG_SQ_MAIN_DBG
			ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
#endif
			break;
		case MQTT_EVENT_DATA:
			ESP_LOGI(TAG, "MQTT_EVENT_DATA");
			printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
			printf("DATA=%.*s\r\n", event->data_len, event->data);
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

	/* initialize data buffer */
	for (int i = 0; i < MQTT_DATA_BUF_SIZE; i++) {
		mqtt_data[i] = 'a';
	}

	ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
	sq_uart_send(ant_mqtt_setup, sizeof(ant_mqtt_setup));
	esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_start(client);
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

	nvs_flash_init();
	wifi_init();
	mqtt_app_start();
}
