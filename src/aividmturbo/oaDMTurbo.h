// *****************************************************************************
// oaDMTurbo.h — Main header for liboaDMTurbo
//
// Client-server high-performance DM system
// Architecture:
//   PlugIn (client) ←→ CSMsg/IChannel ←→ LibServer (server process)
//   Memory DB: DMLib → DMCell → DMView → DMCellView → DMFile
//
// IMPORTANT: This implementation uses OA standard plugin interfaces.
//            PlugIn inherits from oaPlugIn::ILib and implements all 48 methods.
// *****************************************************************************

#ifndef OADMTURBO_H
#define OADMTURBO_H

// ============================================================================
// OA Standard Headers
// ============================================================================
#include "oaPlugIn.h"
#include "oaPlugInDMInterfaces.h"
#include "oaPlugInIDMObject.h"
#include "oaPlugInDMTypes.h"
#include "oaException.h"

// ============================================================================
// Standard C++ Headers
// ============================================================================
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <cstring>

namespace oaDMTurbo {

// ============================================================================
// Message IDs — Our own protocol (not OA standard)
// ============================================================================
enum CSMsgId {
    kMsgNone = 0,
    // Library operations
    kMsgCreateLib = 1,
    kMsgOpenLib = 2,
    kMsgCloseLib = 3,
    kMsgLibExists = 4,
    // Cell operations
    kMsgCreateCell = 10,
    kMsgFindCell = 11,
    kMsgDestroyCell = 12,
    // View operations
    kMsgCreateView = 20,
    kMsgDestroyView = 21,
    // CellView operations
    kMsgCreateCellView = 30,
    kMsgFindCellView = 31,
    kMsgDestroyCellView = 32,
    // File operations
    kMsgCreateFile = 40,
    kMsgFindFile = 41,
    kMsgDestroyFile = 42,
    kMsgGetPath = 43,
    kMsgGetTempFile = 44,
    kMsgExists = 45,
    // Locking
    kMsgLock = 50,
    kMsgUnlock = 51,
    kMsgHasLockedFiles = 52,
    // Persistence
    kMsgSave = 60,
    kMsgLoad = 61,
    // Response
    kMsgOK = 100,
    kMsgError = 101,
    // Control
    kMsgPing = 110,
    kMsgPong = 111,
    kMsgShutdown = 112,
};

enum oaDMTurboMsgIds {
    oacDMTurboErrBase = 8000,
    oacDMTurboServerNotFound = 8004,
    oacDMTurboConnectionFailed = 8005,
    oacDMTurboProtocolError = 8006,
};

// ============================================================================
// Forward declarations
// ============================================================================
class IChannel;
class Stream;
class StringTbl;
class DMCellView;
class DMView;
class VLogRun;

// ============================================================================
// Exception hierarchy
// ============================================================================
class Exception {
public:
    Exception(oaDMTurboMsgIds id) : msgId_(id) {}
    virtual ~Exception() {}
    virtual const char* getMsgsTable() const;
    virtual int getMsgIdStartValue() const;
    virtual int getMsgIdEndValue() const;
protected:
    oaDMTurboMsgIds msgId_;
};

class Error : public Exception {
public:
    Error(oaDMTurboMsgIds id, ...);
    Error(const oa::oaException& ex);
    virtual ~Error() {}
};

class OSError : public Error {
public:
    OSError(oaDMTurboMsgIds id, ...) : Error(id) {}
    virtual ~OSError() {}
};

class SocketError : public OSError {
public:
    SocketError();
    virtual ~SocketError() {}
};

// ============================================================================
// IChannel — Communication channel abstraction (our own design)
// ============================================================================
class IChannel {
public:
    virtual ~IChannel() {}
    virtual int getPortNumber() const = 0;
    virtual bool send(const char* data, int len) = 0;
    virtual int  recv(char* buf, int maxLen) = 0;

    // Factory methods
    static IChannel* connectToServer(const char* host, int port);
    static int startServer(int port = 0);
};

// ============================================================================
// CSMsg — Client-server message (our own design)
// ============================================================================
class CSMsg {
public:
    CSMsg(IChannel* channel);
    ~CSMsg();
    
    int  getPortNumber();
    CSMsgId getId() const;
    void set(CSMsgId id);
    bool send();
    bool recv();
    bool request(CSMsgId reqId);

    // Stream access for reading/writing args
    Stream& getStream();
    void writeString(const std::string& s);
    void writeInt(unsigned int val);
    void writeLong(long val);
    void writeChar(char c);
    std::string readString();
    unsigned int readInt();
    long readLong();
    char readChar();

    static const char* msgName(CSMsgId id);

private:
    IChannel* channel_;
    CSMsgId id_;
    Stream* stream_;
};

// ============================================================================
// Stream — Binary serialization stream (our own design)
// ============================================================================
class Stream {
public:
    Stream(unsigned int initialSize = 256);
    ~Stream();
    
    void reserve(unsigned int size);
    void setSize(unsigned int size);
    void clear(unsigned int offset = 0);
    
    int inFrom(char* buf, int maxLen);
    int outTo(char* buf, int maxLen);
    
    Stream& operator<<(const char& c);
    Stream& operator<<(const unsigned int& val);
    Stream& operator<<(const long& val);
    Stream& operator<<(const std::string& s);
    
    Stream& operator>>(char& c);
    Stream& operator>>(unsigned int& val);
    Stream& operator>>(long& val);
    Stream& operator>>(std::string& s);

    // Helpers for CSMsg
    char* getBuf() { return buf_; }
    unsigned int getSize() const { return writePos_; }
    void setReadPos(unsigned int pos) { readPos_ = pos; }

private:
    char* buf_;
    unsigned int size_;
    unsigned int capacity_;
    unsigned int readPos_;
    unsigned int writePos_;
};

// ============================================================================
// StringTbl — String interning table (our own design)
// ============================================================================
class StringTbl {
public:
    StringTbl(StringTbl* master = nullptr);
    ~StringTbl();
    
    void setMaster(StringTbl* master);
    unsigned int addString(const std::string& s);
    unsigned int findKey(const std::string& s);
    std::string operator[](unsigned int idx);
    std::string operator[](const std::string& key);
    const char* cStrAt(unsigned int idx);
    
    void strmOut(Stream& s, unsigned int count);
    void strmIn(Stream& s);
    void updateMaster();
    
private:
    StringTbl* master_;
    std::map<std::string, unsigned int> keyMap_;
    std::vector<std::string> strings_;
};

// ============================================================================
// Internal Database Objects (our own design, server-side only)
// These are NOT OA types — they are our internal representation
// ============================================================================

typedef std::map<unsigned int, unsigned int> FileNumMap;

class DMObject {
public:
    DMObject();
    virtual ~DMObject();
    
    void addFile(unsigned int fileId, unsigned int fileType);
    void removeFile(unsigned int fileId);
    void findFile(unsigned int fileId) const;
    const std::map<unsigned int, unsigned int>& getFiles() const;
    bool hasFiles() const;
    
protected:
    std::map<unsigned int, unsigned int> files_;
};

class DMLib : public DMObject {
public:
    DMLib(const std::string& name, const std::string& path);
    ~DMLib();
    
    void getName(std::string& name) const;
    void getPath(std::string& path) const;
    
    const std::map<unsigned int, unsigned int>& getCells() const;
    const std::map<unsigned int, unsigned int>& getViews() const;
    const std::map<unsigned int, unsigned int>& getCellViews() const;
    
    void findCell(unsigned int cellId) const;
    void findView(unsigned int viewId, unsigned int viewType) const;
    void findCellView(unsigned int cellId, unsigned int viewId, unsigned int viewType) const;
    
    void addCell(unsigned int cellId);
    void removeCell(unsigned int cellId);
    void addView(unsigned int viewId, unsigned int viewType);
    void removeView(unsigned int viewId, unsigned int viewType);
    void addCellView(unsigned int cellId, unsigned int viewId, unsigned int viewType);
    void removeCellView(unsigned int cellId, unsigned int viewId, unsigned int viewType);
    
private:
    std::string name_;
    std::string path_;
    std::map<unsigned int, unsigned int> cells_;
    std::map<unsigned int, unsigned int> views_;
    std::map<unsigned int, unsigned int> cellViews_;
};

class DMCell : public DMObject {
public:
    DMCell(DMLib* lib, unsigned int cellId);
    
    const char* getName() const;
    DMLib* getLib() const;
    const std::vector<DMCellView*>& getCellViews() const;
    void findCellView(DMView* view) const;
    void addCellView(DMCellView* cv);
    void removeCellView(DMCellView* cv);
    
private:
    DMLib* lib_;
    unsigned int cellId_;
    std::vector<DMCellView*> cellViews_;
};

class DMView : public DMObject {
public:
    DMView(DMLib* lib, unsigned int viewId, unsigned int viewType);
    
    const char* getName() const;
    const char* getViewType() const;
    DMLib* getLib() const;
    
private:
    DMLib* lib_;
    unsigned int viewId_;
    unsigned int viewType_;
};

class DMCellView : public DMObject {
public:
    DMCellView(DMCell* cell, DMView* view);
    ~DMCellView();
    
    DMCell* getCell() const;
    DMView* getView() const;
    unsigned int getPrimaryFile() const;
    void setPrimaryFile(unsigned int fileId);
    void setView(DMView* view);
    
private:
    DMCell* cell_;
    DMView* view_;
    unsigned int primaryFile_;
};

class DMFile : public DMObject {
public:
    DMFile(DMObject* parent, unsigned int fileId, unsigned int fileType, bool primary);
    ~DMFile();
    
    DMObject* getParent() const;
    const char* getName() const;
    bool isPrimary() const;
    bool isLeader() const;
    DMFile* getLeader() const;
    
    unsigned int getLockStatus(unsigned int pid);
    void releaseLock(unsigned int pid);
    bool canLock();
    bool lock(unsigned int pid);
    
    void releaseCache(const oaCommon::ProcInfo& procInfo);
    void getCache(const oaCommon::ProcInfo& procInfo);
    void addFile(unsigned int fileId, unsigned int fileType);
    void setLeader(DMFile* leader);
    
private:
    DMObject* parent_;
    unsigned int fileId_;
    unsigned int fileType_;
    bool primary_;
    DMFile* leader_;
    int lockPid_;
    std::string* cachePath_;
};

class DMIn {
public:
    DMIn(StringTbl& strTbl, oaPlugIn::IDMAccess* access, FileNumMap& fileNumMap, unsigned int& objCount);
    ~DMIn();
    
    void doLib(Stream& s);
    void doCells(Stream& s);
    void doViews(Stream& s);
    void doCellViews(Stream& s);
    void doFiles(Stream& s);
    
private:
    StringTbl& strTbl_;
    oaPlugIn::IDMAccess* access_;
    FileNumMap& fileNumMap_;
    unsigned int& objCount_;
};

class DMOut {
public:
    DMOut(DMLib* lib);
    
    void doLib(Stream& s);
    void doCells(Stream& s);
    void doViews(Stream& s);
    void doCellViews(Stream& s);
    void doFiles(Stream& s);
    
private:
    DMLib* lib_;
};

class Action {
public:
    Action(oaPlugIn::IDMFile* file, char op, StringTbl& strTbl, unsigned int id, oaCommon::ProcInfo* procInfo);
    
    void setStr(oaPlugIn::IDMFile* file, unsigned int* ids, StringTbl& strTbl, unsigned int count);
    void setStr(oaPlugIn::IDMFile* file, unsigned int* ids, StringTbl& strTbl, oaCommon::ProcInfo* procInfo);
    static const char* getNumStr(char c1, char c2);
};

class DMLog {
public:
    void operator<<(const Action& action);
    void strmIn(Stream& s);
    void strmOut(Stream& s);
    void execute(Stream& s, unsigned int version, VLogRun* runner);
    void execute(VLogRun* runner);
    void execute(Stream& s, VLogRun* runner);
    
private:
    std::vector<Action> actions_;
};

class LogIO {
public:
    LogIO(const std::string& path, StringTbl& strTbl);
    ~LogIO();
    
    void load(DMLog& log);
    void save(DMLog* log);
    void clear();
    void execute(char op, char type, unsigned int* ids);
    
private:
    std::string path_;
    StringTbl& strTbl_;
};

class FileNameRec {
public:
    FileNameRec(const std::string& lib, const std::string& cell, const std::string& view, const std::string& file);
    FileNameRec(oaPlugIn::IDMFile* file);
    
    unsigned long hash() const;
    bool operator==(const FileNameRec& other) const;
    
private:
    std::string lib_;
    std::string cell_;
    std::string view_;
    std::string file_;
};

class ServerInfo {
public:
    ServerInfo();
    ServerInfo(const ServerInfo& other);
    
    void reset(const std::string& host);
    ServerInfo& operator=(const ServerInfo& other);
    
    std::string host;
    unsigned int port;
    unsigned int pid;
};

class ServerXml {
public:
    ServerXml(ServerInfo& info);
    ~ServerXml();
    
    void attribute(const oaCommon::XmlValue& key, const oaCommon::XmlValue& value);
    void load(const std::string& path);
    void write(const std::string& path);
    void remove(const std::string& path);
    
private:
    ServerInfo& info_;
};

class IDMLibServer {
public:
    virtual ~IDMLibServer() {}
    virtual void get(const std::string& lib, ServerInfo& info, const std::string& path, unsigned int flags) = 0;
    virtual void find(const std::string& lib, unsigned int flags, IDMLibServer*& result) = 0;
};

class LibServer : public IDMLibServer {
public:
    void init(const char* path, unsigned int port);
    void requestStop();
    bool isActive();
    unsigned int getOpenLibCount();
    
    void serveLib(const char* libName, const char* libPath);
    
    unsigned long addRef();
    unsigned long release();
    unsigned long getRefCount();
    long queryInterface(const oaCommon::Guid& id, void** iPtr);
    
    virtual void get(const std::string& lib, ServerInfo& info, const std::string& path, unsigned int flags);
    virtual void find(const std::string& lib, unsigned int flags, IDMLibServer*& result);
    
private:
    IChannel* channel_;
    unsigned int refCount_;
    bool active_;
};

class LibServerMgr {
public:
    LibServerMgr();
    ~LibServerMgr();
    
    static LibServerMgr& get();
    
    void start(unsigned int maxServers, const char* logPath);
    void start(unsigned int maxServers, unsigned int port, const char* logPath);
    void find(const char* libName, unsigned int flags, IDMLibServer*& result);
    
    unsigned long addRef();
    unsigned long release();
    unsigned long getRefCount();
    long queryInterface(const oaCommon::Guid& id, void** iPtr);
    
    static class Factory : public oaCommon::IFactory {
    public:
        virtual oa::oaUInt4 createInstance(oaCommon::IBase*, const oaCommon::Guid&, void**);
        virtual unsigned long addRef() override { return ++refCount_; }
        virtual unsigned long release() override { return --refCount_; }
        virtual unsigned long getRefCount() override { return refCount_; }
        virtual long queryInterface(const oaCommon::Guid& id, void** ptr) override {
            if (id == oaCommon::IID_IBase || id == oaCommon::IID_IFactory) {
                *ptr = static_cast<oaCommon::IFactory*>(this);
                return oaCommon::IBase::cOK;
            }
            *ptr = nullptr;
            return oaCommon::IBase::cNoInterface;
        }
    private:
        unsigned long refCount_ = 0;
    } factory;
    
private:
    std::map<std::string, LibServer*> servers_;
    unsigned int refCount_;
};

// ============================================================================
// PlugIn — OA DM plugin implementation
//
// CRITICAL: This class MUST inherit from oaPlugIn::ILib and implement ALL
//           48 pure virtual methods defined in oaPlugInDMInterfaces.h
// ============================================================================
class LibStorage;

class PlugIn : public oaPlugIn::ILib,
              public oaPlugIn::IDMAccess,
              public oaPlugIn::ILocking,
              public oaPlugIn::IDMSystemCaps,
              public oaPlugIn::IAccessControl,
              public oaCommon::IPlugInAbort {
public:
    // ========================================================================
    // IBase interface (reference counting)
    // ========================================================================
    unsigned long addRef();
    unsigned long release();
    unsigned long getRefCount();
    long queryInterface(const oaCommon::Guid& id, void** iPtr);
    
    // ========================================================================
    // ILib interface — ALL 48 methods must be implemented
    // ========================================================================
    
    // --- Lifecycle (7 methods) ---
    void init(const char* libName, const char* libPath, oaPlugIn::oaLibModeEnum libMode,
              const char* writePath, oaPlugIn::IDMAccess* dmAccess,
              oaPlugIn::IAttrIter* dmAttrs = NULL) override;
    
    bool libExists(const char* libPath) override;
    
    void libPreCreate() override;
    void libPostCreate() override;
    void libPreOpen() override;
    void libPostOpen() override;
    void libPreClose() override;
    
    // --- Cell operations (6 methods) ---
    void cellCreate(const char* cellName) override;
    bool cellFind(const char* cellName) override;
    bool cellValidate(const char* cellName) override;
    void cellPreFind() override;
    void cellValidateDestroy(oaPlugIn::ICell* cell) override;
    void cellDestroy(oaPlugIn::ICell* cell) override;
    
    // --- View operations (7 methods) ---
    void viewCreate(const char* viewName, const char* viewType) override;
    bool viewFind(const char* viewName, const char* viewType) override;
    bool viewValidate(const char* viewName, const char* viewType) override;
    void viewPreFind(const char* viewName) override;
    void viewPreFind() override;
    void viewValidateDestroy(oaPlugIn::IView* view) override;
    void viewDestroy(oaPlugIn::IView* view) override;
    
    // --- CellView operations (11 methods) ---
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
    
    // --- File operations (17 methods) ---
    void fileCreate(const char* name, oaPlugIn::IDMObject* parent, bool primary) override;
    bool fileFind(const char* name, oaPlugIn::IDMObject* parent) override;
    bool fileValidate(const char* name, oaPlugIn::IDMObject* parent) override;
    void filePreFind(oaPlugIn::IDMObject* parent) override;
    void filePreSetLeader(oaPlugIn::IDMFile* file, oaPlugIn::IDMFile* leader) override;
    void filePostSetLeader(oaPlugIn::IDMFile* file) override;
    void filePreSetName(oaPlugIn::IDMFile* file, const char* name) override;
    void filePostSetName(oaPlugIn::IDMFile* file) override;
    void fileValidateDestroy(oaPlugIn::IDMFile* file) override;
    void fileDestroy(oaPlugIn::IDMFile* file) override;
    bool fileGetCache(oaPlugIn::IDMFile* file, oaCommon::IString*& path) override;
    void fileReleaseCache(oaPlugIn::IDMFile* file, const char* path) override;
    
    void getPath(oaPlugIn::IDMFile* file, oaCommon::IString*& path) override;
    void getPath(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type, oaCommon::IString*& path) override;
    
    void getTempFile(oaPlugIn::IDMObject* dmObject, bool sameFileSystem, oaCommon::IString*& path) override;
    bool exists(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    
    void fileCreate(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    void fileValidateDestroy(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    void fileDestroy(oaPlugIn::IDMFile* file, oaPlugIn::oaSaveRecoverTypeEnum type) override;
    
    // --- System (3 methods) ---
    void getDMSystemName(oaCommon::IString*& dmSystem) override;
    void getAttributes(oaPlugIn::IAttrIter*& dmAttrs) override;
    void setAttributes(oaPlugIn::IAttrIter* dmAttrs) override;
    
    // ========================================================================
    // ILocking interface (4 methods)
    // ========================================================================
    bool lock(oaPlugIn::IDMFile* obj) override;
    void unlock(oaPlugIn::IDMFile* obj) override;
    oa::oaUInt4 getLockStatus(oaPlugIn::IDMFile* obj) override;
    void setPlugInMessage(oaPlugIn::IPlugInMessage* plugInMsgIn) override;
    
    // ========================================================================
    // IDMSystemCaps interface (3 methods)
    // ========================================================================
    bool queryCapability(const char* name) override;
    bool getMetric(const char* name, oa::oaUInt4& metric) override;
    bool getMetric(const char* name, double& metric) override;
    
    // ========================================================================
    // Factory and metadata
    // ========================================================================
    static class Factory : public oaCommon::IFactory {
    public:
        virtual oa::oaUInt4 createInstance(oaCommon::IBase*, const oaCommon::Guid&, void**);
        virtual unsigned long addRef() override { return ++refCount_; }
        virtual unsigned long release() override { return --refCount_; }
        virtual unsigned long getRefCount() override { return refCount_; }
        virtual long queryInterface(const oaCommon::Guid& id, void** ptr) override {
            if (id == oaCommon::IID_IBase || id == oaCommon::IID_IFactory) {
                *ptr = static_cast<oaCommon::IFactory*>(this);
                return oaCommon::IBase::cOK;
            }
            *ptr = nullptr;
            return oaCommon::IBase::cNoInterface;
        }
    private:
        unsigned long refCount_ = 0;
    } factory;
    
    static const char* getPlugInName();
    
private:
    // IAccessControl interface
    bool getAccess(oaPlugIn::oaLibAccessEnum accessType, oa::oaUInt4 timeOut) override;
    void releaseAccess() override;
    
    // IDMAccess interface
    void add(oaPlugIn::IDMObject* object, bool checkExistence = false) override;
    void addLeader(oaPlugIn::IDMFile* file) override;
    void create(oaPlugIn::IDMObject* object) override;
    void destroy(oaPlugIn::IDMObject* object) override;
    void getMappedName(oaPlugIn::IDMAccess::NameSpace fromNS, const char* fromName,
                       oaPlugIn::IDMAccess::NameSpace toNS, oaCommon::IString*& toName) override;
    oaCommon::SRef<oaPlugIn::IDMLib> getDMLib() override;
    
    // IPlugInAbort interface
    void onAbort() override;
    
    // Internal state
    unsigned long refCount_;
    std::string libName_;
    std::string libPath_;
    std::string writePath_;
    bool libReadOnly_;
    DMLib* dbLib_;
    IChannel* channel_;
    oaPlugIn::IDMAccess* dmAccess_;
    LibStorage* storage_;
};

// ============================================================================
// Global entry points
// ============================================================================
extern "C" {
    void oaDMTurboInit();
    void DMTurboExit();
    long getClassObject(const char* clsid, const oaCommon::Guid& iid, void** ppv);
}

} // namespace oaDMTurbo

#endif // OADMTURBO_H
