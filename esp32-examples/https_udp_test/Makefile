#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := https-mbedtls

include $(IDF_PATH)/make/project.mk

server:
	openssl s_server -cert server-cert.pem -key server-key.pem -dtls1 -accept 1337
