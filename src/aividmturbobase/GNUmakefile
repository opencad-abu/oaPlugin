# ******************************************************************************
# GNUmakefile — Builds liboaAiviDMTurboBase.so
# ******************************************************************************

ROOT	= ..

include $(ROOT)/build.variables

default : all

PROJ_INCLUDES := -I$(OA_INC)/oa -I$(OA_INC) -I../aividmturbo

PROJ_NAME = oaAiviDMTurboBase
LIB_LDNAMES := oaCommon oaPlugIn

-include local.rules
include $(ROOT)/src.rules
include Hfiles.defs
include $(ROOT)/lib.rules
