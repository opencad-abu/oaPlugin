// *****************************************************************************
// oaAiviDMTurboBase.cpp — Stub implementation of liboaDMTurboBase.so
//
// Implements the database layer and communication layer for the
// client-server DM system.  All methods are stub/fake implementations
// that COMPILE — the goal is API compatibility, not full functionality.
// *****************************************************************************

#include "oaDMTurbo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <ctime>

// GUID constants are defined in OA's liboaPlugIn.so - link against it for real values

// ============================================================================
// Exception hierarchy
// ============================================================================
namespace oaDMTurbo {

static const char* kDMTurboMsgTable = "DMTurbo Message Table";

const char* Exception::getMsgsTable() const {
    return kDMTurboMsgTable;
}

int Exception::getMsgIdStartValue() const {
    return oacDMTurboErrBase;
}

int Exception::getMsgIdEndValue() const {
    return oacDMTurboErrBase + 999;
}

// Error — format-string constructor (va_list)
Error::Error(oaDMTurboMsgIds id, ...)
    : Exception(id)
{
    // In a real implementation, format the va_args message.
    // Stub: ignore variadic args.
    va_list args;
    va_start(args, id);
    (void)args;   // silence unused warning
    va_end(args);
}

Error::Error(const OpenAccess_4::oaException& ex)
    : Exception(static_cast<oaDMTurboMsgIds>(oacDMTurboErrBase))
{
    (void)ex;  // stub — discard OA exception info
}

// OSError — inherits inline constructors from Error, nothing extra to define.

SocketError::SocketError()
    : OSError(static_cast<oaDMTurboMsgIds>(oacDMTurboErrBase), "SocketError")
{
}

// ============================================================================
// MsgName — Message name registry
// ============================================================================

// Static map: message ID → name string
static std::map<unsigned int, const char*>& msgNameMap() {
    static std::map<unsigned int, const char*> m;
    return m;
}

// MsgName removed - no longer in header

// ============================================================================
// StringTbl — String interning table
// ============================================================================

StringTbl::StringTbl(StringTbl* master)
    : master_(master)
{
}

StringTbl::~StringTbl() {
    // Nothing to clean up (don't own master_)
}

void StringTbl::setMaster(StringTbl* master) {
    master_ = master;
}

unsigned int StringTbl::addString(const std::string& s) {
    // Check if string already exists
    std::map<std::string, unsigned int>::const_iterator it = keyMap_.find(s);
    if (it != keyMap_.end()) {
        return it->second;
    }
    unsigned int idx = static_cast<unsigned int>(strings_.size());
    keyMap_[s] = idx;
    strings_.push_back(s);
    return idx;
}

unsigned int StringTbl::findKey(const std::string& s) {
    std::map<std::string, unsigned int>::const_iterator it = keyMap_.find(s);
    if (it != keyMap_.end()) {
        return it->second;
    }
    // Check master table
    if (master_) {
        return master_->findKey(s);
    }
    return static_cast<unsigned int>(-1);  // not found
}

std::string StringTbl::operator[](unsigned int idx) {
    if (idx < strings_.size()) {
        return strings_[idx];
    }
    if (master_ && idx >= strings_.size()) {
        return (*master_)[idx];
    }
    return std::string();
}

std::string StringTbl::operator[](const std::string& key) {
    unsigned int idx = findKey(key);
    if (idx != static_cast<unsigned int>(-1)) {
        return (*this)[idx];
    }
    return std::string();
}

const char* StringTbl::cStrAt(unsigned int idx) {
    // Returns a pointer to interned string — caller must NOT free.
    static std::string cache;
    cache = (*this)[idx];
    if (cache.empty()) return nullptr;
    return cache.c_str();
}

void StringTbl::strmOut(Stream& s, unsigned int count) {
    // Serialize count strings to stream
    s << count;
    for (unsigned int i = 0; i < count && i < strings_.size(); ++i) {
        s << strings_[i];
    }
}

void StringTbl::strmIn(Stream& s) {
    // Deserialize strings from stream
    unsigned int count = 0;
    s >> count;
    strings_.clear();
    keyMap_.clear();
    for (unsigned int i = 0; i < count; ++i) {
        std::string str;
        s >> str;
        addString(str);
    }
}

void StringTbl::updateMaster() {
    if (master_) {
        // Push local strings to master
        for (unsigned int i = 0; i < strings_.size(); ++i) {
            master_->addString(strings_[i]);
        }
    }
}

// ============================================================================
// Stream — Binary serialization stream
// ============================================================================

Stream::Stream(unsigned int initialSize)
    : buf_(nullptr), size_(0), capacity_(0), readPos_(0), writePos_(0)
{
    reserve(initialSize);
}

Stream::~Stream() {
    if (buf_) {
        free(buf_);
        buf_ = nullptr;
    }
}

void Stream::reserve(unsigned int size) {
    if (size > capacity_) {
        unsigned int newCap = capacity_ > 0 ? capacity_ * 2 : size;
        if (newCap < size) newCap = size;
        char* newBuf = static_cast<char*>(realloc(buf_, newCap));
        if (newBuf) {
            buf_ = newBuf;
            capacity_ = newCap;
        }
    }
}

void Stream::setSize(unsigned int size) {
    reserve(size);
    size_ = size;
    if (writePos_ > size_) writePos_ = size_;
    if (readPos_ > size_) readPos_ = size_;
}

void Stream::clear(unsigned int offset) {
    readPos_ = offset;
    writePos_ = offset;
    if (offset == 0) {
        size_ = 0;
    }
}

int Stream::inFrom(char* buf, int maxLen) {
    if (!buf_ || !buf) return 0;
    int toRead = (static_cast<int>(size_) < maxLen) ? static_cast<int>(size_) : maxLen;
    if (toRead > 0) {
        memcpy(buf, buf_, toRead);
        readPos_ += toRead;
    }
    return toRead;
}

int Stream::outTo(char* buf, int maxLen) {
    if (!buf_ || !buf) return 0;
    int toWrite = (static_cast<int>(size_) < maxLen) ? static_cast<int>(size_) : maxLen;
    if (toWrite > 0) {
        memcpy(buf_, buf, toWrite);
        writePos_ += toWrite;
    }
    return toWrite;
}

Stream& Stream::operator<<(const char& c) {
    reserve(writePos_ + 1);
    buf_[writePos_++] = c;
    if (writePos_ > size_) size_ = writePos_;
    return *this;
}

Stream& Stream::operator<<(const unsigned int& val) {
    reserve(writePos_ + sizeof(unsigned int));
    memcpy(buf_ + writePos_, &val, sizeof(unsigned int));
    writePos_ += sizeof(unsigned int);
    if (writePos_ > size_) size_ = writePos_;
    return *this;
}

Stream& Stream::operator<<(const long& val) {
    reserve(writePos_ + sizeof(long));
    memcpy(buf_ + writePos_, &val, sizeof(long));
    writePos_ += sizeof(long);
    if (writePos_ > size_) size_ = writePos_;
    return *this;
}

Stream& Stream::operator<<(const std::string& s) {
    unsigned int len = static_cast<unsigned int>(s.size());
    *this << len;
    reserve(writePos_ + len);
    memcpy(buf_ + writePos_, s.data(), len);
    writePos_ += len;
    if (writePos_ > size_) size_ = writePos_;
    return *this;
}

Stream& Stream::operator>>(char& c) {
    if (readPos_ < size_) {
        c = buf_[readPos_++];
    } else {
        c = 0;
    }
    return *this;
}

Stream& Stream::operator>>(unsigned int& val) {
    if (readPos_ + sizeof(unsigned int) <= size_) {
        memcpy(&val, buf_ + readPos_, sizeof(unsigned int));
        readPos_ += sizeof(unsigned int);
    } else {
        val = 0;
    }
    return *this;
}

Stream& Stream::operator>>(long& val) {
    if (readPos_ + sizeof(long) <= size_) {
        memcpy(&val, buf_ + readPos_, sizeof(long));
        readPos_ += sizeof(long);
    } else {
        val = 0;
    }
    return *this;
}

Stream& Stream::operator>>(std::string& s) {
    unsigned int len = 0;
    *this >> len;
    if (len > 0 && readPos_ + len <= size_) {
        s.assign(buf_ + readPos_, len);
        readPos_ += len;
    } else {
        s.clear();
    }
    return *this;
}

// ============================================================================
// DMObject — Base database object
// ============================================================================

DMObject::DMObject() {
}

DMObject::~DMObject() {
}

void DMObject::addFile(unsigned int fileId, unsigned int fileType) {
    files_[fileId] = fileType;
}

void DMObject::removeFile(unsigned int fileId) {
    std::map<unsigned int, unsigned int>::iterator it = files_.find(fileId);
    if (it != files_.end()) {
        files_.erase(it);
    }
}

void DMObject::findFile(unsigned int fileId) const {
    // Stub: does nothing, caller checks files_ map directly
    (void)fileId;
}

const std::map<unsigned int, unsigned int>& DMObject::getFiles() const {
    return files_;
}

bool DMObject::hasFiles() const {
    return !files_.empty();
}

// ============================================================================
// DMLib — Library database object
// ============================================================================

DMLib::DMLib(const std::string& name, const std::string& path)
    : DMObject(), name_(name), path_(path)
{
}

DMLib::~DMLib() {
}

void DMLib::getName(std::string& name) const {
    name = name_;
}

void DMLib::getPath(std::string& path) const {
    path = path_;
}

const std::map<unsigned int, unsigned int>& DMLib::getCells() const {
    return cells_;
}

const std::map<unsigned int, unsigned int>& DMLib::getViews() const {
    return views_;
}

const std::map<unsigned int, unsigned int>& DMLib::getCellViews() const {
    return cellViews_;
}

void DMLib::findCell(unsigned int cellId) const {
    (void)cellId;
}

void DMLib::findView(unsigned int viewId, unsigned int viewType) const {
    (void)viewId;
    (void)viewType;
}

void DMLib::findCellView(unsigned int cellId, unsigned int viewId,
                         unsigned int viewType) const {
    (void)cellId;
    (void)viewId;
    (void)viewType;
}

void DMLib::addCell(unsigned int cellId) {
    unsigned int idx = static_cast<unsigned int>(cells_.size());
    cells_[cellId] = idx;
}

void DMLib::removeCell(unsigned int cellId) {
    std::map<unsigned int, unsigned int>::iterator it = cells_.find(cellId);
    if (it != cells_.end()) {
        cells_.erase(it);
    }
}

void DMLib::addView(unsigned int viewId, unsigned int viewType) {
    views_[viewId] = viewType;
}

void DMLib::removeView(unsigned int viewId, unsigned int viewType) {
    std::map<unsigned int, unsigned int>::iterator it = views_.find(viewId);
    if (it != views_.end() && it->second == viewType) {
        views_.erase(it);
    }
}

void DMLib::addCellView(unsigned int cellId, unsigned int viewId,
                        unsigned int viewType) {
    // Encode cellId and viewId into a single key
    unsigned int key = (cellId << 16) | (viewId & 0xFFFF);
    cellViews_[key] = viewType;
}

void DMLib::removeCellView(unsigned int cellId, unsigned int viewId,
                           unsigned int viewType) {
    unsigned int key = (cellId << 16) | (viewId & 0xFFFF);
    std::map<unsigned int, unsigned int>::iterator it = cellViews_.find(key);
    if (it != cellViews_.end() && it->second == viewType) {
        cellViews_.erase(it);
    }
}

// ============================================================================
// DMCell — Cell database object
// ============================================================================

DMCell::DMCell(DMLib* lib, unsigned int cellId)
    : DMObject(), lib_(lib), cellId_(cellId)
{
}

const char* DMCell::getName() const {
    static char buf[64];
    snprintf(buf, sizeof(buf), "cell_%u", cellId_);
    return buf;
}

DMLib* DMCell::getLib() const {
    return lib_;
}

const std::vector<DMCellView*>& DMCell::getCellViews() const {
    return cellViews_;
}

void DMCell::findCellView(DMView* view) const {
    (void)view;
}

void DMCell::addCellView(DMCellView* cv) {
    cellViews_.push_back(cv);
}

void DMCell::removeCellView(DMCellView* cv) {
    std::vector<DMCellView*>::iterator it =
        std::find(cellViews_.begin(), cellViews_.end(), cv);
    if (it != cellViews_.end()) {
        cellViews_.erase(it);
    }
}

// ============================================================================
// DMView — View database object
// ============================================================================

DMView::DMView(DMLib* lib, unsigned int viewId, unsigned int viewType)
    : DMObject(), lib_(lib), viewId_(viewId), viewType_(viewType)
{
}

const char* DMView::getName() const {
    static char buf[64];
    snprintf(buf, sizeof(buf), "view_%u", viewId_);
    return buf;
}

const char* DMView::getViewType() const {
    static char buf[64];
    snprintf(buf, sizeof(buf), "viewType_%u", viewType_);
    return buf;
}

DMLib* DMView::getLib() const {
    return lib_;
}

// ============================================================================
// DMCellView — CellView database object
// ============================================================================

DMCellView::DMCellView(DMCell* cell, DMView* view)
    : DMObject(), cell_(cell), view_(view), primaryFile_(0)
{
}

DMCellView::~DMCellView() {
}

DMCell* DMCellView::getCell() const {
    return cell_;
}

DMView* DMCellView::getView() const {
    return view_;
}

unsigned int DMCellView::getPrimaryFile() const {
    return primaryFile_;
}

void DMCellView::setPrimaryFile(unsigned int fileId) {
    primaryFile_ = fileId;
}

void DMCellView::setView(DMView* view) {
    view_ = view;
}

// ============================================================================
// DMFile — File database object
// ============================================================================

DMFile::DMFile(DMObject* parent, unsigned int fileId, unsigned int fileType,
               bool primary)
    : DMObject(),
      parent_(parent),
      fileId_(fileId),
      fileType_(fileType),
      primary_(primary),
      leader_(nullptr),
      lockPid_(0),
      cachePath_(nullptr)
{
}

DMFile::~DMFile() {
    delete cachePath_;
}

DMObject* DMFile::getParent() const {
    return parent_;
}

const char* DMFile::getName() const {
    static char buf[64];
    snprintf(buf, sizeof(buf), "file_%u", fileId_);
    return buf;
}

bool DMFile::isPrimary() const {
    return primary_;
}

bool DMFile::isLeader() const {
    // A file is a leader if no other file is its leader
    return leader_ == nullptr;
}

DMFile* DMFile::getLeader() const {
    return leader_;
}

unsigned int DMFile::getLockStatus(unsigned int pid) {
    if (lockPid_ == 0) return 0;
    if (lockPid_ == static_cast<int>(pid)) return 1;
    return 2;  // locked by other
}

void DMFile::releaseLock(unsigned int pid) {
    if (lockPid_ == static_cast<int>(pid)) {
        lockPid_ = 0;
    }
}

bool DMFile::canLock() {
    return lockPid_ == 0;
}

bool DMFile::lock(unsigned int pid) {
    if (lockPid_ == 0) {
        lockPid_ = static_cast<int>(pid);
        return true;
    }
    return false;
}

void DMFile::releaseCache(const oaCommon::ProcInfo& procInfo) {
    (void)procInfo;
    delete cachePath_;
    cachePath_ = nullptr;
}

void DMFile::getCache(const oaCommon::ProcInfo& procInfo) {
    (void)procInfo;
    if (!cachePath_) {
        cachePath_ = new std::string("/tmp/cache_file_");
        *cachePath_ += std::to_string(fileId_);
    }
}

void DMFile::addFile(unsigned int fileId, unsigned int fileType) {
    DMObject::addFile(fileId, fileType);
}

void DMFile::setLeader(DMFile* leader) {
    leader_ = leader;
}

// ============================================================================
// VLogRun — Minimal definition (used by DMLog)
// ============================================================================
class VLogRun {
public:
    VLogRun() {}
    virtual ~VLogRun() {}
    virtual void run(class Action& a) { (void)a; }
};

// ============================================================================
// DMIn — Database deserialization (Stream → in-memory DB)
// ============================================================================

DMIn::DMIn(StringTbl& strTbl, oaPlugIn::IDMAccess* access,
           FileNumMap& fileNumMap, unsigned int& objCount)
    : strTbl_(strTbl), access_(access),
      fileNumMap_(fileNumMap), objCount_(objCount)
{
}

DMIn::~DMIn() {
}

void DMIn::doLib(Stream& s) {
    (void)s;
    objCount_++;
}

void DMIn::doCells(Stream& s) {
    (void)s;
}

void DMIn::doViews(Stream& s) {
    (void)s;
}

void DMIn::doCellViews(Stream& s) {
    (void)s;
}

void DMIn::doFiles(Stream& s) {
    (void)s;
}

// ============================================================================
// DMOut — Database serialization (in-memory DB → Stream)
// ============================================================================

DMOut::DMOut(DMLib* lib)
    : lib_(lib)
{
}

void DMOut::doLib(Stream& s) {
    (void)s;
    if (lib_) {
        std::string name, path;
        lib_->getName(name);
        lib_->getPath(path);
        s << name;
        s << path;
    }
}

void DMOut::doCells(Stream& s) {
    (void)s;
    if (lib_) {
        const std::map<unsigned int, unsigned int>& cells = lib_->getCells();
        unsigned int count = static_cast<unsigned int>(cells.size());
        s << count;
        for (std::map<unsigned int, unsigned int>::const_iterator it = cells.begin();
             it != cells.end(); ++it) {
            s << it->first;
            s << it->second;
        }
    }
}

void DMOut::doViews(Stream& s) {
    (void)s;
    if (lib_) {
        const std::map<unsigned int, unsigned int>& views = lib_->getViews();
        unsigned int count = static_cast<unsigned int>(views.size());
        s << count;
        for (std::map<unsigned int, unsigned int>::const_iterator it = views.begin();
             it != views.end(); ++it) {
            s << it->first;
            s << it->second;
        }
    }
}

void DMOut::doCellViews(Stream& s) {
    (void)s;
    if (lib_) {
        const std::map<unsigned int, unsigned int>& cvs = lib_->getCellViews();
        unsigned int count = static_cast<unsigned int>(cvs.size());
        s << count;
        for (std::map<unsigned int, unsigned int>::const_iterator it = cvs.begin();
             it != cvs.end(); ++it) {
            s << it->first;
            s << it->second;
        }
    }
}

void DMOut::doFiles(Stream& s) {
    (void)s;
}

// ============================================================================
// Action — Operation record
// ============================================================================

Action::Action(oaPlugIn::IDMFile* file, char op, StringTbl& strTbl,
               unsigned int id, oaCommon::ProcInfo* procInfo)
{
    (void)file;
    (void)op;
    (void)strTbl;
    (void)id;
    (void)procInfo;
}

void Action::setStr(oaPlugIn::IDMFile* file, unsigned int* ids,
                    StringTbl& strTbl, unsigned int count) {
    (void)file;
    (void)ids;
    (void)strTbl;
    (void)count;
}

void Action::setStr(oaPlugIn::IDMFile* file, unsigned int* ids,
                    StringTbl& strTbl, oaCommon::ProcInfo* procInfo) {
    (void)file;
    (void)ids;
    (void)strTbl;
    (void)procInfo;
}

const char* Action::getNumStr(char c1, char c2) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%c%c_0", c1, c2);
    return buf;
}

// ============================================================================
// DMLog — Action log
// ============================================================================

void DMLog::operator<<(const Action& action) {
    (void)action;
}

void DMLog::strmIn(Stream& s) {
    (void)s;
}

void DMLog::strmOut(Stream& s) {
    (void)s;
}

void DMLog::execute(Stream& s, unsigned int version, VLogRun* runner) {
    (void)s;
    (void)version;
    (void)runner;
}

void DMLog::execute(VLogRun* runner) {
    (void)runner;
}

void DMLog::execute(Stream& s, VLogRun* runner) {
    (void)s;
    (void)runner;
}

// ============================================================================
// LogIO — Log file I/O
// ============================================================================

LogIO::LogIO(const std::string& path, StringTbl& strTbl)
    : path_(path), strTbl_(strTbl)
{
}

LogIO::~LogIO() {
}

void LogIO::load(DMLog& log) {
    (void)log;
}

void LogIO::save(DMLog* log) {
    (void)log;
}

void LogIO::clear() {
}

void LogIO::execute(char op, char type, unsigned int* ids) {
    (void)op;
    (void)type;
    (void)ids;
}

// ============================================================================
// FileNameRec — File path record
// ============================================================================

FileNameRec::FileNameRec(const std::string& lib, const std::string& cell,
                         const std::string& view, const std::string& file)
    : lib_(lib), cell_(cell), view_(view), file_(file)
{
}

FileNameRec::FileNameRec(oaPlugIn::IDMFile* file)
    : lib_(""), cell_(""), view_(""), file_("")
{
    (void)file;
}

unsigned long FileNameRec::hash() const {
    // Simple DJB2 hash over the four string components
    unsigned long h = 5381;
    const std::string* parts[4] = { &lib_, &cell_, &view_, &file_ };
    for (int i = 0; i < 4; ++i) {
        for (size_t j = 0; j < parts[i]->size(); ++j) {
            h = ((h << 5) + h) + static_cast<unsigned char>((*parts[i])[j]);
        }
        h = ((h << 5) + h) + '/';  // separator
    }
    return h;
}

bool FileNameRec::operator==(const FileNameRec& other) const {
    return lib_ == other.lib_ &&
           cell_ == other.cell_ &&
           view_ == other.view_ &&
           file_ == other.file_;
}

// ============================================================================
// ServerInfo — Server connection info
// ============================================================================

ServerInfo::ServerInfo()
    : host(""), port(0), pid(0)
{
}

ServerInfo::ServerInfo(const ServerInfo& other)
    : host(other.host), port(other.port), pid(other.pid)
{
}

void ServerInfo::reset(const std::string& hostName) {
    host = hostName;
    port = 0;
    pid = 0;
}

ServerInfo& ServerInfo::operator=(const ServerInfo& other) {
    if (this != &other) {
        host = other.host;
        port = other.port;
        pid = other.pid;
    }
    return *this;
}

// ============================================================================
// ServerXml — XML server configuration
// ============================================================================

ServerXml::ServerXml(ServerInfo& info)
    : info_(info)
{
}

ServerXml::~ServerXml() {
}

void ServerXml::attribute(const oaCommon::XmlValue& key,
                          const oaCommon::XmlValue& value) {
    const char* k = static_cast<const char*>(key);
    const char* v = static_cast<const char*>(value);
    if (k && v) {
        std::string keyStr(k);
        std::string valStr(v);
        if (keyStr == "host") {
            info_.host = valStr;
        } else if (keyStr == "port") {
            info_.port = static_cast<unsigned int>(atoi(v));
        } else if (keyStr == "pid") {
            info_.pid = static_cast<unsigned int>(atoi(v));
        }
    }
}

void ServerXml::load(const std::string& path) {
    (void)path;
}

void ServerXml::write(const std::string& path) {
    (void)path;
}

void ServerXml::remove(const std::string& path) {
    (void)path;
}

// CSMsg implementation moved to oaAiviDMTurboMsg.cpp

// ============================================================================
// LibServer — Library server process (inherits IDMLibServer)
// ============================================================================

void LibServer::init(const char* path, unsigned int port) {
    (void)path;
    (void)port;
    active_ = true;
    refCount_ = 1;
    channel_ = nullptr;
}

void LibServer::requestStop() {
    active_ = false;
}

bool LibServer::isActive() {
    return active_;
}

unsigned int LibServer::getOpenLibCount() {
    return 1;
}

void LibServer::serveLib(const char* libName, const char* libPath) {
    (void)libName;
    (void)libPath;
}

unsigned long LibServer::addRef() {
    return ++refCount_;
}

unsigned long LibServer::release() {
    unsigned long rc = --refCount_;
    if (rc == 0) {
        // In real implementation: stop and clean up
    }
    return rc;
}

unsigned long LibServer::getRefCount() {
    return refCount_;
}

long LibServer::queryInterface(const oaCommon::Guid& id, void** iPtr) {
    (void)id;
    if (iPtr) *iPtr = nullptr;
    return 0;
}

void LibServer::get(const std::string& lib, ServerInfo& info,
                    const std::string& path, unsigned int flags) {
    (void)lib;
    (void)path;
    (void)flags;
    info.host = "localhost";
    info.port = 0;
    info.pid = static_cast<unsigned int>(getpid());
}

void LibServer::find(const std::string& lib, unsigned int flags,
                     IDMLibServer*& result) {
    (void)lib;
    (void)flags;
    // Stub: return self
    result = this;
}

// ============================================================================
// LibServerMgr — Server manager (singleton)
// ============================================================================

LibServerMgr::LibServerMgr()
    : refCount_(1)
{
}

LibServerMgr::~LibServerMgr() {
    for (std::map<std::string, LibServer*>::iterator it = servers_.begin();
         it != servers_.end(); ++it) {
        delete it->second;
    }
    servers_.clear();
}

LibServerMgr& LibServerMgr::get() {
    static LibServerMgr instance;
    return instance;
}

void LibServerMgr::start(unsigned int maxServers, const char* logPath) {
    (void)maxServers;
    (void)logPath;
}

void LibServerMgr::start(unsigned int maxServers, unsigned int port,
                         const char* logPath) {
    (void)maxServers;
    (void)port;
    (void)logPath;
}

void LibServerMgr::find(const char* libName, unsigned int flags,
                        IDMLibServer*& result) {
    (void)flags;
    std::string key(libName);
    std::map<std::string, LibServer*>::iterator it = servers_.find(key);
    if (it != servers_.end()) {
        result = it->second;       // LibServer IS-A IDMLibServer
        return;
    }
    LibServer* svr = new LibServer();
    svr->init(libName, 0);
    servers_[key] = svr;
    result = svr;                  // LibServer IS-A IDMLibServer
}

unsigned long LibServerMgr::addRef() {
    return ++refCount_;
}

unsigned long LibServerMgr::release() {
    return --refCount_;
}

unsigned long LibServerMgr::getRefCount() {
    return refCount_;
}

long LibServerMgr::queryInterface(const oaCommon::Guid& id, void** iPtr) {
    (void)id;
    if (iPtr) *iPtr = nullptr;
    return 0;
}

// Factory static instance
LibServerMgr::Factory LibServerMgr::factory;

oa::oaUInt4 LibServerMgr::Factory::createInstance(oaCommon::IBase* base,
    const oaCommon::Guid& guid, void** iPtr)
{
    (void)base;
    (void)guid;
    if (iPtr) *iPtr = nullptr;
    return 0;
}

// ============================================================================
// Global entry points
// ============================================================================

extern "C" {

void oaDMTurboInit() {
    // Stub: global initialization
}

void DMTurboExit() {
    // Stub: global cleanup
}

} // extern "C"

} // namespace oaDMTurbo
