
#include "squidward_dtls.h"

static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_ssl_config conf;
static mbedtls_x509_crt cacert;
static mbedtls_timing_delay_context timer;
static mbedtls_ssl_context ssl;
static mbedtls_net_context server_fd;

#define MESSAGE "This is a test 1 2 1 2"

void dtls_write(char *msg)
{
	int ret, len;

	ESP_LOGI(TAG, "  > Write to server:" );

	//len = strlen(msg);
	len = sizeof(MESSAGE) - 1;

	mbedtls_printf("SENDING - %s - with length - %d\n", msg, len);

	do ret = mbedtls_ssl_write( &ssl, (unsigned char *) MESSAGE, len );
	while( ret == MBEDTLS_ERR_SSL_WANT_READ ||
			ret == MBEDTLS_ERR_SSL_WANT_WRITE );

	if( ret < 0 )
	{
		mbedtls_printf( " failed\n  ! mbedtls_ssl_write returned %d\n\n", ret );
		goto teardown;
	}

	len = ret;
	ESP_LOGI(TAG, " %d bytes written\n\n%s\n\n", len, MESSAGE );
	goto exit;

teardown:
	dtls_teardown();

exit:
	return;
}

void dtls_read()
{
	int ret, len;
	unsigned char buf[1024];
	int retry_left = MAX_RETRY;

	ESP_LOGI(TAG, "  < Read from server:" );

	len = sizeof( buf ) - 1;
	memset( buf, 0, sizeof( buf ) );

	do ret = mbedtls_ssl_read( &ssl, buf, len );
	while( ret == MBEDTLS_ERR_SSL_WANT_READ ||
			ret == MBEDTLS_ERR_SSL_WANT_WRITE );

	if( ret <= 0 )
	{
		switch( ret )
		{
			case MBEDTLS_ERR_SSL_TIMEOUT:
				mbedtls_printf( " timeout\n\n" );
				if( retry_left-- > 0 ) {
					//goto send_request;
				}
				goto exit;

			case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
				mbedtls_printf( " connection was closed gracefully\n" );
				ret = 0;
				goto close_notify;

			default:
				mbedtls_printf( " mbedtls_ssl_read returned -0x%x\n\n", -ret );
				goto exit;
		}
	}

	len = ret;
	ESP_LOGI(TAG, " %d bytes read\n\n%s\n\n", len, buf );

close_notify:
	ESP_LOGI(TAG, "  . Closing the connection..." );

	/* No error checking, the connection might be closed already */
	do ret = mbedtls_ssl_close_notify( &ssl );
	while( ret == MBEDTLS_ERR_SSL_WANT_WRITE );
	ret = 0;

	ESP_LOGI(TAG, " done\n" );

	/*
	 * 9. Final clean-ups and exit
	 */
exit:
	dtls_teardown();

}

void dtls_teardown()
{
	mbedtls_net_free( &server_fd );
	mbedtls_x509_crt_free( &cacert );
	mbedtls_ssl_free( &ssl );
	mbedtls_ssl_config_free( &conf );
	mbedtls_ctr_drbg_free( &ctr_drbg );
	mbedtls_entropy_free( &entropy );
}

void dtls_setup()
{
	int ret;
	uint32_t flags;

	const char *pers = "dtls_client";

	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connected to AP");


	/*
	 * 0. Initialize the RNG and the session data
	 */
	mbedtls_net_init( &server_fd );
	mbedtls_ssl_init( &ssl );
	mbedtls_ssl_config_init( &conf );
	mbedtls_x509_crt_init( &cacert );
	mbedtls_ctr_drbg_init( &ctr_drbg );

	ESP_LOGI(TAG, "\n  . Seeding the random number generator..." );


	mbedtls_entropy_init( &entropy );
	if( ( ret = mbedtls_ctr_drbg_seed( &ctr_drbg, mbedtls_entropy_func, &entropy,
					(const unsigned char *) pers,
					strlen( pers ) ) ) != 0 )
	{
		ESP_LOGE(TAG, " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret );
		goto exit;
	}

	ESP_LOGI(TAG, " ok\n" );

	/*
	 * 0. Load certificates
	 */
	ESP_LOGI(TAG, "  . Loading the CA root certificate ..." );


	ret = mbedtls_x509_crt_parse( &cacert, (const unsigned char *) mbedtls_test_cas_pem,
			mbedtls_test_cas_pem_len );
	if( ret < 0 )
	{
		ESP_LOGE(TAG, " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n", -ret );
		goto exit;
	}

	ESP_LOGI(TAG, " ok (%d skipped)\n", ret );

	/*
	 * 1. Start the connection
	 */
	ESP_LOGI(TAG, "  . Connecting to udp/%s/%s...", SERVER_NAME, SERVER_PORT );


	if( ( ret = mbedtls_net_connect( &server_fd, SERVER_NAME,
					SERVER_PORT, MBEDTLS_NET_PROTO_UDP ) ) != 0 )
	{
		ESP_LOGE(TAG, " failed\n  ! mbedtls_net_connect returned %d\n\n", ret );
		goto exit;
	}

	ESP_LOGI(TAG, " ok\n" );

	/*
	 * 2. Setup stuff
	 */
	ESP_LOGI(TAG, "  . Setting up the DTLS structure..." );


	if( ( ret = mbedtls_ssl_config_defaults( &conf,
					MBEDTLS_SSL_IS_CLIENT,
					MBEDTLS_SSL_TRANSPORT_DATAGRAM,
					MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
	{
		ESP_LOGE(TAG, " failed\n  ! mbedtls_ssl_config_defaults returned %d\n\n", ret );
		goto exit;
	}

	/* OPTIONAL is usually a bad choice for security, but makes interop easier
	 * in this simplified example, in which the ca chain is hardcoded.
	 * Production code should set a proper ca chain and use REQUIRED. */
	mbedtls_ssl_conf_authmode( &conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
	mbedtls_ssl_conf_ca_chain( &conf, &cacert, NULL );
	mbedtls_ssl_conf_rng( &conf, mbedtls_ctr_drbg_random, &ctr_drbg );

	if( ( ret = mbedtls_ssl_setup( &ssl, &conf ) ) != 0 )
	{
		ESP_LOGE(TAG, " failed\n  ! mbedtls_ssl_setup returned %d\n\n", ret );
		goto exit;
	}

	if( ( ret = mbedtls_ssl_set_hostname( &ssl, SERVER_NAME ) ) != 0 )
	{
		ESP_LOGE(TAG, " failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n", ret );
		goto exit;
	}

	mbedtls_ssl_set_bio( &ssl, &server_fd,
			mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout );

	mbedtls_ssl_set_timer_cb( &ssl, &timer, mbedtls_timing_set_delay,
			mbedtls_timing_get_delay );

	ESP_LOGI(TAG, " ok\n" );

	/*
	 * 4. Handshake
	 */
	ESP_LOGI(TAG, "  . Performing the DTLS handshake..." );


	do ret = mbedtls_ssl_handshake( &ssl );
	while( ret == MBEDTLS_ERR_SSL_WANT_READ ||
			ret == MBEDTLS_ERR_SSL_WANT_WRITE );

	if( ret != 0 )
	{
		ESP_LOGE(TAG, " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n\n", -ret );
		goto exit;
	}

	ESP_LOGI(TAG, " ok\n" );

	/*
	 * 5. Verify the server certificate
	 */
	ESP_LOGI(TAG, "  . Verifying peer X.509 certificate..." );

	/* In real life, we would have used MBEDTLS_SSL_VERIFY_REQUIRED so that the
	 * handshake would not succeed if the peer's cert is bad.  Even if we used
	 * MBEDTLS_SSL_VERIFY_OPTIONAL, we would bail out here if ret != 0 */
	if( ( flags = mbedtls_ssl_get_verify_result( &ssl ) ) != 0 )
	{
		char vrfy_buf[512];

		ESP_LOGE(TAG, " failed\n" );

		mbedtls_x509_crt_verify_info( vrfy_buf, sizeof( vrfy_buf ), "  ! ", flags );

		ESP_LOGE(TAG, "%s\n", vrfy_buf );
	}
	else
		ESP_LOGI(TAG, " ok\n" );


	/*
	 * 8. Done, cleanly close the connection
	 */
	ESP_LOGI(TAG, "  . Closing the connection..." );

	/* No error checking, the connection might be closed already */
	do ret = mbedtls_ssl_close_notify( &ssl );
	while( ret == MBEDTLS_ERR_SSL_WANT_WRITE );
	ret = 0;

	ESP_LOGI(TAG, " done\n" );

	/*
	 * 9. Final clean-ups and exit
	 */
exit:

	mbedtls_net_free( &server_fd );

	mbedtls_x509_crt_free( &cacert );
	mbedtls_ssl_free( &ssl );
	mbedtls_ssl_config_free( &conf );
	mbedtls_ctr_drbg_free( &ctr_drbg );
	mbedtls_entropy_free( &entropy );
}


unsigned long mbedtls_timing_get_timer( struct mbedtls_timing_hr_time *val, int reset )
{
	struct _hr_time *t = (struct _hr_time *) val;

	if( reset )
	{
		gettimeofday( &t->start, NULL );
		return( 0 );
	}
	else
	{
		unsigned long delta;
		struct timeval now;
		gettimeofday( &now, NULL );
		delta = ( now.tv_sec  - t->start.tv_sec  ) * 1000ul
			+ ( now.tv_usec - t->start.tv_usec ) / 1000;
		return( delta );
	}
}

/*
 * Set delays to watch
 */
void mbedtls_timing_set_delay( void *data, uint32_t int_ms, uint32_t fin_ms )
{
	mbedtls_timing_delay_context *ctx = (mbedtls_timing_delay_context *) data;

	ctx->int_ms = int_ms;
	ctx->fin_ms = fin_ms;

	if( fin_ms != 0 )
		(void) mbedtls_timing_get_timer( &ctx->timer, 1 );
}

/*
 * Get number of delays expired
 */
int mbedtls_timing_get_delay( void *data )
{
	mbedtls_timing_delay_context *ctx = (mbedtls_timing_delay_context *) data;
	unsigned long elapsed_ms;

	if( ctx->fin_ms == 0 )
		return( -1 );

	elapsed_ms = mbedtls_timing_get_timer( &ctx->timer, 0 );

	if( elapsed_ms >= ctx->fin_ms )
		return( 2 );

	if( elapsed_ms >= ctx->int_ms )
		return( 1 );

	return( 0 );
}