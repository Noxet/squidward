/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
   */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#define LED 18

void app_main()
{
	// configure LED as output
	gpio_config_t io_conf = {
		.pin_bit_mask 	= (1 << LED),
		.mode		= GPIO_MODE_OUTPUT,
		.pull_up_en	= 0,
		.pull_down_en	= 0,
		.intr_type	= GPIO_INTR_DISABLE
	};

	gpio_config(&io_conf);

	while(1) {
		/* Blink off (output low) */
		printf("Turning off the LED\n");
		gpio_set_level(LED, 0);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
		/* Blink on (output high) */
		printf("Turning on the LED\n");
		gpio_set_level(LED, 1);
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
}
