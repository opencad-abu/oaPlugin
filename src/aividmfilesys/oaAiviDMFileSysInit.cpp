// oaAiviDMFileSysInit.cpp
#include "oaDMFileSysComp.h"
#include <oa/oaCommonPlugInMgr.h>
#include <oa/oaCommonFactory.h>
#include <cstdlib>
#include <cstring>

namespace {

const char* kClassId = "oaAiviDMFileSys";
const char* kLegacyClassId = "oaDMFileSys";
const char* kSystemClassId = "oaAiviDMSystem";
const char* kLegacySystemClassId = "oaDMSystem";

bool isSupportedClassId(const char* classID)
{
    return classID &&
           (strcmp(classID, kClassId) == 0 ||
            strcmp(classID, kLegacyClassId) == 0 ||
            strcmp(classID, kSystemClassId) == 0 ||
            strcmp(classID, kLegacySystemClassId) == 0);
}

void registerFactoryAliases()
{
    oaCommon::oaPlugInMgr::registerFactory(
        kClassId, &oaDMFileSys::oaDMFileSysComp::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        kSystemClassId, &oaDMFileSys::oaDMFileSysComp::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        kLegacyClassId, &oaDMFileSys::oaDMFileSysComp::factory);
    oaCommon::oaPlugInMgr::registerFactory(
        kLegacySystemClassId, &oaDMFileSys::oaDMFileSysComp::factory);
}

void cleanUpOnExit()
{
    oaDMFileSys::FileLocking::get().cleanUpOnExit();
}

} // namespace

// ---------------------------------------------------------------------------
extern "C" void oaAiviDMFileSysInit()
{
    FILE* dbg = fopen("/tmp/dmfilesys_debug.log", "a");
    if (dbg) { fprintf(dbg, "oaAiviDMFileSysInit: called\n"); fclose(dbg); }
    registerFactoryAliases();
    oaDMFileSys::oaDMFileSysComp::exitHandlerSetUp = true;
    if (dbg) {
        dbg = fopen("/tmp/dmfilesys_debug.log", "a");
        if (dbg) { fprintf(dbg, "oaAiviDMFileSysInit: registered factories\n"); fclose(dbg); }
    }
}

// ---------------------------------------------------------------------------
extern "C" void FileSysExit()
{
    cleanUpOnExit();
}

extern "C" void oaDMFileSysInit()
{
    oaAiviDMFileSysInit();
}

extern "C" void oaAiviDMFileSysExit()
{
    cleanUpOnExit();
}

extern "C" void AiviDMFileSysExit()
{
    oaAiviDMFileSysExit();
}

// ---------------------------------------------------------------------------
// getClassObject — standard OA loader entry point
// Returns factory, OA then calls factory->createInstance() → ILib
// ---------------------------------------------------------------------------
extern "C" long getClassObject(const char* classID,
                                const oaCommon::Guid& interfaceID,
                                void** instance)
{
    FILE* dbg = fopen("/tmp/dmfilesys_debug.log", "a");
    if (dbg) {
        fprintf(dbg, "getClassObject: classID='%s' instance=%p\n",
                classID ? classID : "(null)", (void*)instance);
        fclose(dbg);
    }
    if (!instance || !isSupportedClassId(classID)) return 1;
    
    // Return the static factory
    *instance = &oaDMFileSys::oaDMFileSysComp::factory;
    if (dbg) {
        dbg = fopen("/tmp/dmfilesys_debug.log", "a");
        if (dbg) { fprintf(dbg, "getClassObject: returned factory OK\n"); fclose(dbg); }
    }
    return 0;
}
