
/*  WiFi softAP Example

    Control an LED over HTTP.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include <esp_http_server.h>

/* The examples use WiFi configuration that you can set via 'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
   */
#define EXAMPLE_ESP_WIFI_SSID      "IPFreely"
#define EXAMPLE_ESP_WIFI_PASS      ""
#define EXAMPLE_MAX_STA_CONN       4

#define LED_PIN 5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static const char *TAG = "wifi softAP";

static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
		int32_t event_id, void* event_data)
{
	if (event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
				MAC2STR(event->mac), event->aid);
	} else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
				MAC2STR(event->mac), event->aid);
	}
}

void wifi_init_softap()
{
	s_wifi_event_group = xEventGroupCreate();

	tcpip_adapter_init();
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

	wifi_config_t wifi_config = {
		.ap = {
			.ssid = EXAMPLE_ESP_WIFI_SSID,
			.ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
			.password = EXAMPLE_ESP_WIFI_PASS,
			.max_connection = EXAMPLE_MAX_STA_CONN,
			.authmode = WIFI_AUTH_WPA_WPA2_PSK
		},
	};
	if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
		wifi_config.ap.authmode = WIFI_AUTH_OPEN;
	}

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s",
			EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
}

esp_err_t get_handler(httpd_req_t *req)
{
	/* Send a simple response */
	const char resp[] = "ESP32 says Hello\n";
	httpd_resp_send(req, resp, strlen(resp));
	return ESP_OK;
}

esp_err_t led_handler(httpd_req_t *req)
{
	/* Send a simple response */
	const char led_on[] = "LED is on\n";
	const char led_off[] = "LED is off\n";
	const char err[] = "Undefined\n";
	const char *resp = err;

	char content[10];

	httpd_req_recv(req, content, req->content_len);
	
	if (strncmp(content, "1", 1) == 0) {
		gpio_set_level(LED_PIN, 1);
		resp = led_on;
	} else if (strncmp(content, "0", 1) == 0) {
		gpio_set_level(LED_PIN, 0);
		resp = led_off;
	}

	httpd_resp_send(req, resp, strlen(resp));
	return ESP_OK;
}

/* URI handler structure for GET /uri */
httpd_uri_t uri_get = {
	.uri      = "/hello",
	.method   = HTTP_GET,
	.handler  = get_handler,
	.user_ctx = NULL
};

httpd_uri_t uri_post = {
	.uri		= "/led",
	.method		= HTTP_POST,
	.handler	= led_handler,
	.user_ctx	= NULL
};

/* Function for starting the webserver */
httpd_handle_t start_webserver(void)
{
	/* Generate default configuration */
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	/* Empty handle to esp_http_server */
	httpd_handle_t server = NULL;

	/* Start the httpd server */
	if (httpd_start(&server, &config) == ESP_OK) {
		/* Register URI handlers */
		httpd_register_uri_handler(server, &uri_get);
		httpd_register_uri_handler(server, &uri_post);
	}
	/* If server failed to start, handle will be NULL */
	return server;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
	if (server) {
		/* Stop the httpd server */
		httpd_stop(server);
	}
}

void app_main()
{
	//Initialize NVS
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
	wifi_init_softap();
	httpd_handle_t serv = start_webserver();
	if (serv == NULL) {
		ESP_LOGE(TAG, "Failed to start web server");
	} else {
		ESP_LOGI(TAG, "Web server started!");
	}

	// Set function to GPIO, and direction to output
	gpio_iomux_out(LED_PIN, FUNC_GPIO5_GPIO5, false);
	gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
}
