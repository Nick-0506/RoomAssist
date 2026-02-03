#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#
# NICK_FIX If move the folder form homekit-sdk to esp root, need to change the path

HOMEKIT_PATH ?= $(abspath $(shell pwd)/../esp-homekit-sdk)

COMMON_COMPONENT_PATH ?= $(abspath $(shell pwd)/../esp-homekit-sdk/common)

PROJECT_NAME := emulator
EXTRA_COMPONENT_DIRS += $(HOMEKIT_PATH)/components/
EXTRA_COMPONENT_DIRS += $(HOMEKIT_PATH)/components/homekit
EXTRA_COMPONENT_DIRS += $(COMMON_COMPONENT_PATH)

include $(IDF_PATH)/make/project.mk

