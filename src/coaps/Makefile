#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := coaps

EXTRA_COMPONENT_DIRS = $(SQUIDWARD_PATH)/components/sq_wifi
EXTRA_COMPONENT_DIRS += $(SQUIDWARD_PATH)/components/sq_coap
EXTRA_COMPONENT_DIRS += $(SQUIDWARD_PATH)/components/sq_uart

include $(IDF_PATH)/make/project.mk

CONFIGS = sdk-psk-nodbg sdk-psk-dbg

builds:
	mkdir -p binaries
	mkdir -p buildlogs
	for conf in $(CONFIGS) ; do \
		cp config/$$conf sdkconfig ; \
		$(MAKE) -j $(shell nproc) 2> buildlogs/$(PROJECT_NAME)-$$conf.log; \
		cp build/$(PROJECT_NAME).bin binaries/$(PROJECT_NAME)-$$conf.bin ; \
	done
	for conf in $(CONFIGS) ; do \
		[ -s buildlogs/$(PROJECT_NAME)-$$conf.log ] && echo -e "\e[33mThere is output in log file $(PROJECT_NAME)-$$conf.log\e[0m" || echo -n "" ; \
	done

cleanbuilds: clean
	rm -rf buildlogs binaries
