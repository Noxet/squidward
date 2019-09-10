#ifndef SQUIDWARD_DTLS_H
#define SQUIDWARD_DTLS_H

#define READ_TIMEOUT_MS 10000

#include <sys/time.h>

#include "esp_log.h"
#include "esp_system.h"

#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "mbedtls/timing.h"

#define mbedtls_malloc(a) malloc(a)
#define mbedtls_realloc(a,b) realloc(a,b)
#define mbedtls_strdup(a) strdup(a)

#define SERVER_PORT "1337"
#define SERVER_NAME "192.168.12.1"

#define MAX_RETRY	5
#define DEBUG_LEVEL	0

#include "squidward_esp_utils.h"

struct _hr_time
{
	struct timeval start;
};

extern const int CONNECTED_BIT;
extern const char *TAG;

void dtls_setup();
void dtls_teardown();
void dtls_write(char *msg);
void dtls_read();
unsigned long mbedtls_timing_get_timer(struct mbedtls_timing_hr_time *, int reset);
void mbedtls_timing_set_delay(void *data, uint32_t int_ms, uint32_t fin_ms);
int mbedtls_timing_get_delay(void *data);

#endif