// *****************************************************************************
// oaDMFileSysTypes.h — Internal types and constants
//
// Reverse-engineered from liboaDMFileSys.so v22.61.p005
// *****************************************************************************

#ifndef OADMFILESYS_TYPES_H
#define OADMFILESYS_TYPES_H

#include <string>
#include <map>
#include <cstdint>
#include <cstddef>

// Forward declarations from OA namespaces
namespace OpenAccess_4 {
    class oaString;
    class oaFile;
    class oaDir;
    class oaFSComponent;
    class oaSocket;
    class oaMemory;
}

namespace oaCommon {
    class Guid;
    class IBase;
    class IString;
    class IFactory;
    class IPlugInAbort;
    class ProcInfo;
}

namespace oaPlugIn {
    class IAttr;
    class IDMObject;
    class IDMFile;
    class IDMContainer;
    class ICell;
    class ICellView;
    class IView;
    class IPlugInMessage;
    class IDMAccess;
    class IDMLib;
    
    // oaLibModeEnum and oaSaveRecoverTypeEnum are defined in
    // <oa/oaPlugInDMTypes.h> — do not redefine here
    
    extern const oaCommon::Guid IID_ILib;
    extern const oaCommon::Guid IID_IDMAccess;
    extern const oaCommon::Guid IID_ILocking;
    extern const oaCommon::Guid IID_IAccessControl;
    extern const oaCommon::Guid IID_IDMSystemCaps;
    extern const oaCommon::Guid IID_IPlugInAbort;
    extern const oaCommon::Guid IID_IAttr;
}

namespace oaDMFileSys {

// ============================================================================
// Message IDs
// ============================================================================
enum oaDMFileSysMsgIds {
    // Error messages
    oacDMFileSysErrBase = 7000,
    oacLibExistsErr,
    oacLibNotFoundErr,
    oacCellExistsErr,
    oacCellNotFoundErr,
    oacViewExistsErr,
    oacViewNotFoundErr,
    oacCellViewExistsErr,
    oacCellViewNotFoundErr,
    oacFileExistsErr,
    oacFileNotFoundErr,
    oacLockFailErr,
    oacInvalidPathErr,
    oacAccessDeniedErr,
    oacNotALibErr,
    oacReadOnlyLibErr,
    
    // Info messages
    oacDMFileSysInfoBase = 7500,
    oacLibCreatedInfo,
    oacLibOpenedInfo,
};

// InfoMsg IDs are a subset of the main message ID enum
typedef oaDMFileSysMsgIds oaDMFileSysInfoMsgIds;

// ============================================================================
// String constants matching the original .so
// ============================================================================
extern const char* const masterTagHeader;
extern const char* const masterTagFileName;
extern const char* const libReadOnlyAttrName;
extern const char* const vcSystemAttrName;
extern const char* const unixFileSystemName;
extern const char* const windowsFileSystemName;
extern const char* const fileSystemAttrName;
extern const char* const vhdlFileName;
extern const char* const verilogFileName;
extern const char* const dataDMstr;
extern const char* const techDBstr;
extern const char* const fileExt;
extern const char* const cacheExt;
extern const char* const dmSystemKeyWord;
extern const char* const libraryKeyWord;
extern const char* const plugInFileName;
extern const char* const criticalSaveExt;
extern const char* const autoSaveExt;

// View type mapping: logical name → physical file extension
extern const char* const s_viewType;

// ============================================================================
// Physical/Logical name mapping
// ============================================================================
typedef std::map<std::string, std::string> NameMap;
extern NameMap physicalMap;
extern NameMap logicalMap;

// ============================================================================
// Master tag constants
// ============================================================================
constexpr int masterTagHeaderLen = 30;  // "-- Master.tag File, Rev:1.0"
constexpr int defaultOSNS = 1;          // cUnixNS

// Save/recover extensions
// autoSaveExt  = ".oa_" 
// criticalSaveExt = ".oa_save"

// ============================================================================
// Locking constants
// ============================================================================
constexpr const char* EX_LOCKFILE = ".cdslck";
constexpr const char* cPlatform = "Unix";
constexpr const char* cAppName = "OpenAccess edit lock";
constexpr const char* cVersion = "2.0";

constexpr unsigned int s_delayMicroSecToCheckFSDaemon = 100000;
constexpr unsigned int s_maxCountsToCheckFSDaemon = 100;
constexpr unsigned int minLinuxRhelLinkNum = 5;
constexpr unsigned int editLockFileExtLen = 7;  // ".cdslck"

constexpr const char* editLockFileExt = ".cdslck";
constexpr const char* editLockFileRhelLinkExt = ".cdslck.rhel";

} // namespace oaDMFileSys

#endif // OADMFILESYS_TYPES_H
