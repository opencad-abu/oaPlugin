# ******************************************************************************
# GNUmakefile — Builds liboaAiviDMFileSys.so
# ******************************************************************************

ROOT	= ..

include $(ROOT)/build.variables

default : all

# Include paths: OA headers + our local base module
PROJ_INCLUDES := -I$(OA_INC) -I../aividmfilesysbase

PROJ_NAME = oaAiviDMFileSys
LIB_LDNAMES := oaAiviDMFileSysBase oaPlugIn oaCommon

# Our plugin has additional source files
LIB_SRC_EXTRA := oaAiviDMFileSysData.cpp oaAiviDMFileSysDynamic.cpp oaAiviDMFileSysLocking.cpp

-include local.rules

include $(ROOT)/src.rules
include Hfiles.defs
include $(ROOT)/lib.rules
