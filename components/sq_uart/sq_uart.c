
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "squidward/sq_uart.h"

#define SQ_UART_BAUDRATE	CONFIG_SQ_UART_BAUDRATE
#define CTRL_PIN			CONFIG_SQ_UART_CTRL_PIN

void sq_uart_init()
{
	uart_config_t uart_config = {
		.baud_rate = SQ_UART_BAUDRATE,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE
	};
	// Configure UART parameters
	ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
	ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));

	gpio_config_t io_conf = {
		.pin_bit_mask	= (1ULL << CTRL_PIN),
		.mode			= GPIO_MODE_OUTPUT,
		.pull_up_en		= 0,
		.pull_down_en	= 1,
		.intr_type		= GPIO_PIN_INTR_DISABLE
	};

	gpio_config(&io_conf);
}

void sq_uart_send(const char *data, size_t len)
{
#ifdef CONFIG_SQ_UART_DBG
	ESP_LOGI(TAG, "[%s] - Sending %d bytes of data: %s", __FUNCTION__, len, data);
#endif

	/* Turn on output switch for Otii, and transmit an array of bytes for annotation.
	 * Wait until TX buffer is empty, preventing bogus data to be sent.
	 */
	ESP_ERROR_CHECK(uart_wait_tx_done(UART_NUM_0, 100));
	gpio_set_level(CTRL_PIN, 1);
	int res = uart_write_bytes(UART_NUM_0, data, len);
	/* Again, wait until finished before turning off the output. */
	ESP_ERROR_CHECK(uart_wait_tx_done(UART_NUM_0, 100));
	gpio_set_level(CTRL_PIN, 0);

#ifdef CONFIG_SQ_UART_DBG
	if (res >= 0) {
		ESP_LOGI(TAG, "[%s] - Sent %d bytes of data", __FUNCTION__, res);
	} else {
		ESP_LOGI(TAG, "[%s] - Parameter error", __FUNCTION__);
	}
#endif
}
