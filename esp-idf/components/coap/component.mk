#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS := port/include port/include/coap libcoap/include libcoap/include/coap2 libcoap/ext/tinydtls libcoap/ext/tinydtls/aes

COMPONENT_OBJS = libcoap/src/address.o libcoap/src/async.o libcoap/src/block.o libcoap/src/coap_event.o libcoap/src/coap_hashkey.o libcoap/src/coap_session.o libcoap/src/coap_time.o libcoap/src/coap_debug.o libcoap/src/encode.o libcoap/src/mem.o libcoap/src/net.o libcoap/src/option.o libcoap/src/pdu.o libcoap/src/resource.o libcoap/src/str.o libcoap/src/subscribe.o libcoap/src/uri.o libcoap/src/coap_tinydtls.o port/coap_io.o \
libcoap/ext/tinydtls/ccm.o libcoap/ext/tinydtls/crypto.o libcoap/ext/tinydtls/dtls.o libcoap/ext/tinydtls/dtls_debug.o libcoap/ext/tinydtls/dtls_prng.o libcoap/ext/tinydtls/dtls_time.o libcoap/ext/tinydtls/hmac.o libcoap/ext/tinydtls/netq.o libcoap/ext/tinydtls/peer.o libcoap/ext/tinydtls/session.o \
port/rijndael.o libcoap/ext/tinydtls/ecc/ecc.o

COMPONENT_SRCDIRS := libcoap/ext/tinydtls/aes libcoap/ext/tinydtls/ecc libcoap/ext/tinydtls/sha2 libcoap/ext/tinydtls libcoap/src libcoap port

COMPONENT_SUBMODULES += libcoap

# Silence format truncation warning, until it is fixed upstream
libcoap/src/coap_debug.o: CFLAGS += -Wno-format-truncation
