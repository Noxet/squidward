
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "nvs_flash.h"

#include "squidward/sq_wifi.h"
#include "squidward/sq_coap.h"

const int CONNECTED_BIT = BIT0;
EventGroupHandle_t wifi_event_group;

const char *TAG = "coaps_fota";

#ifdef CONFIG_COAP_MBEDTLS_PKI
/* CA cert, taken from coap_ca.pem
   Client cert, taken from coap_client.crt
   Client key, taken from coap_client.key

   The PEM, CRT and KEY file are examples taken from the wpa2 enterprise
   example.

   To embed it in the app binary, the PEM, CRT and KEY file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
 */
extern uint8_t ca_pem_start[] asm("_binary_coap_ca_pem_start");
extern uint8_t ca_pem_end[]   asm("_binary_coap_ca_pem_end");
extern uint8_t client_crt_start[] asm("_binary_coap_client_crt_start");
extern uint8_t client_crt_end[]   asm("_binary_coap_client_crt_end");
extern uint8_t client_key_start[] asm("_binary_coap_client_key_start");
extern uint8_t client_key_end[]   asm("_binary_coap_client_key_end");
#endif /* CONFIG_COAP_MBEDTLS_PKI */

void coap_message_handler(coap_context_t *ctx, coap_session_t *session,
                            coap_pdu_t *sent, coap_pdu_t *received,
                            const coap_tid_t id)
{
    unsigned char *data = NULL;
    size_t data_len;
    coap_pdu_t *pdu = NULL;
    coap_opt_t *block_opt;
    coap_opt_iterator_t opt_iter;
    unsigned char buf[4];
    coap_optlist_t *option;
    coap_tid_t tid;

    if (COAP_RESPONSE_CLASS(received->code) == 2) {
        /* Need to see if blocked response */
        block_opt = coap_check_option(received, COAP_OPTION_BLOCK2, &opt_iter);
        if (block_opt) {
            uint16_t blktype = opt_iter.type;

            if (coap_opt_block_num(block_opt) == 0) {
                printf("Received:\n");
            }
            if (coap_get_data(received, &data_len, &data)) {
                printf("%.*s", (int)data_len, data);
            }
            if (COAP_OPT_BLOCK_MORE(block_opt)) {
                /* more bit is set */

                /* create pdu with request for next block */
                pdu = coap_new_pdu(session);
                if (!pdu) {
                    ESP_LOGE(TAG, "coap_new_pdu() failed");
                    goto clean_up;
                }
                pdu->type = COAP_MESSAGE_CON;
                pdu->tid = coap_new_message_id(session);
                pdu->code = COAP_REQUEST_GET;

                /* add URI components from optlist */
                for (option = optlist; option; option = option->next ) {
                    switch (option->number) {
                    case COAP_OPTION_URI_HOST :
                    case COAP_OPTION_URI_PORT :
                    case COAP_OPTION_URI_PATH :
                    case COAP_OPTION_URI_QUERY :
                        coap_add_option(pdu, option->number, option->length,
                                        option->data);
                        break;
                    default:
                        ;     /* skip other options */
                    }
                }

                /* finally add updated block option from response, clear M bit */
                /* blocknr = (blocknr & 0xfffffff7) + 0x10; */
                coap_add_option(pdu,
                                blktype,
                                coap_encode_var_safe(buf, sizeof(buf),
                                                     ((coap_opt_block_num(block_opt) + 1) << 4) |
                                                     COAP_OPT_BLOCK_SZX(block_opt)), buf);

                tid = coap_send(session, pdu);

                if (tid != COAP_INVALID_TID) {
                    resp_wait = 1;
                    wait_ms = SQ_COAP_TIME_SEC * 1000;
                    return;
                }
            }
            printf("\n");
        } else {
            if (coap_get_data(received, &data_len, &data)) {
                printf("Received: %.*s\n", (int)data_len, data);
            }
        }
    }
clean_up:
    resp_wait = 0;
}



void app_main(void)
{
    ESP_ERROR_CHECK( nvs_flash_init() );
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init();

    xTaskCreate(coap_init, "coap", 8 * 1024, NULL, 5, NULL);
}
