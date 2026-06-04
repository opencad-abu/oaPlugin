ROOT	= ..
include $(ROOT)/build.variables

default : all

OA_INC ?= $(abspath $(ROOT)/../../oa22.61-devel/include)
OACPP ?= $(abspath $(ROOT)/../../oacpp)
OACPP_SRC ?= $(OACPP)/src
PROJ_INCLUDES := -I$(OA_INC) -I../../src/common -I../../src/plugIn -I../../src/base -I../../src/dm
PROJ_INCLUDES += -I$(OACPP_SRC)/common -I$(OACPP_SRC)/plugIn -I$(OACPP_SRC)/base -I$(OACPP_SRC)/dm
PROJ_NAME = oaAiviVC
LIB_LDNAMES := oaPlugIn oaCommon

-include local.rules
include $(ROOT)/src.rules
include Hfiles.defs
include $(ROOT)/lib.rules

GDM_WRAPPER_SRC = gdm/aivivc_gdm_wrap.c
GDM_ADAPTER_SCRIPT = gdm/aivivc-gdm
GDM_CONFIG_SCRIPT = gdm/aivivcgdmconfig
GDM_WRAPPER_LIBDIR = $(ROOT)/lib/aivivc/gdm/$(SYSNAME)/$(OPTMODE)
GDM_WRAPPER_SO = $(GDM_WRAPPER_LIBDIR)/libgdmaivivc_sh.so
GDM_WRAPPER_ADAPTER_INSTALLED = $(GDM_WRAPPER_LIBDIR)/aivivc-gdm
GDM_WRAPPER_CONFIG_INSTALLED = $(GDM_WRAPPER_LIBDIR)/aivivcgdmconfig
GDM_WRAPPER_ADAPTER = $(abspath $(GDM_WRAPPER_ADAPTER_INSTALLED))
GDM_WRAPPER_CFLAGS ?= -std=c99 -O2 -fPIC -Wall -Wextra
GDM_WRAPPER_LDFLAGS ?= -shared -ldl

.PHONY: gdm-native-wrapper gdm-native-wrapper-clean

all: gdm-native-wrapper
clean: gdm-native-wrapper-clean

gdm-native-wrapper: $(GDM_WRAPPER_SO) $(GDM_WRAPPER_ADAPTER_INSTALLED) $(GDM_WRAPPER_CONFIG_INSTALLED)

$(GDM_WRAPPER_SO): $(GDM_WRAPPER_SRC) $(GDM_ADAPTER_SCRIPT)
	mkdir -p $(GDM_WRAPPER_LIBDIR)
	$(CC) $(GDM_WRAPPER_CFLAGS) \
	    -DAIVIVC_GDM_ADAPTER_PATH=\"$(GDM_WRAPPER_ADAPTER)\" \
	    -o $@ $< $(GDM_WRAPPER_LDFLAGS)

$(GDM_WRAPPER_ADAPTER_INSTALLED): $(GDM_ADAPTER_SCRIPT)
	mkdir -p $(GDM_WRAPPER_LIBDIR)
	install -m 755 $< $@

$(GDM_WRAPPER_CONFIG_INSTALLED): $(GDM_CONFIG_SCRIPT)
	mkdir -p $(GDM_WRAPPER_LIBDIR)
	install -m 755 $< $@

gdm-native-wrapper-clean:
	$(RM) -f $(GDM_WRAPPER_SO) $(GDM_WRAPPER_ADAPTER_INSTALLED) $(GDM_WRAPPER_CONFIG_INSTALLED)
