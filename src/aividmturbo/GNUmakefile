# ******************************************************************************
# GNUmakefile — Builds liboaAiviDMTurbo.so
# ******************************************************************************

ROOT	= ..

include $(ROOT)/build.variables

default : all

PROJ_INCLUDES := -I$(OA_INC)/oa -I$(OA_INC)

PROJ_NAME = oaAiviDMTurbo
LIB_LDNAMES := oaAiviDMTurboBase oaCommon oaPlugIn oaBase oaDM

PROJ_CPPFILES = oaAiviDMTurboChannel.cpp oaAiviDMTurboData.cpp oaAiviDMTurboInit.cpp \
                oaAiviDMTurboMsg.cpp oaAiviDMTurboPlugIn.cpp oaAiviDMTurboServer.cpp \
                oaAiviDMTurboServerImpl.cpp oaAiviDMTurboStorage.cpp

-include local.rules
include $(ROOT)/src.rules
include Hfiles.defs
include $(ROOT)/lib.rules
