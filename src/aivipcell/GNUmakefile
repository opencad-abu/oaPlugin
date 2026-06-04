ROOT	= ..

default : all

PYTHON_VERSION ?= $(shell python3 -c "import sys; print('%d.%d' % sys.version_info[:2])")
OA_INC ?= $(abspath $(ROOT)/../../oa22.61-devel/include)
PYTHON_INCLUDE_DIR ?= $(shell python3 -c "import sysconfig; print(sysconfig.get_path('include') or '')")
PYTHON_LIBDIR ?= $(shell python3 -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR') or sysconfig.get_config_var('LIBPL') or '')")
PYTHON_LIBNAME ?= python$(PYTHON_VERSION)
PYTHON_EXTRA_LIBS ?= -ldl -lm

PROJ_INCLUDES := -I$(OA_INC) -I$(PYTHON_INCLUDE_DIR)
PROJ_NAME = oaAiviPcell
LIB_LDNAMES := oaDesign oaPlugIn oaCommon oaBase
LIB_LDFLAGS += $(if $(PYTHON_LIBDIR),-L$(PYTHON_LIBDIR))
LIB_LDFLAGS += $(if $(PYTHON_LIBNAME),-l$(PYTHON_LIBNAME))
LIB_LDFLAGS += $(PYTHON_EXTRA_LIBS)

include $(ROOT)/build.variables
-include local.rules
include $(ROOT)/src.rules
include Hfiles.defs
include $(ROOT)/lib.rules
