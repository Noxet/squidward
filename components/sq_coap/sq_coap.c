
#include "squidward/sq_coap.h"
#include "squidward/sq_uart.h"

#include <sys/param.h>

const char ant_setup_plain[]	= "Setting up plain conn";
const char ant_setup_psk[]		= "Setting up PSK conn";
const char ant_setup_pki[]		= "Setting up PKI conn";
const char ant_setup_done[]		= "Setup done";

#ifdef CONFIG_COAP_MBEDTLS_PKI

int verify_cn_callback(const char *cn,
				   const uint8_t *asn1_public_cert,
				   size_t asn1_length,
				   coap_session_t *session,
				   unsigned depth,
				   int validated,
				   void *arg
				  )
{
	coap_log(LOG_INFO, "CN '%s' presented by server (%s)\n",
			 cn, depth ? "CA" : "Certificate");
	return 1;
}
#endif /* CONFIG_COAP_MBEDTLS_PKI */

/**
 * @brief Initialize a CoAP session.
 * 
 * Set up a CoAPs connection using mbedTLS.
 * @param[out] ctx		A pointer to a CoAP context pointer, default value should be NULL.
 * @param[out] session	A pointer to a CoAP session pointer, default value should be NULL.
 */
int sq_coap_init(coap_context_t **ctx, coap_session_t **session)
{
#ifdef CONFIG_SQ_COAP_DBG
	ESP_LOGD(TAG, "[%s] - Entered function", __FUNCTION__);
#endif

	struct hostent *hp;
	coap_address_t    dst_addr;
	static coap_uri_t uri;
	const char       *server_uri = SQ_COAP_URI;
	char *phostname = NULL;

	coap_set_log_level(SQ_COAP_LOG_LEVEL);

#define BUFSIZE 40
	unsigned char _buf[BUFSIZE];
	unsigned char *buf;
	size_t buflen;
	int res;

	/* TODO: remove
	ESP_LOGI(TAG, "Connecting to WiFi...");
	xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
	ESP_LOGI(TAG, "Connected to AP");
	*/

	optlist = NULL;
	if (coap_split_uri((const uint8_t *)server_uri, strlen(server_uri), &uri) == -1) {
		ESP_LOGE(TAG, "CoAP server uri error");
		return SQ_COAP_ERR_FAIL;
	}

	if (uri.scheme == COAP_URI_SCHEME_COAPS && !coap_dtls_is_supported()) {
		ESP_LOGE(TAG, "MbedTLS (D)TLS Client Mode not configured");
		return SQ_COAP_ERR_FAIL;
	}
	if (uri.scheme == COAP_URI_SCHEME_COAPS_TCP && !coap_tls_is_supported()) {
		ESP_LOGE(TAG, "CoAP server uri coaps+tcp:// scheme is not supported");
		return SQ_COAP_ERR_FAIL;
	}

	phostname = (char *) calloc(1, uri.host.length + 1);
	if (phostname == NULL) {
		ESP_LOGE(TAG, "calloc failed");
		return SQ_COAP_ERR_FAIL;
	}

	memcpy(phostname, uri.host.s, uri.host.length);
	hp = gethostbyname(phostname);
	free(phostname);

	if (hp == NULL) {
		ESP_LOGE(TAG, "DNS lookup failed");
		free(phostname);
		return SQ_COAP_ERR_DNS;
	}
	char tmpbuf[INET6_ADDRSTRLEN];
	coap_address_init(&dst_addr);
	switch (hp->h_addrtype) {
		case AF_INET:
			dst_addr.addr.sin.sin_family      = AF_INET;
			dst_addr.addr.sin.sin_port        = htons(uri.port);
			memcpy(&dst_addr.addr.sin.sin_addr, hp->h_addr, sizeof(dst_addr.addr.sin.sin_addr));
			inet_ntop(AF_INET, &dst_addr.addr.sin.sin_addr, tmpbuf, sizeof(tmpbuf));
			ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", tmpbuf);
			break;
		case AF_INET6:
			dst_addr.addr.sin6.sin6_family      = AF_INET6;
			dst_addr.addr.sin6.sin6_port        = htons(uri.port);
			memcpy(&dst_addr.addr.sin6.sin6_addr, hp->h_addr, sizeof(dst_addr.addr.sin6.sin6_addr));
			inet_ntop(AF_INET6, &dst_addr.addr.sin6.sin6_addr, tmpbuf, sizeof(tmpbuf));
			ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", tmpbuf);
			break;
		default:
			ESP_LOGE(TAG, "DNS lookup response failed");
			sq_coap_cleanup(*ctx, *session);
			return SQ_COAP_ERR_FAIL;
	}

	if (uri.path.length) {
		buflen = BUFSIZE;
		buf = _buf;
		res = coap_split_path(uri.path.s, uri.path.length, buf, &buflen);

		while (res--) {
			coap_insert_optlist(&optlist,
								coap_new_optlist(COAP_OPTION_URI_PATH,
													coap_opt_length(buf),
													coap_opt_value(buf)));

			buf += coap_opt_size(buf);
		}
	}

	if (uri.query.length) {
		buflen = BUFSIZE;
		buf = _buf;
		res = coap_split_query(uri.query.s, uri.query.length, buf, &buflen);

		while (res--) {
			coap_insert_optlist(&optlist,
								coap_new_optlist(COAP_OPTION_URI_QUERY,
													coap_opt_length(buf),
													coap_opt_value(buf)));

			buf += coap_opt_size(buf);
		}
	}

	*ctx = coap_new_context(NULL);
	if (!*ctx) {
		ESP_LOGE(TAG, "coap_new_context() failed");
		sq_coap_cleanup(*ctx, *session);
		return SQ_COAP_ERR_FAIL;
	}

#ifdef CONFIG_SQ_COAP_DBG
	ESP_LOGI(TAG, "[%s] - context created at %p", __FUNCTION__, *ctx);
#endif

	/*
		* Note that if the URI starts with just coap:// (not coaps://) the
		* session will still be plain text.
		*
		* coaps+tcp:// is NOT supported by the libcoap->mbedtls interface
		* so COAP_URI_SCHEME_COAPS_TCP will have failed in a test above,
		* but the code is left in for completeness.
		*/
	if (uri.scheme == COAP_URI_SCHEME_COAPS || uri.scheme == COAP_URI_SCHEME_COAPS_TCP) {
#ifndef CONFIG_MBEDTLS_TLS_CLIENT
		ESP_LOGE(TAG, "MbedTLS (D)TLS Client Mode not configured");
		sq_coap_cleanup(*ctx, *session);
		return SQ_COAP_ERR_FAIL;
#endif /* CONFIG_MBEDTLS_TLS_CLIENT */

#ifdef CONFIG_COAP_MBEDTLS_PSK
		sq_uart_send(ant_setup_psk, sizeof(ant_setup_psk));
		*session = coap_new_client_session_psk(*ctx, NULL, &dst_addr,
												uri.scheme == COAP_URI_SCHEME_COAPS ? COAP_PROTO_DTLS : COAP_PROTO_TLS,
												SQ_COAP_PSK_IDENTITY,
												(const uint8_t *) SQ_COAP_PSK_KEY,
												sizeof(SQ_COAP_PSK_KEY) - 1);
		sq_uart_send(ant_setup_done, sizeof(ant_setup_done));
#endif /* CONFIG_COAP_MBEDTLS_PSK */

#ifdef CONFIG_COAP_MBEDTLS_PKI
		unsigned int ca_pem_bytes = ca_pem_end - ca_pem_start;
		unsigned int client_crt_bytes = client_crt_end - client_crt_start;
		unsigned int client_key_bytes = client_key_end - client_key_start;
		coap_dtls_pki_t dtls_pki;
		static char client_sni[256];

		memset (&dtls_pki, 0, sizeof(dtls_pki));
		dtls_pki.version = COAP_DTLS_PKI_SETUP_VERSION;
		if (ca_pem_bytes) {
			/*
				* Add in additional certificate checking.
				* This list of enabled can be tuned for the specific
				* requirements - see 'man coap_encryption'.
				*
				* Note: A list of root ca file can be setup separately using
				* coap_context_set_pki_root_cas(), but the below is used to
				* define what checking actually takes place.
				*/
			dtls_pki.verify_peer_cert        = 1;
			dtls_pki.require_peer_cert       = 1;
			dtls_pki.allow_self_signed       = 1;
			dtls_pki.allow_expired_certs     = 1;
			dtls_pki.cert_chain_validation   = 1;
			dtls_pki.cert_chain_verify_depth = 2;
			dtls_pki.check_cert_revocation   = 1;
			dtls_pki.allow_no_crl            = 1;
			dtls_pki.allow_expired_crl       = 1;
			dtls_pki.allow_bad_md_hash       = 1;
			dtls_pki.allow_short_rsa_length  = 1;
			dtls_pki.validate_cn_call_back   = verify_cn_callback;
			dtls_pki.cn_call_back_arg        = NULL;
			dtls_pki.validate_sni_call_back  = NULL;
			dtls_pki.sni_call_back_arg       = NULL;
			memset(client_sni, 0, sizeof(client_sni));
			if (uri.host.length) {
				memcpy(client_sni, uri.host.s, MIN(uri.host.length, sizeof(client_sni)));
			} else {
				memcpy(client_sni, "localhost", 9);
			}
			dtls_pki.client_sni = client_sni;
		}
		dtls_pki.pki_key.key_type = COAP_PKI_KEY_PEM_BUF;
		dtls_pki.pki_key.key.pem_buf.public_cert = client_crt_start;
		dtls_pki.pki_key.key.pem_buf.public_cert_len = client_crt_bytes;
		dtls_pki.pki_key.key.pem_buf.private_key = client_key_start;
		dtls_pki.pki_key.key.pem_buf.private_key_len = client_key_bytes;
		dtls_pki.pki_key.key.pem_buf.ca_cert = ca_pem_start;
		dtls_pki.pki_key.key.pem_buf.ca_cert_len = ca_pem_bytes;

		sq_uart_send(ant_setup_pki, sizeof(ant_setup_pki));
		*session = coap_new_client_session_pki(*ctx, NULL, &dst_addr,
												uri.scheme == COAP_URI_SCHEME_COAPS ? COAP_PROTO_DTLS : COAP_PROTO_TLS,
												&dtls_pki);
		sq_uart_send(ant_setup_done, sizeof(ant_setup_done));
#endif /* CONFIG_COAP_MBEDTLS_PKI */
	} else {
		sq_uart_send(ant_setup_plain, sizeof(ant_setup_plain));
		*session = coap_new_client_session(*ctx, NULL, &dst_addr,
											uri.scheme == COAP_URI_SCHEME_COAP_TCP ? COAP_PROTO_TCP :
											COAP_PROTO_UDP);
		sq_uart_send(ant_setup_done, sizeof(ant_setup_done));
	}
	if (!*session) {
		ESP_LOGE(TAG, "coap_new_client_session() failed");
		sq_coap_cleanup(*ctx, *session);
		return SQ_COAP_ERR_FAIL;
	}

#ifdef CONFIG_SQ_COAP_DBG
	ESP_LOGI(TAG, "[%s] - session created at %p", __FUNCTION__, *session);
#endif

	return SQ_COAP_OK;
}

void sq_coap_cleanup(coap_context_t *ctx, coap_session_t *session)
{
#ifdef CONFIG_SQ_COAP_DBG
	ESP_LOGI(TAG, "[%s] - Entered function", __FUNCTION__);
#endif
	if (optlist) {
		coap_delete_optlist(optlist);
		optlist = NULL;
	}
	if (session) {
		coap_session_release(session);
	}
	if (ctx) {
		coap_free_context(ctx);
	}
	coap_cleanup();
}
