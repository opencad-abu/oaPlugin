// *****************************************************************************
// oaAiviDMTurboPlugIn.cpp — oaDMTurbo Plugin implementation
// 存储格式: lib.xml + f000000001 编号文件
// *****************************************************************************

#include "oaDMTurbo.h"
#include "oaDMTurboStorage.h"
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

namespace oaDMTurbo {

// Static Factory instance
PlugIn::Factory PlugIn::factory;

const char* PlugIn::getPlugInName() { return "oaAiviDMTurbo"; }

// IBase
unsigned long PlugIn::addRef() { return ++refCount_; }
unsigned long PlugIn::getRefCount() { return refCount_; }
unsigned long PlugIn::release() {
    if (refCount_ > 0) --refCount_;
    if (refCount_ == 0) { delete this; return 0; }
    return refCount_;
}

// queryInterface
long PlugIn::queryInterface(const oaCommon::Guid& id, void** iPtr) {
    if (!iPtr) return oaCommon::IBase::cFail;
    *iPtr = nullptr;

    if (id == oaPlugIn::IID_ILib || id.data1 == 0xd620948b) {
        *iPtr = static_cast<oaPlugIn::ILib*>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    if (id == oaPlugIn::IID_ILocking || id.data1 == 0x474f0519) {
        *iPtr = static_cast<oaPlugIn::ILocking*>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    if (id == oaPlugIn::IID_IDMSystemCaps || id.data1 == 0x2cc384a9) {
        *iPtr = static_cast<oaPlugIn::IDMSystemCaps*>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    if (id == oaPlugIn::IID_IAccessControl || id.data1 == 0xa5e6875f) {
        *iPtr = static_cast<oaPlugIn::IAccessControl*>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    if (id == oaPlugIn::IID_IDMAccess || id.data1 == 0x1d866f60) {
        *iPtr = static_cast<oaPlugIn::IDMAccess*>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    if (id == oaPlugIn::IID_IPlugInAbort || id.data1 == 0xaa880baf) {
        *iPtr = static_cast<oaCommon::IPlugInAbort*>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    return oaCommon::IBase::cNoInterface;
}

// IDMAccess stubs
void PlugIn::add(oaPlugIn::IDMObject* object, bool checkExistence) {}
void PlugIn::addLeader(oaPlugIn::IDMFile* file) {}
void PlugIn::create(oaPlugIn::IDMObject* object) {}
void PlugIn::destroy(oaPlugIn::IDMObject* object) {}
void PlugIn::getMappedName(oaPlugIn::IDMAccess::NameSpace fromNS, const char* fromName,
                            oaPlugIn::IDMAccess::NameSpace toNS, oaCommon::IString*& toName) {
    toName = nullptr;
}
oaCommon::SRef<oaPlugIn::IDMLib> PlugIn::getDMLib() {
    return oaCommon::SRef<oaPlugIn::IDMLib>(nullptr);
}

// IPlugInAbort
void PlugIn::onAbort() {}

// 指针安全验证: 检查地址范围
static bool isSafeStr(const char* p) {
    if (!p) return false;
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    // 过滤哨兵值 (0x2, 0xa, 0xffffffff 等)
    if (a < 0x10000) return false;
    if (a == 0xffffffff || a == 0xffffffffffffffffULL) return false;
    // x86_64 用户空间合法地址: 0x400000+ (堆) 或 0x7f... (栈/mmap)
    if (a >= 0x400000 && a < 0x7fffffffffffULL) return true;
    return false;
}

// ILib Lifecycle
void PlugIn::init(const char* libName, const char* libPath, oaPlugIn::oaLibModeEnum libMode,
                  const char* writePath, oaPlugIn::IDMAccess* dmAccess, oaPlugIn::IAttrIter* dmAttrs) {
    libName_ = isSafeStr(libName) ? libName : "";
    libPath_ = isSafeStr(libPath) ? libPath : "";
    writePath_ = isSafeStr(writePath) ? writePath : "";
    libReadOnly_ = (libMode == oaPlugIn::oacReadOnlyLibMode);
    dmAccess_ = dmAccess;
    channel_ = nullptr;
    dbLib_ = nullptr;
    storage_ = nullptr;
}

bool PlugIn::libExists(const char* libPath) {
    if (!libPath) return false;
    std::string xmlPath = std::string(libPath) + "/lib.xml";
    return access(xmlPath.c_str(), F_OK) == 0;
}

void PlugIn::libPreCreate() {
    mkdir(libPath_.c_str(), 0755);
    storage_ = new LibStorage(libName_, libPath_);
}

void PlugIn::libPostCreate() {
    if (storage_) {
        // 只创建 lib 级别的默认 DMData 文件（类似 oaDMFileSys 的 data.dm）
        // 注意：dmfile_lib 由 oaDMFile::create 在测试中显式创建，不在此预先创建
        storage_->writeLibXml();
    }
}

void PlugIn::libPreOpen() {
    storage_ = new LibStorage(libName_, libPath_);
    storage_->loadLibXml();
}

void PlugIn::libPostOpen() {}

void PlugIn::libPreClose() {
    if (storage_) {
        storage_->writeLibXml();
        delete storage_;
        storage_ = nullptr;
    }
}

// Cell operations
void PlugIn::cellCreate(const char* cellName) {
    if (storage_ && cellName) {
        storage_->addCell(cellName);
        storage_->addFile("cell", cellName, "DMData");
        storage_->writeLibXml();
    }
}

bool PlugIn::cellFind(const char* cellName) {
    return storage_ && cellName && storage_->hasCell(cellName);
}

bool PlugIn::cellValidate(const char* cellName) { return true; }
void PlugIn::cellPreFind() {}
void PlugIn::cellValidateDestroy(oaPlugIn::ICell* cell) {}
void PlugIn::cellDestroy(oaPlugIn::ICell* cell) {}

// View operations
void PlugIn::viewCreate(const char* viewName, const char* viewType) {
    if (storage_ && viewName && viewType) {
        storage_->addView(viewName, viewType);
        storage_->addFile("view", viewName, "DMData");
        storage_->writeLibXml();
    }
}

bool PlugIn::viewFind(const char* viewName, const char* viewType) {
    return storage_ && viewName && storage_->hasView(viewName);
}

bool PlugIn::viewValidate(const char* viewName, const char* viewType) { return true; }
void PlugIn::viewPreFind(const char* viewName) {}
void PlugIn::viewPreFind() {}
void PlugIn::viewValidateDestroy(oaPlugIn::IView* view) {}
void PlugIn::viewDestroy(oaPlugIn::IView* view) {}

// CellView operations
void PlugIn::cellViewCreate(const char* cName, const char* vName, const char* vtName) {
    if (storage_ && cName && vName && vtName) {
        storage_->addCellView(cName, vName, vtName);
        std::string parentKey = std::string(cName) + "/" + vName;
        storage_->addFile("cellview", parentKey, "DMData");
        storage_->addFile("cellview", parentKey, "netlist", true);
        storage_->writeLibXml();
    }
}

bool PlugIn::cellViewFind(const char* cName, const char* vName, const char* vtName) {
    return storage_ && cName && vName && storage_->hasCellView(cName, vName);
}

bool PlugIn::cellViewFindViewType(const char* cName, const char* vName, char* vtName) {
    if (storage_ && cName && vName) {
        CellViewRecord* cv = storage_->getCellView(cName, vName);
        if (cv && vtName) {
            strncpy(vtName, cv->viewType.c_str(), 255);
            vtName[255] = '\0';
            return true;
        }
    }
    return false;
}

bool PlugIn::cellViewValidate(const char* cName, const char* vName, const char* vtName) { return true; }
void PlugIn::cellViewPreFind(oaPlugIn::ICell* cell) {}
void PlugIn::cellViewPreFind(oaPlugIn::IView* view) {}
void PlugIn::cellViewPreFind() {}
void PlugIn::cellViewPreSetView(oaPlugIn::ICellView* cv, oaPlugIn::IView* view, oaPlugIn::IDMFile* newPrimary) {}
void PlugIn::cellViewPostSetView(oaPlugIn::ICellView* cv) {}
void PlugIn::cellViewValidateDestroy(oaPlugIn::ICellView* cv) {}
void PlugIn::cellViewDestroy(oaPlugIn::ICellView* cv) {}

// File operations
void PlugIn::fileCreate(const char* name, oaPlugIn::IDMObject* parent, bool primary) {
    if (storage_ && name) {
        // 分配文件编号并创建空文件
        unsigned int num = storage_->addFile("lib", "", name, primary);
        std::string fp = storage_->getFilePath(num);
        FILE* f = fopen(fp.c_str(), "w");
        if (f) fclose(f);
        storage_->writeLibXml();
    }
}

bool PlugIn::fileFind(const char* name, oaPlugIn::IDMObject* parent) {
    // 先检查 lib.xml 里是否有这个文件
    if (storage_ && name && storage_->findFile(name) >= 0) {
        // 再检查物理文件是否真的存在
        int num = storage_->findFile(name);
        std::string fp = storage_->getFilePath(num);
        return (access(fp.c_str(), F_OK) == 0);
    }
    return false;
}

bool PlugIn::fileValidate(const char* name, oaPlugIn::IDMObject* parent) { return true; }
void PlugIn::filePreFind(oaPlugIn::IDMObject* parent) {}
void PlugIn::filePreSetLeader(oaPlugIn::IDMFile* file, oaPlugIn::IDMFile* leader) {}
void PlugIn::filePostSetLeader(oaPlugIn::IDMFile* file) {}
void PlugIn::filePreSetName(oaPlugIn::IDMFile* file, const char* name) {}
void PlugIn::filePostSetName(oaPlugIn::IDMFile* file) {}
void PlugIn::fileValidateDestroy(oaPlugIn::IDMFile* file) {}
void PlugIn::fileDestroy(oaPlugIn::IDMFile* file) {}

bool PlugIn::fileGetCache(oaPlugIn::IDMFile* file, oaCommon::IString*& path) {
    path = nullptr;
    return false;
}

void PlugIn::fileReleaseCache(oaPlugIn::IDMFile* file, const char* path) {}

void PlugIn::getPath(oaPlugIn::IDMFile* file, oaCommon::IString*& path) {
    if (!storage_ || !file) {
        path = nullptr;
        return;
    }
    
    // 从 IDMFile 获取文件名
    const char* fname = "";
    oaCommon::SRef<oaCommon::IString> nameRef = file->getName();
    if (nameRef) {
        fname = static_cast<const char*>(nameRef);
    }
    
    // 查找文件编号，返回编号文件路径 (RELATIVE to libPath)
    int num = storage_->findFile(fname);
    if (num >= 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "f%09u", num);
        static std::string cache;
        static oaCommon::StringImp* cached = nullptr;
        cache = buf;
        delete cached;
        cached = new oaCommon::StringImp(cache.c_str());
        path = cached;
    } else {
        path = nullptr;
    }
}

void PlugIn::getPath(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type, oaCommon::IString*& path) {
    path = nullptr;
}

void PlugIn::getTempFile(oaPlugIn::IDMObject* dmObject, bool sameFileSystem, oaCommon::IString*& path) {
    path = nullptr;
}

bool PlugIn::exists(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) {
    return false;
}

void PlugIn::fileCreate(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) {}
void PlugIn::fileValidateDestroy(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) {}
void PlugIn::fileDestroy(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) {}

// System
void PlugIn::getDMSystemName(oaCommon::IString*& dmSystem) {
    static oaCommon::StringImp name("oaAiviDMTurbo");
    dmSystem = &name;
}

void PlugIn::getAttributes(oaPlugIn::IAttrIter*& dmAttrs) { dmAttrs = nullptr; }
void PlugIn::setAttributes(oaPlugIn::IAttrIter* dmAttrs) {}

// ILocking
bool PlugIn::lock(oaPlugIn::IDMFile* obj) { return true; }
void PlugIn::unlock(oaPlugIn::IDMFile* obj) {}
oa::oaUInt4 PlugIn::getLockStatus(oaPlugIn::IDMFile* obj) { return 0; }
void PlugIn::setPlugInMessage(oaPlugIn::IPlugInMessage* plugInMsgIn) {}

// IDMSystemCaps
bool PlugIn::queryCapability(const char* name) { return false; }
bool PlugIn::getMetric(const char* name, oa::oaUInt4& metric) { return false; }
bool PlugIn::getMetric(const char* name, double& metric) { return false; }

// IAccessControl
bool PlugIn::getAccess(oaPlugIn::oaLibAccessEnum accessType, oa::oaUInt4 timeOut) { return true; }
void PlugIn::releaseAccess() {}

// Factory
oa::oaUInt4 PlugIn::Factory::createInstance(oaCommon::IBase*, const oaCommon::Guid& id, void** iPtr) {
    if (!iPtr) return oaCommon::IBase::cFail;
    *iPtr = nullptr;

    if (id == oaPlugIn::IID_ILib) {
        PlugIn* plugIn = new PlugIn();
        plugIn->refCount_ = 1;
        plugIn->libReadOnly_ = false;
        plugIn->dbLib_ = nullptr;
        plugIn->channel_ = nullptr;
        plugIn->dmAccess_ = nullptr;
        plugIn->storage_ = nullptr;
        *iPtr = static_cast<oaPlugIn::ILib*>(plugIn);
        return oaCommon::IBase::cOK;
    }
    return oaCommon::IBase::cNoInterface;
}

} // namespace oaDMTurbo
