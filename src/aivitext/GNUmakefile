ROOT	= ..
include $(ROOT)/build.variables
default : all
PROJ_INCLUDES := -I$(OA_INC)/oa -I$(OA_INC)
PROJ_NAME = oaAiviText
LIB_LDNAMES := oaDesign oaPlugIn oaCommon oaBase
-include local.rules
include $(ROOT)/src.rules
include Hfiles.defs
include $(ROOT)/lib.rules
