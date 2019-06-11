set(COMPONENT_ADD_INCLUDEDIRS port/include port/include/coap libcoap/include libcoap/include/coap2 libcoap/ext/tinydtls libcoap/ext/tinydtls/aes)

set(COMPONENT_SRCS "libcoap/src/address.c"
                   "libcoap/src/async.c"
                   "libcoap/src/block.c"
                   "libcoap/src/coap_event.c"
                   "libcoap/src/coap_hashkey.c"
                   "libcoap/src/coap_session.c"
                   "libcoap/src/coap_time.c"
                   "libcoap/src/coap_debug.c"
                   "libcoap/src/encode.c"
                   "libcoap/src/mem.c"
                   "libcoap/src/net.c"
                   "libcoap/src/option.c"
                   "libcoap/src/pdu.c"
                   "libcoap/src/resource.c"
                   "libcoap/src/str.c"
                   "libcoap/src/subscribe.c"
                   "libcoap/src/uri.c"
                   "libcoap/src/coap_tinydtls.c"
                   "port/coap_io.c"
                   "libcoap/ext/tinydtls/ccm.c"
                   "libcoap/ext/tinydtls/crypto.c"
                   "libcoap/ext/tinydtls/dtls.c"
                   "libcoap/ext/tinydtls/dtls_debug.c"
                   "libcoap/ext/tinydtls/dtls_prng.c"
                   "libcoap/ext/tinydtls/dtls_time.c"
                   "libcoap/ext/tinydtls/hmac.c"
                   "libcoap/ext/tinydtls/netq.c"
                   "libcoap/ext/tinydtls/peer.c"
                   "libcoap/ext/tinydtls/session.c"
                   "port/rijndael.c"
                   "libcoap/ext/tinydtls/ecc/ecc.c")

set(COMPONENT_REQUIRES lwip)

register_component()

# Silence format truncation warning, until it is fixed upstream
set_source_files_properties(libcoap/src/coap_debug.c PROPERTIES COMPILE_FLAGS -Wno-format-truncation)
