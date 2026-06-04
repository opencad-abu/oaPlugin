MODULE_DIRS := \
	aividmfilesysbase \
	aividmfilesys \
	aividmturbobase \
	aividmturbo \
	aividmturbo_server \
	aivivc \
	aivipcell \
	aivilock \
	aivitext

ifeq ($(strip $(RECX_DIRS)),)
BUILD_DIRS := $(MODULE_DIRS)
else
BUILD_DIRS := $(RECX_DIRS)
endif

SUBMAKE_VARS :=
ifneq ($(strip $(OPTMODE)),)
SUBMAKE_VARS += OPTMODE="$(OPTMODE)"
endif
ifneq ($(strip $(SYSNAME)),)
SUBMAKE_VARS += SYSNAME="$(SYSNAME)"
endif

default: all

all clean install:
	@status=0; \
	for dir in $(BUILD_DIRS); do \
		if [ -f "$$dir/GNUmakefile" ]; then \
			$(MAKE) -C "$$dir" $@ $(SUBMAKE_VARS) || status=$$?; \
			if [ $$status -ne 0 ] && [ "$@" != "clean" ]; then \
				exit $$status; \
			fi; \
		fi; \
	done; \
	exit $$status

.PHONY: default all clean install
