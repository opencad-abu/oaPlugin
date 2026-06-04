
#ifndef OADMFILESYS_COMP_H
#define OADMFILESYS_COMP_H

// Real OA headers
#include <oa/oaCommonIBase.h>
#include <oa/oaCommonFactory.h>
#include <oa/oaCommonProcInfo.h>
#include <oa/oaPlugInDMTypes.h>
#include <oa/oaPlugInDMInterfaces.h>
#include <oa/oaPlugInIDMObject.h>
#include <oa/oaString.h>
#include <oa/oaPlugInMessageInterfaces.h>

#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <cstdio>
#include <map>

namespace oaCommon {
class StringImp;
}

namespace oaDMFileSys {

// Forward declarations
class Locking;
class FileLocking;

// ============================================================================
// String constants
// ============================================================================
extern const char* const masterTagHeader;
extern const char* const masterTagFileName;
extern const char* const autoSaveExt;
extern const char* const criticalSaveExt;
extern const char* const editLockFileExt;
extern const char* const editLockFileRhelLinkExt;
extern const char* const EX_LOCKFILE;

// ============================================================================
// oaDMFileSysComp — Main DM file system implementation
// Properly inherits from all OA plugin interfaces (diamond via virtual IBase)
// ============================================================================
class oaDMFileSysComp : public virtual oaPlugIn::ILib,
                        public virtual oaPlugIn::IDMAccess,
                        public virtual oaPlugIn::ILocking,
                        public virtual oaPlugIn::IAccessControl,
                        public virtual oaPlugIn::IDMSystemCaps,
                        public virtual oaCommon::IPlugInAbort {
public:
    oaDMFileSysComp();
    virtual ~oaDMFileSysComp();
    
    // IBase (virtual)
    unsigned long addRef() override;
    unsigned long getRefCount() override;
    unsigned long release() override;
    long queryInterface(const oaCommon::Guid& id, void** iPtr) override;
    

    // IDMAccess (virtual)
    void add(oaPlugIn::IDMObject* object, bool checkExistence = false) override;
    void addLeader(oaPlugIn::IDMFile* file) override;
    void create(oaPlugIn::IDMObject* object) override;
    void destroy(oaPlugIn::IDMObject* object) override;
    void getMappedName(oaPlugIn::IDMAccess::NameSpace fromNS, const char* fromName,
                       oaPlugIn::IDMAccess::NameSpace toNS, oaCommon::IString*& toName) override;
    oaCommon::SRef<oaPlugIn::IDMLib> getDMLib() override;
    
    // ILib (virtual) — continues below
    // ILib (virtual)
    void init(const char* libName, const char* libPath,
              oaPlugIn::oaLibModeEnum libMode,
              const char* writePath,
              oaPlugIn::IDMAccess* dmAccess,
              oaCommon::IIter<oaPlugIn::IAttr*, &oaPlugIn::IID_IAttr>* dmAttrs = nullptr) override;
    
    bool libExists(const char* libPath) override;
    void libPreCreate() override;
    void libPreOpen() override;
    void libPostOpen() override;
    void libPostCreate() override;
    void libPreClose() override;
    
    void cellCreate(const char* cellName) override;
    bool cellFind(const char* cellName) override;
    bool cellValidate(const char* cellName) override;
    void cellPreFind() override;
    void cellValidateDestroy(oaPlugIn::ICell* cell) override;
    void cellDestroy(oaPlugIn::ICell* cell) override;
    
    void viewCreate(const char* viewName, const char* viewType) override;
    bool viewFind(const char* viewName, const char* viewType) override;
    bool viewValidate(const char* viewName, const char* viewType) override;
    void viewPreFind(const char* viewName) override;
    void viewPreFind() override;
    void viewValidateDestroy(oaPlugIn::IView* view) override;
    void viewDestroy(oaPlugIn::IView* view) override;
    
    void cellViewCreate(const char* cName, const char* vName, const char* vtName) override;
    bool cellViewFind(const char* cName, const char* vName, const char* vtName) override;
    bool cellViewFindViewType(const char* cName, const char* vName, char* vtName) override;
    bool cellViewValidate(const char* cName, const char* vName, const char* vtName) override;
    void cellViewPreFind(oaPlugIn::ICell* cell) override;
    void cellViewPreFind(oaPlugIn::IView* view) override;
    void cellViewPreFind() override;
    void cellViewPreSetView(oaPlugIn::ICellView* cv, oaPlugIn::IView* view, oaPlugIn::IDMFile* newPrimary) override;
    void cellViewPostSetView(oaPlugIn::ICellView* cv) override;
    void cellViewValidateDestroy(oaPlugIn::ICellView* cv) override;
    void cellViewDestroy(oaPlugIn::ICellView* cv) override;
    
    void fileCreate(const char* name, oaPlugIn::IDMObject* parent, bool primary) override;
    bool fileFind(const char* name, oaPlugIn::IDMObject* parent) override;
    bool fileValidate(const char* name, oaPlugIn::IDMObject* parent) override;
    void filePreFind(oaPlugIn::IDMObject* parent) override;
    void filePreSetLeader(oaPlugIn::IDMFile* file, oaPlugIn::IDMFile* leader) override;
    void filePostSetLeader(oaPlugIn::IDMFile* file) override;
    void filePreSetName(oaPlugIn::IDMFile* file, const char* name) override;
    void filePostSetName(oaPlugIn::IDMFile* file) override;
    void fileValidateDestroy(oaPlugIn::IDMFile* file) override;
    void fileValidateDestroy(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    void fileDestroy(oaPlugIn::IDMFile* file) override;
    void fileDestroy(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    bool fileGetCache(oaPlugIn::IDMFile* file, oaCommon::IString*& path) override;
    void fileReleaseCache(oaPlugIn::IDMFile* file, const char* path) override;
    
    void getPath(oaPlugIn::IDMFile* file, oaCommon::IString*& path) override;
    void getTempFile(oaPlugIn::IDMObject* dmObject, bool sameFileSystem, oaCommon::IString*& path) override;
    void getDMSystemName(oaCommon::IString*& dmSystem) override;
    void getAttributes(oaCommon::IIter<oaPlugIn::IAttr*, &oaPlugIn::IID_IAttr>*& dmAttrs) override;
    void setAttributes(oaCommon::IIter<oaPlugIn::IAttr*, &oaPlugIn::IID_IAttr>* dmAttrs) override;
    
    void getPath(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type, oaCommon::IString*& path) override;
    void fileCreate(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    bool exists(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    
    // IAccessControl (virtual)
    bool getAccess(oaPlugIn::oaLibAccessEnum accessType, oa::oaUInt4 timeOut) override;
    void releaseAccess() override;
    
    // ILocking (virtual)
    bool lock(oaPlugIn::IDMFile* obj) override;
    void unlock(oaPlugIn::IDMFile* obj) override;
    oa::oaUInt4 getLockStatus(oaPlugIn::IDMFile* obj) override;
    void setPlugInMessage(oaPlugIn::IPlugInMessage* plugInMsg) override;
    
    // IDMSystemCaps (virtual)
    bool queryCapability(const char* name) override;
    bool getMetric(const char* name, double& metric) override;
    bool getMetric(const char* name, oa::oaUInt4& metric) override;
    
    // IPlugInAbort (virtual)
    void onAbort() override;
    
    // ---- Internal helpers ----
    std::string buildLibPath() const;
    std::string buildCellPath(const char* cellName) const;
    std::string buildCellViewPath(const char* cName, const char* vName) const;
    std::string buildFilePath(oaPlugIn::IDMObject* parent, const char* name) const;
    std::string buildFilePath(oaPlugIn::IDMFile* file) const;
    std::string buildPhysicalFilePath(oaPlugIn::IDMObject* parent, const char* logicalName) const;
    std::string buildSaveRecoverPath(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) const;
    std::string buildCachePath(oaPlugIn::IDMFile* file) const;
    std::string resolvePrimaryFileName(const std::string& cell, const std::string& view,
                                       const std::string& logicalName) const;
    bool getCachedFilePath(oaPlugIn::IDMFile* file, std::string& path) const;
    bool getCellViewNames(oaPlugIn::IDMFile* file, std::string& cell, std::string& view) const;
    bool isPrimaryLogicalName(const std::string& logicalName) const;
    void cacheFilePath(oaPlugIn::IDMFile* file, const std::string& path);
    oaCommon::IString* makePersistentString(const std::string& value) const;
    
    void createDirectory(const std::string& path);
    bool directoryExists(const std::string& path);
    void createDataDMFile(const std::string& dirPath);
    bool fileExistsOnDisk(const std::string& path);
    void writeFile(const std::string& path, const std::string& content);
    std::string readFile(const std::string& path);
    void collectDirectory(const std::string& dirPath, std::set<std::string>& entries);
    
    // Static members
    static class Factory : public oaCommon::IFactory {
    public:
        oa::oaUInt4 createInstance(oaCommon::IBase* parent, const oaCommon::Guid& id, void** instance) override;
        unsigned long addRef() override { return 1; }
        unsigned long release() override { return 1; }
        unsigned long getRefCount() override { return 1; }
        long queryInterface(const oaCommon::Guid& id, void** ptr) override {
            if (id == oaCommon::IID_IBase || id == oaCommon::IID_IFactory) {
                *ptr = static_cast<oaCommon::IFactory*>(this);
                return oaCommon::IBase::cOK;
            }
            *ptr = nullptr;
            return oaCommon::IBase::cNoInterface;
        }
    } factory;
    static bool exitHandlerSetUp;
    
private:
    unsigned long refCount_ = 0;
    std::string libName_;
    std::string libPath_;
    std::string writePath_;
    bool libReadOnly_ = false;
    Locking* locking_ = nullptr;
    oaPlugIn::IDMAccess* dmAccess_ = nullptr;
    oaCommon::ProcInfo procInfo_;
    std::string lastFilePath_;
    std::string currentCell_;
    std::string currentView_;
    std::map<std::string, std::string> viewTypes_;      // viewName -> viewType
    std::map<std::string, std::string> cellViewTypes_;  // cell/view -> viewType
    std::map<oaPlugIn::IDMFile*, std::string> filePaths_;
    std::map<oaPlugIn::ICell*, std::string> cellNames_;   // cell ptr → name cache
    std::map<oaPlugIn::IView*, std::string> viewNames_;   // view ptr → name cache
    mutable std::vector<std::unique_ptr<oaCommon::StringImp>> stringCache_;
    std::string hostName_;
    std::string accessLockPath_;
    bool accessLocked_ = false;
    unsigned int accessLockCount_ = 0;
};

// ============================================================================
// Locking — file locking implementation
// ============================================================================
class Locking {
public:
    Locking();
    ~Locking();
    
    bool lock(oaPlugIn::IDMFile* obj);
    void unlock(oaPlugIn::IDMFile* obj);
    unsigned int getLockStatus(oaPlugIn::IDMFile* obj);
    void setPlugInMessage(oaPlugIn::IPlugInMessage* msg);
    
    bool lockObj(oaPlugIn::ICell* cell) const;
    bool lockObj(oaPlugIn::IView* view) const;
    bool lockObj(oaPlugIn::IDMFile* file) const;
    bool lockObj(oaPlugIn::ICellView* cv) const;
    bool lockObj(oaPlugIn::IDMObject* obj, bool recursive) const;
    void unlockObj(oaPlugIn::ICell* cell) const;
    void unlockObj(oaPlugIn::IView* view) const;
    void unlockObj(oaPlugIn::IDMFile* file) const;
    void unlockObj(oaPlugIn::ICellView* cv) const;
    void unlockObj(oaPlugIn::IDMObject* obj) const;
    bool hasLockedFiles(oaPlugIn::ICell* cell, bool recursive) const;
    bool hasLockedFiles(oaPlugIn::IView* view, bool recursive) const;
    bool hasLockedFiles(oaPlugIn::ICellView* cv, bool recursive) const;
    bool hasLockedFiles(oaPlugIn::IDMObject* obj, bool recursive) const;
    bool isEditLockFile(const OpenAccess_4::oaString& name) const;
    unsigned int getLockStatusPvt(oaPlugIn::IDMFile* file) const;
    bool hasExclusiveLock(const std::string& path);
    bool exclusiveLock(const std::string& path);
    void exclusiveUnLock(const std::string& path);
    void renameLock(oaPlugIn::IDMFile* file, const std::string& newPath);
    std::string getLockFilePath(oaPlugIn::IDMFile* file) const;
    std::string getLockFilePath(const std::string& dataPath) const;
    void setLibPath(const std::string& path);
    
private:
    oaPlugIn::IPlugInMessage* plugInMsg_ = nullptr;
    std::string lockDir_;
};

// ============================================================================
// FileLocking — Singleton for process-level file locking  
// ============================================================================
class FileLocking {
public:
    static FileLocking& get();
    
    void start();
    void terminate(const char* path);
    bool isActive(const char* path);
    void load(FILE* fp, oaCommon::ProcInfo& procInfo);
    bool save(const char* path) const;
    bool getVersion(const char* path);
    void cleanUpOnExit();
    std::string getLoginName();
    bool isEditLockFile(const OpenAccess_4::oaString& path);
    bool isProcessAlive(unsigned int pid, const char* procName);
    bool isProcessAliveWithTimeout(unsigned int pid, unsigned int& timeout, const char* procName, unsigned int maxTimeout);
    long queryInterface(const oaCommon::Guid& id, void** iPtr);
    void setPlugInMessage(oaPlugIn::IPlugInMessage* msg);
    oaPlugIn::IPlugInMessage* getPlugInMessage();
    std::string getRhelLinkFileName(const char* path, const oaCommon::ProcInfo& procInfo, std::string& out);
    void removeRhelLink(const std::string& path, const oaCommon::ProcInfo& procInfo);
    bool lock(const std::string& path);
    void unlock(const std::string& path);
    unsigned int getLockStatus(const std::string& path);
    bool alive(const oaCommon::ProcInfo& procInfo);
    std::string getLockPath(const std::string& path);
    std::string getLockingProc(const std::string& path);
    bool recover(const std::string& path);
    bool openRhel(const char* path);
    void renameRhelLink(const std::string& oldPath, const std::string& newPath);
    
private:
    FileLocking();
    ~FileLocking();
    
    static std::string s_userName;
    static std::string loginName;
    static bool daemonStarted;
    oaPlugIn::IPlugInMessage* plugInMsg_ = nullptr;
};

} // namespace oaDMFileSys

#endif
