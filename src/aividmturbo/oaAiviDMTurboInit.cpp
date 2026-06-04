// oaAiviDMTurboInit.cpp — Entry points

#include "oaDMTurbo.h"

namespace oaDMTurbo {

// Class ID strings for registration
static const char* kClassId                = "oaAiviDMTurbo";
static const char* kLegacyClassId          = "oaDMTurbo";
static const char* kPlugInClassId          = "oaAiviDMTurbo::PlugIn";
static const char* kLegacyPlugInClassId    = "oaDMTurbo::PlugIn";
static const char* kLibServerMgrId         = "oaAiviDMTurboLibServerMgr";
static const char* kLegacyLibServerMgrId   = "oaDMTurboLibServerMgr";
static const char* kLibServerMgrClassId    = "oaAiviDMTurbo::LibServerMgr";
static const char* kLegacyLibServerMgrClassId = "oaDMTurbo::LibServerMgr";

// Factory createInstance — defined in oaAiviDMTurboPlugIn.cpp

// LibServerMgr::Factory::createInstance
oa::oaUInt4 LibServerMgr::Factory::createInstance(
    oaCommon::IBase*, const oaCommon::Guid&, void**)
{
    return oaCommon::IBase::cOK;
}

} // namespace oaDMTurbo

namespace {

bool isPlugInClassId(const char* clsid)
{
    return clsid &&
           (strcmp(clsid, oaDMTurbo::kClassId) == 0 ||
            strcmp(clsid, oaDMTurbo::kLegacyClassId) == 0 ||
            strcmp(clsid, oaDMTurbo::kPlugInClassId) == 0 ||
            strcmp(clsid, oaDMTurbo::kLegacyPlugInClassId) == 0);
}

bool isLibServerMgrClassId(const char* clsid)
{
    return clsid &&
           (strcmp(clsid, oaDMTurbo::kLibServerMgrId) == 0 ||
            strcmp(clsid, oaDMTurbo::kLegacyLibServerMgrId) == 0 ||
            strcmp(clsid, oaDMTurbo::kLibServerMgrClassId) == 0 ||
            strcmp(clsid, oaDMTurbo::kLegacyLibServerMgrClassId) == 0);
}

void registerFactoryAliases()
{
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kClassId, &oaDMTurbo::PlugIn::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kLegacyClassId, &oaDMTurbo::PlugIn::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kPlugInClassId, &oaDMTurbo::PlugIn::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kLegacyPlugInClassId, &oaDMTurbo::PlugIn::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kLibServerMgrId, &oaDMTurbo::LibServerMgr::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kLegacyLibServerMgrId, &oaDMTurbo::LibServerMgr::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kLibServerMgrClassId, &oaDMTurbo::LibServerMgr::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        oaDMTurbo::kLegacyLibServerMgrClassId, &oaDMTurbo::LibServerMgr::factory);
}

} // namespace

// ============================================================================
// OA entry points
// ============================================================================
extern "C" void oaAiviDMTurboInit()
{
    registerFactoryAliases();
}

extern "C" void oaDMTurboInit()
{
    oaAiviDMTurboInit();
}

extern "C" void oaAiviDMTurboExit()
{
}

extern "C" void AiviDMTurboExit()
{
    oaAiviDMTurboExit();
}

extern "C" void DMTurboExit()
{
    oaAiviDMTurboExit();
}

extern "C" long getClassObject(const char* clsid, const oaCommon::Guid& iid, void** ppv)
{
    using namespace oaDMTurbo;
    
    if (!clsid || !ppv) return 1;
    *ppv = nullptr;
    
    if (isPlugInClassId(clsid)) {
        *ppv = &PlugIn::factory;
        return 0;
    }
    
    if (isLibServerMgrClassId(clsid)) {
        *ppv = &LibServerMgr::factory;
        return 0;
    }
    
    return 1; // Class not found
}
