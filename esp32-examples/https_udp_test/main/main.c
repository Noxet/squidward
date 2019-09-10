
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "squidward_esp_utils.h"
#include "squidward_dtls.h"


/* FreeRTOS event group to signal when we are connected & ready to make a request */
EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;

/* Debug tag */
const char *TAG = "mbedTLS-UDP";

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");


static void https_get_task(void *pvParameters)
{
	//char *msg = "This is a test 1 2 1 2";
	unsigned char buf[1024];

	// TODO: Add length to dtls_write, do NOT use strcpy
	char coap_get[] = {0x40, 0x01, 0x75, 0xd5, 0x72, 0x05, 0x3a, 0x00};

	dtls_setup();
	dtls_write(coap_get);
	dtls_read(buf, sizeof(buf));

	dtls_close();
	dtls_teardown();

	ESP_LOGI(TAG, "Received from server: %s", buf);

	while (1);
}


void app_main()
{
	ESP_ERROR_CHECK( nvs_flash_init() );
	initialise_wifi();
	xTaskCreate(&https_get_task, "https_get_task", 8192, NULL, 5, NULL);
}
