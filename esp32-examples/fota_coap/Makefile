#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := fota_coap

include $(IDF_PATH)/make/project.mk

server:
	cp ca_cert.pem build
	cp ca_key.pem build
	cd build && openssl s_server -WWW -key ca_key.pem -cert ca_cert.pem -port 8070
