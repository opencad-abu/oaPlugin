// oaAiviDMFileSysComp.cpp — DMFileSys plugin compiled against real OA headers
#include "oaDMFileSysComp.h"
#include <oa/oaCommonPlugInBase.h>
#include <cstring>
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <vector>
#include <fstream>

// ── Recursive directory removal ──────────────────────────────
namespace {
bool removeDirectoryRecursive(const std::string& path) {
    if (path.empty()) return false;
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string full = path + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            removeDirectoryRecursive(full);
        } else {
            ::unlink(full.c_str());
        }
    }
    closedir(dir);
    return ::rmdir(path.c_str()) == 0;
}

static std::string toStdString(const oaCommon::SRef<oaCommon::IString>& s) {
    if (!s) return "";
    const char* p = static_cast<const char*>(s);
    return p ? p : "";
}

static bool resolveCellViewContext(oaPlugIn::IDMObject* obj,
                                   std::string& cell,
                                   std::string& view) {
    if (!obj) return false;

    oaPlugIn::ICellView* cv = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICellView, reinterpret_cast<void**>(&cv)) == oaCommon::IBase::cOK && cv) {
        cell = toStdString(cv->getCellName());
        view = toStdString(cv->getViewName());
        cv->release();
        return !cell.empty() && !view.empty();
    }

    oaPlugIn::IDMFile* file = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMFile, reinterpret_cast<void**>(&file)) == oaCommon::IBase::cOK && file) {
        oaPlugIn::IDMObject* parent = nullptr;
        file->getParent(parent);
        bool ok = resolveCellViewContext(parent, cell, view);
        if (parent) {
            parent->release();
        }
        file->release();
        return ok;
    }

    oaPlugIn::ICell* c = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICell, reinterpret_cast<void**>(&c)) == oaCommon::IBase::cOK && c) {
        cell = toStdString(c->getName());
        c->release();
        return !cell.empty();
    }

    oaPlugIn::IView* v = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IView, reinterpret_cast<void**>(&v)) == oaCommon::IBase::cOK && v) {
        view = toStdString(v->getName());
        v->release();
        return !view.empty();
    }

    return false;
}

static bool resolveCellViewContext(oaPlugIn::IDMFile* file,
                                   std::string& cell,
                                   std::string& view) {
    if (!file) return false;
    oaPlugIn::IDMObject* parent = nullptr;
    file->getParent(parent);
    bool ok = resolveCellViewContext(parent, cell, view);
    if (parent) {
        parent->release();
    }
    return ok;
}

static bool hasSuffix(const std::string& value, const char* suffix) {
    if (!suffix) {
        return false;
    }
    const size_t suffixLen = std::strlen(suffix);
    return value.size() >= suffixLen &&
           value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

static std::string trimTrailingSlashes(const std::string& path) {
    std::string trimmed(path);
    while (trimmed.size() > 1 && !trimmed.empty() && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}

static std::string joinPath(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    if (rhs.empty()) {
        return lhs;
    }
    if (lhs.back() == '/') {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

static std::string dirnameOf(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

static std::string basenameOf(const std::string& path) {
    size_t pos = path.rfind('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

static std::string replaceDotOaExtension(const std::string& path, const char* replacement) {
    if (replacement == nullptr) {
        return path;
    }
    if (hasSuffix(path, ".oa")) {
        return path.substr(0, path.size() - 3) + replacement;
    }
    return path + replacement;
}

static const char* mapViewTypeToPrimaryFile(const std::string& viewType) {
    if (viewType == "schematicSymbol") {
        return "symbol.oa";
    }
    if (viewType == "schematic") {
        return "sch.oa";
    }
    if (viewType == "maskLayout") {
        return "layout.oa";
    }
    return nullptr;
}

static std::string readPrimaryFileFromMasterTag(const std::string& cellViewDir,
                                                const std::string& logicalName) {
    std::string directPath = joinPath(cellViewDir, logicalName);
    if (::access(directPath.c_str(), F_OK) == 0) {
        return logicalName;
    }

    std::ifstream master(joinPath(cellViewDir, oaDMFileSys::masterTagFileName).c_str());
    if (master) {
        std::string header;
        std::string line;
        std::getline(master, header);
        std::getline(master, line);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty()) {
            if (line.find("ViewType=") == 0) {
                const char* mapped = mapViewTypeToPrimaryFile(line.substr(9));
                if (mapped != nullptr) {
                    return mapped;
                }
            } else {
                return line;
            }
        }
    }

    if (logicalName.find('.') == std::string::npos) {
        DIR* dir = opendir(cellViewDir.c_str());
        if (dir) {
            struct dirent* entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name == "." || name == ".." || name == "data.dm" ||
                    name == "master.tag" || hasSuffix(name, oaDMFileSys::editLockFileExt) ||
                    hasSuffix(name, oaDMFileSys::criticalSaveExt) || hasSuffix(name, oaDMFileSys::autoSaveExt) ||
                    hasSuffix(name, ".oacache")) {
                    continue;
                }
                if (hasSuffix(name, ".oa")) {
                    closedir(dir);
                    return name;
                }
            }
            closedir(dir);
        }
    }

    return logicalName;
}

static std::string makeRelativeToRoot(const std::string& root, const std::string& path) {
    std::string normRoot = trimTrailingSlashes(root);
    if (!normRoot.empty() && path.compare(0, normRoot.size(), normRoot) == 0) {
        size_t pos = normRoot.size();
        if (path.size() > pos && path[pos] == '/') {
            ++pos;
        }
        return path.substr(pos);
    }
    return path;
}

static bool copyFileContents(const std::string& src, const std::string& dst) {
    std::ifstream in(src.c_str(), std::ios::binary);
    if (!in) {
        return false;
    }
    std::ofstream out(dst.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << in.rdbuf();
    return out.good();
}

static std::string trimString(const std::string& value)
{
    std::string::size_type first = 0;
    while (first < value.size() &&
           std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::string::size_type last = value.size();
    while (last > first &&
           std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

static bool accessLockLineMatchesKey(const std::string& line, const char* key)
{
    const size_t keyLen = std::strlen(key);
    if (line.compare(0, keyLen, key) != 0) {
        return false;
    }
    return line.size() == keyLen || line[keyLen] == '=' ||
           std::isspace(static_cast<unsigned char>(line[keyLen]));
}

static std::string accessLockLineValue(const std::string& line, const char* key)
{
    const size_t keyLen = std::strlen(key);
    if (line.size() <= keyLen) {
        return "";
    }
    if (line[keyLen] == '=') {
        return trimString(line.substr(keyLen + 1));
    }
    return trimString(line.substr(keyLen));
}

static std::string currentHostName()
{
    char buf[256];
    if (::gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return buf;
    }
    return "unknown";
}

static std::string currentLoginName()
{
    const char* user = std::getenv("USER");
    if (!user || !*user) {
        user = std::getenv("LOGNAME");
    }
    return (user && *user) ? user : "unknown";
}

static bool isLocalProcessAlive(unsigned long pid)
{
    if (pid == 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

struct AccessLockRecord {
    bool exists = false;
    std::string hostName;
    std::string loginName;
    unsigned long pid = 0;
    std::string accessType;
};

static bool readAccessLock(const std::string& path, AccessLockRecord& record)
{
    std::ifstream input(path.c_str());
    if (!input) {
        return false;
    }

    record = AccessLockRecord();
    record.exists = true;

    std::string line;
    while (std::getline(input, line)) {
        line = trimString(line);
        if (accessLockLineMatchesKey(line, "HostName")) {
            record.hostName = accessLockLineValue(line, "HostName");
        } else if (accessLockLineMatchesKey(line, "LoginName")) {
            record.loginName = accessLockLineValue(line, "LoginName");
        } else if (accessLockLineMatchesKey(line, "ProcessIdentifier")) {
            record.pid = std::strtoul(accessLockLineValue(line, "ProcessIdentifier").c_str(),
                                      nullptr, 10);
        } else if (accessLockLineMatchesKey(line, "AccessType")) {
            record.accessType = accessLockLineValue(line, "AccessType");
        }
    }

    return true;
}

static bool isAccessLockOwnedByCurrentProcess(const AccessLockRecord& record)
{
    return record.exists && record.pid == static_cast<unsigned long>(::getpid()) &&
           (record.hostName.empty() || record.hostName == currentHostName());
}

static bool isAccessLockActive(const AccessLockRecord& record)
{
    if (!record.exists || record.pid == 0) {
        return false;
    }
    const std::string host = currentHostName();
    if (!record.hostName.empty() && record.hostName != host) {
        return true;
    }
    return isLocalProcessAlive(record.pid);
}

static bool writeAccessLockFile(const std::string& path,
                                oaPlugIn::oaLibAccessEnum accessType)
{
    int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        return false;
    }

    FILE* fp = ::fdopen(fd, "w");
    if (fp == nullptr) {
        ::close(fd);
        return false;
    }

    const std::string host = currentHostName();
    const std::string login = currentLoginName();
    const time_t now = ::time(nullptr);
    fprintf(fp, "AppIdentifier=OpenAccess library access lock\n");
    fprintf(fp, "HostName=%s\n", host.c_str());
    fprintf(fp, "LoginName=%s\n", login.c_str());
    fprintf(fp, "ProcessIdentifier=%lu\n", static_cast<unsigned long>(::getpid()));
    fprintf(fp, "ProcessCreationTime_UTC=%lld\n", static_cast<long long>(now));
    fprintf(fp, "AccessType=%s\n",
            accessType == oaPlugIn::oacWriteLibAccess ? "write" : "read");
    fclose(fp);
    return true;
}

static bool tryAcquireAccessLock(const std::string& path,
                                 oaPlugIn::oaLibAccessEnum accessType)
{
    if (writeAccessLockFile(path, accessType)) {
        return true;
    }
    if (errno != EEXIST) {
        return false;
    }

    AccessLockRecord record;
    if (!readAccessLock(path, record)) {
        return false;
    }
    if (isAccessLockOwnedByCurrentProcess(record)) {
        return true;
    }
    if (!isAccessLockActive(record)) {
        ::unlink(path.c_str());
        return writeAccessLockFile(path, accessType);
    }
    return false;
}
} // anonymous namespace

namespace oaDMFileSys {

using namespace oaPlugIn;

oaDMFileSysComp::Factory oaDMFileSysComp::factory;
bool oaDMFileSysComp::exitHandlerSetUp = false;

oa::oaUInt4 oaDMFileSysComp::Factory::createInstance(oaCommon::IBase*, const oaCommon::Guid& id, void** instance) {
    auto* c = new oaDMFileSysComp();
    oa::oaUInt4 r = c->queryInterface(id, instance);
    if (r != 0) { delete c; *instance = nullptr; }
    return r;
}

oaDMFileSysComp::oaDMFileSysComp()
    : locking_(new Locking())
{
}

oaDMFileSysComp::~oaDMFileSysComp()
{
    releaseAccess();
    delete locking_;
    locking_ = nullptr;
}

unsigned long oaDMFileSysComp::addRef() { return ++refCount_; }
unsigned long oaDMFileSysComp::getRefCount() { return refCount_; }
unsigned long oaDMFileSysComp::release() { (void)refCount_; return 1; }

long oaDMFileSysComp::queryInterface(const oaCommon::Guid& id, void** iPtr) {
    if (!iPtr) return oaCommon::IBase::cFail;
    *iPtr = nullptr;
    auto eq = [&](const oaCommon::Guid& g) { return memcmp(&id, &g, sizeof(oaCommon::Guid)) == 0; };
    
    if (eq(oaPlugIn::IID_ILib))           { *iPtr = static_cast<ILib*>(this); }
    else if (eq(oaPlugIn::IID_IDMAccess))   { *iPtr = static_cast<IDMAccess*>(this); }
    else if (eq(oaPlugIn::IID_ILocking))    { *iPtr = static_cast<ILocking*>(this); }
    else if (eq(oaPlugIn::IID_IAccessControl)){ *iPtr = static_cast<IAccessControl*>(this); }
    else if (eq(oaPlugIn::IID_IDMSystemCaps)){ *iPtr = static_cast<IDMSystemCaps*>(this); }
    else if (eq(oaCommon::IID_IPlugInAbort)){ *iPtr = static_cast<oaCommon::IPlugInAbort*>(this); }
    else { return oaCommon::IBase::cNoInterface; }
    addRef();
    return oaCommon::IBase::cOK;
}

// IDMAccess — forward to OA's DM access object when one is provided.
void oaDMFileSysComp::add(oaPlugIn::IDMObject* object, bool checkExistence) {
    if (dmAccess_ != nullptr && dmAccess_ != static_cast<IDMAccess*>(this)) {
        dmAccess_->add(object, checkExistence);
    }
}

void oaDMFileSysComp::addLeader(oaPlugIn::IDMFile* file) {
    if (dmAccess_ != nullptr && dmAccess_ != static_cast<IDMAccess*>(this)) {
        dmAccess_->addLeader(file);
    }
}

void oaDMFileSysComp::create(oaPlugIn::IDMObject* object) {
    if (dmAccess_ != nullptr && dmAccess_ != static_cast<IDMAccess*>(this)) {
        dmAccess_->create(object);
    }
}

void oaDMFileSysComp::destroy(oaPlugIn::IDMObject* object) {
    if (dmAccess_ != nullptr && dmAccess_ != static_cast<IDMAccess*>(this)) {
        dmAccess_->destroy(object);
    }
}

void oaDMFileSysComp::getMappedName(IDMAccess::NameSpace fromNS,
                                    const char* fromName,
                                    IDMAccess::NameSpace toNS,
                                    oaCommon::IString*& toName) {
    toName = nullptr;
    if (dmAccess_ != nullptr && dmAccess_ != static_cast<IDMAccess*>(this)) {
        dmAccess_->getMappedName(fromNS, fromName, toNS, toName);
        if (toName != nullptr) {
            return;
        }
    }
    (void)fromNS;
    (void)toNS;
    if (fromName != nullptr) {
        toName = makePersistentString(fromName);
    }
}

oaCommon::SRef<IDMLib> oaDMFileSysComp::getDMLib() {
    if (dmAccess_ != nullptr && dmAccess_ != static_cast<IDMAccess*>(this)) {
        return dmAccess_->getDMLib();
    }
    return oaCommon::SRef<IDMLib>(nullptr);
}

// ILib — init
void oaDMFileSysComp::init(const char* n, const char* p, oaLibModeEnum m,
    const char* w, IDMAccess* a, oaCommon::IIter<IAttr*, &IID_IAttr>* attrs) {
    libName_ = n ? n : ""; libPath_ = p ? p : ""; writePath_ = w ? w : "";
    dmAccess_ = a; libReadOnly_ = (m == oacReadOnlyLibMode); (void)attrs;
    stringCache_.clear();
    if (locking_ != nullptr) {
        locking_->setLibPath(libPath_);
    }
}

// ILib — lib operations
bool oaDMFileSysComp::libExists(const char* p) {
    if (!p) return false;
    return access((std::string(p) + "/master.tag").c_str(), F_OK) == 0;
}
void oaDMFileSysComp::libPreCreate() {
    mkdir(libPath_.c_str(), 0777);
    FILE* f = fopen((libPath_ + "/master.tag").c_str(), "w");
    if (f) { fprintf(f, "-- Master.tag File, Rev:1.0\n"); fclose(f); }
    f = fopen((libPath_ + "/data.dm").c_str(), "w"); if (f) fclose(f);
    // .oalib — OA library identity marker (XML)
    f = fopen((libPath_ + "/.oalib").c_str(), "w");
    if (f) { fprintf(f, "<?xml version=\"1.0\"?>\n\n<Library DMSystem=\"oaAiviDMFileSys\">\n</Library>\n"); fclose(f); }
    // .alock is managed by IAccessControl::getAccess/releaseAccess.
}
void oaDMFileSysComp::libPostCreate() {}
void oaDMFileSysComp::libPreOpen() {}
void oaDMFileSysComp::libPostOpen() {}
void oaDMFileSysComp::libPreClose() {
    releaseAccess();
    filePaths_.clear();
    currentCell_.clear();
    currentView_.clear();
    viewTypes_.clear();
    cellViewTypes_.clear();
}

oaCommon::IString* oaDMFileSysComp::makePersistentString(const std::string& value) const
{
    stringCache_.emplace_back(new oaCommon::StringImp(value.c_str()));
    return stringCache_.back().get();
}

void oaDMFileSysComp::cacheFilePath(IDMFile* file, const std::string& path)
{
    if (file != nullptr && !path.empty()) {
        filePaths_[file] = path;
    }
}

bool oaDMFileSysComp::getCellViewNames(IDMFile* file, std::string& cell, std::string& view) const
{
    cell = currentCell_;
    view = currentView_;
    bool ok = resolveCellViewContext(file, cell, view);
    if (!ok) {
        ok = !cell.empty() && !view.empty();
    }
    return ok;
}

bool oaDMFileSysComp::isPrimaryLogicalName(const std::string& logicalName) const
{
    if (logicalName.empty()) {
        return false;
    }
    if (logicalName.find('.') == std::string::npos) {
        return true;
    }
    return logicalName == "sch.oa" || logicalName == "symbol.oa" ||
           logicalName == "layout.oa";
}

std::string oaDMFileSysComp::resolvePrimaryFileName(const std::string& cell,
                                                    const std::string& view,
                                                    const std::string& logicalName) const
{
    if (cell.empty() || view.empty() || logicalName.empty()) {
        return logicalName;
    }
    return readPrimaryFileFromMasterTag(buildCellViewPath(cell.c_str(), view.c_str()),
                                        logicalName);
}

bool oaDMFileSysComp::getCachedFilePath(IDMFile* file, std::string& path) const
{
    if (file == nullptr) {
        return false;
    }
    std::map<IDMFile*, std::string>::const_iterator it = filePaths_.find(file);
    if (it != filePaths_.end() && !it->second.empty()) {
        path = it->second;
        return true;
    }
    path = buildFilePath(file);
    return !path.empty();
}

// ILib — cell
void oaDMFileSysComp::cellCreate(const char* cn) {
    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) { fprintf(log, "cellCreate: cn=%s\n", cn ? cn : "(null)"); fclose(log); }
    currentCell_ = cn ? cn : "";
    std::string d = buildCellPath(cn);
    mkdir(d.c_str(), 0777);
    FILE* f = fopen((d + "data.dm").c_str(), "w"); if (f) fclose(f);
}
bool oaDMFileSysComp::cellFind(const char* cn) {
    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) { fprintf(log, "cellFind: cn=%s\n", cn ? cn : "(null)"); fclose(log); }
    
    currentCell_ = cn ? cn : "";
    std::string p = buildCellPath(cn);
    struct stat st;
    bool found = (::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    
    log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) {
        fprintf(log, "  → currentCell_='%s', found=%d\n", currentCell_.c_str(), (int)found);
        fclose(log);
    }
    return found;
}
bool oaDMFileSysComp::cellValidate(const char*) { return true; }
void oaDMFileSysComp::cellPreFind() {}
void oaDMFileSysComp::cellValidateDestroy(ICell* cell) {
    if (libReadOnly_) return;
    // cell may be NULL if called after nameTbl.remove; fall back to currentCell_
    const char* cname = nullptr;
    if (cell) {
        oaCommon::SRef<oaCommon::IString> nameRef = cell->getName();
        cname = static_cast<const char*>(nameRef);
    }
    if (!cname || !*cname) {
        cname = currentCell_.c_str();
    }
    if (!cname || !*cname) return;
    std::string cellDir = buildCellPath(cname);
    if (!cellDir.empty() && cellDir.back() == '/') cellDir.pop_back();
}

void oaDMFileSysComp::cellDestroy(ICell* cell) {
    // OA calls nameTbl.remove(index) BEFORE cellDestroy, so getICell(cIdx) may be NULL.
    // Fall back to currentCell_ cache (set during cellCreate/cellFind).
    const char* cname = nullptr;
    if (cell) {
        oaCommon::SRef<oaCommon::IString> nameRef = cell->getName();
        cname = static_cast<const char*>(nameRef);
    }
    if (!cname || !*cname) {
        cname = currentCell_.c_str();
    }
    if (!cname || !*cname) return;
    std::string cellDir = buildCellPath(cname);
    if (!cellDir.empty() && cellDir.back() == '/') cellDir.pop_back();
    removeDirectoryRecursive(cellDir);
}

// ILib — view
void oaDMFileSysComp::viewCreate(const char* vn, const char* vt) {
    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) { fprintf(log, "viewCreate: vn=%s, vt=%s\n", vn ? vn : "(null)", vt ? vt : "(null)"); fclose(log); }
    currentView_ = vn ? vn : "";
    if (vn && *vn) {
        viewTypes_[vn] = vt ? vt : "";
    }
}
bool oaDMFileSysComp::viewFind(const char* vn, const char* vt) {
    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) { fprintf(log, "viewFind: vn=%s, vt=%s\n", vn ? vn : "(null)", vt ? vt : "(null)"); fclose(log); }
    bool found = false;
    if (vn && *vn) {
        std::string resolvedVt;

        auto it = viewTypes_.find(vn);
        if (it != viewTypes_.end()) {
            resolvedVt = it->second;
            found = true;
        } else {
            // Fallback: discover the view from existing cellview directories.
            DIR* dir = opendir(libPath_.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    const char* cname = entry->d_name;
                    if (strcmp(cname, ".") == 0 || strcmp(cname, "..") == 0) continue;
                    if (cname[0] == '.') continue;

                    std::string cellDir = buildCellPath(cname);
                    struct stat st;
                    if (::stat(cellDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

                    std::string cvDir = buildCellViewPath(cname, vn);
                    if (::stat(cvDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) continue;

                    char diskVt[256] = {0};
                    if (cellViewFindViewType(cname, vn, diskVt)) {
                        resolvedVt = diskVt;
                        viewTypes_[vn] = resolvedVt;
                        found = true;
                        break;
                    }
                }
                closedir(dir);
            }
        }

        if (found && vt && *vt) {
            found = (resolvedVt == vt);
        }
    }

    if (found) {
        currentView_ = vn ? vn : "";
    }

    log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) {
        fprintf(log, "  → viewFind result=%d\n", (int)found);
        fclose(log);
    }
    return found;
}
bool oaDMFileSysComp::viewValidate(const char*, const char*) { return true; }
void oaDMFileSysComp::viewPreFind(const char*) {}
void oaDMFileSysComp::viewPreFind() {}
void oaDMFileSysComp::viewValidateDestroy(IView* view) {
    if (!view) return;
    oaCommon::SRef<oaCommon::IString> nameRef = view->getName();
    const char* vname = static_cast<const char*>(nameRef);
}

void oaDMFileSysComp::viewDestroy(IView* view) {
    if (!view) return;
    oaCommon::SRef<oaCommon::IString> nameRef = view->getName();
    const char* vname = static_cast<const char*>(nameRef);
}

// ILib — cellView
void oaDMFileSysComp::cellViewCreate(const char* cn, const char* vn, const char* vt) {
    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) {
        fprintf(log, "cellViewCreate: cn=%s, vn=%s, vt=%s\n", cn ? cn : "(null)", vn ? vn : "(null)", vt ? vt : "(null)");
        fclose(log);
    }
    currentCell_ = cn ? cn : ""; currentView_ = vn ? vn : "";
    if (vn && *vn) {
        viewTypes_[vn] = vt ? vt : "";
    }
    if (cn && *cn && vn && *vn) {
        cellViewTypes_[std::string(cn) + "/" + vn] = vt ? vt : "";
    }
    std::string d = buildCellViewPath(cn, vn);
    mkdir(d.c_str(), 0777);
    FILE* f = fopen((d + "data.dm").c_str(), "w"); if (f) fclose(f);
    f = fopen((d + "master.tag").c_str(), "w");
    if (f) { fprintf(f, "-- Master.tag File, Rev:1.0\nViewType=%s\n", vt ? vt : ""); fclose(f); }
}
bool oaDMFileSysComp::cellViewFind(const char* cn, const char* vn, const char* vt) {
    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) { fprintf(log, "cellViewFind: cn=%s, vn=%s, vt=%s\n", cn ? cn : "(null)", vn ? vn : "(null)", vt ? vt : "(null)"); fclose(log); }
    
    // Set currentCell_ and currentView_ for subsequent fileFind/fileCreate calls
    currentCell_ = cn ? cn : "";
    currentView_ = vn ? vn : "";
    
    bool found = false;
    std::string key;
    if (cn && *cn && vn && *vn) {
        key = std::string(cn) + "/" + vn;
        auto it = cellViewTypes_.find(key);
        if (it != cellViewTypes_.end()) {
            found = (!vt || it->second.empty() || it->second == vt);
        }
    }

    struct stat st;
    std::string p = buildCellViewPath(cn, vn);
    if (!found && ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (!vt || !*vt) {
            found = true;
        } else {
            char actualVt[256] = {0};
            if (cellViewFindViewType(cn, vn, actualVt)) {
                found = (strcmp(actualVt, vt) == 0);
                if (found && !key.empty()) {
                    cellViewTypes_[key] = actualVt;
                    viewTypes_[vn] = actualVt;
                }
            }
        }
    }

    log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) {
        fprintf(log, "  → path=%s, currentCell_='%s', currentView_='%s', found=%d\n", 
                p.c_str(), currentCell_.c_str(), currentView_.c_str(), (int)found);
        fclose(log);
    }
    return found;
}
bool oaDMFileSysComp::cellViewFindViewType(const char* cn, const char* vn, char* vt) {
    // The ViewType= entry in master.tag is the FILE FORMAT type, not the OA viewType name.
    // Infer the OA viewType from the view name using the project naming convention:
    //   symbol*          → schematicSymbol
    //   schematic        → schematic
    //   layout           → maskLayout
    //   netlist          → netlist
    //   functional       → functional
    //   auCdl/cdl        → cdlNetlist
    //   verilog          → verilogNetlist
    //   vhdl             → vhdlNetlist
    //   spice            → spiceNetlist
    //   spectre          → spectreNetlist
    //   hspice/hspiceD   → hspiceNetlist
    //   lvs              → lvsNetlist
    //   UltraSim         → ultraSimNetlist
    //   dummy/custom     → use view name as viewType

    if (!vt) return false;
    *vt = 0;

    if (vn && *vn) {
        if (cn && *cn) {
            auto cvIt = cellViewTypes_.find(std::string(cn) + "/" + vn);
            if (cvIt != cellViewTypes_.end() && !cvIt->second.empty()) {
                strncpy(vt, cvIt->second.c_str(), 255);
                vt[255] = 0;
                return true;
            }
        }
    }

    // Use existing on-disk cellview metadata to determine the OA viewType.
    // If the cellview does not exist yet, return false so OA can create it.
    std::string viewName(vn ? vn : "");

    if (cn && *cn && vn && *vn) {
        std::string cvDir = buildCellViewPath(cn, vn);
        struct stat st;
        if (::stat(cvDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
            return false;
        }
    } else {
        return false;
    }

    // Symbol variants → schematicSymbol
    if (viewName == "symbol" || viewName == "symbol_xform" ||
        viewName.substr(0, 7) == "symbol" && viewName.size() > 6) {
        // symbolr, symboll, symbolNN, symbolrOff, symbollOff,
        // symbolr_xform, symboll_xform, symbolrOff_xform, symbollOff_xform
        strncpy(vt, "schematicSymbol", 255);
        vt[255] = 0;
        return true;
    }

    // Direct mappings for common view names
    static const struct { const char* name; const char* viewType; } nameMap[] = {
        { "schematic",    "schematic" },
        { "layout",       "maskLayout" },
        { "maskLayout",   "maskLayout" },
        { "netlist",      "netlist" },
        { "functional",   "functional" },
        { "auCdl",        "cdlNetlist" },
        { "cdl",          "cdlNetlist" },
        { "verilog",      "verilogNetlist" },
        { "vhdl",         "vhdlNetlist" },
        { "spice",        "spiceNetlist" },
        { "spectre",      "spectreNetlist" },
        { "hspice",       "hspiceNetlist" },
        { "hspiceD",      "hspiceNetlist" },
        { "lvs",          "lvsNetlist" },
        { "auLvs",        "lvsNetlist" },
        { "UltraSim",     "ultraSimNetlist" },
        { "compose",      "schematicSymbol" },
        { "autoLayout",   "maskLayout" },
        { "dc",           "schematic" },
        { nullptr, nullptr }
    };

    for (int i = 0; nameMap[i].name; i++) {
        if (viewName == nameMap[i].name) {
            strncpy(vt, nameMap[i].viewType, 255);
            vt[255] = 0;
            return true;
        }
    }

    // Fallback: use view name as viewType
    strncpy(vt, viewName.c_str(), 255);
    vt[255] = 0;
    return true;
}
bool oaDMFileSysComp::cellViewValidate(const char*, const char*, const char*) { return true; }
void oaDMFileSysComp::cellViewPreFind(ICell*) {}
void oaDMFileSysComp::cellViewPreFind(IView*) {}
void oaDMFileSysComp::cellViewPreFind() {}
void oaDMFileSysComp::cellViewPreSetView(ICellView*, IView*, IDMFile*) {}
void oaDMFileSysComp::cellViewPostSetView(ICellView*) {}
void oaDMFileSysComp::cellViewValidateDestroy(ICellView* cv) {
    if (!cv || libReadOnly_) {
        return;
    }
    oaCommon::SRef<oaCommon::IString> cn = cv->getCellName();
    oaCommon::SRef<oaCommon::IString> vn = cv->getViewName();
    const char* cname = static_cast<const char*>(cn);
    const char* vname = static_cast<const char*>(vn);
    std::string cvDir = buildCellViewPath(cname, vname);
    if (!cvDir.empty() && cvDir.back() == '/') cvDir.pop_back();
    struct stat st;
    bool exists = (::stat(cvDir.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

void oaDMFileSysComp::cellViewDestroy(ICellView* cv) {
    if (!cv) {
        return;
    }
    oaCommon::SRef<oaCommon::IString> cn = cv->getCellName();
    oaCommon::SRef<oaCommon::IString> vn = cv->getViewName();
    const char* cname = static_cast<const char*>(cn);
    const char* vname = static_cast<const char*>(vn);
    std::string cvDir = buildCellViewPath(cname, vname);
    if (!cvDir.empty() && cvDir.back() == '/') cvDir.pop_back();
    bool ok = removeDirectoryRecursive(cvDir);
}

// ILib — file
void oaDMFileSysComp::fileCreate(const char* n, IDMObject* p, bool) {
    if (!n) {
        return;
    }
    std::string cell = currentCell_;
    std::string view = currentView_;
    resolveCellViewContext(p, cell, view);
    if (!cell.empty()) {
        currentCell_ = cell;
    }
    if (!view.empty()) {
        currentView_ = view;
    }
    std::string fp = buildFilePath(p, n);
    if (fp.empty()) {
        return;
    }
    createDirectory(dirnameOf(fp));
    FILE* f = fopen(fp.c_str(), "ab");
    if (f) {
        fclose(f);
    }
}

bool oaDMFileSysComp::fileFind(const char* n, IDMObject* p) {
    if (!n) {
        return false;
    }
    std::string cell = currentCell_;
    std::string view = currentView_;
    resolveCellViewContext(p, cell, view);
    if (!cell.empty()) {
        currentCell_ = cell;
    }
    if (!view.empty()) {
        currentView_ = view;
    }

    std::string path = buildFilePath(p, n);
    return !path.empty() && (::access(path.c_str(), F_OK) == 0);
}

bool oaDMFileSysComp::fileValidate(const char* n, IDMObject* p) { return fileFind(n, p); }
void oaDMFileSysComp::filePreFind(IDMObject*) {}
void oaDMFileSysComp::filePreSetLeader(IDMFile*, IDMFile*) {}
void oaDMFileSysComp::filePostSetLeader(IDMFile*) {}
void oaDMFileSysComp::filePreSetName(IDMFile*, const char*) {}
void oaDMFileSysComp::filePostSetName(IDMFile*) {}

void oaDMFileSysComp::fileValidateDestroy(IDMFile* file) {
    if (!file || libReadOnly_) {
        return;
    }
    std::string fp;
    if (getCachedFilePath(file, fp)) {
        ::access(fp.c_str(), W_OK);
    }
}

void oaDMFileSysComp::fileValidateDestroy(IDMFile* file, oaSaveRecoverTypeEnum type) {
    if (!file || libReadOnly_) {
        return;
    }
    std::string path = buildSaveRecoverPath(file, type);
    if (!path.empty()) {
        ::access(path.c_str(), W_OK);
    }
}

void oaDMFileSysComp::fileDestroy(IDMFile* file) {
    if (!file) {
        return;
    }
    std::string fp;
    if (getCachedFilePath(file, fp)) {
        ::unlink(fp.c_str());
        ::unlink(buildCachePath(file).c_str());
        ::unlink(buildSaveRecoverPath(file, oacAutoSaveType).c_str());
        ::unlink(buildSaveRecoverPath(file, oacCriticalSaveType).c_str());
        ::unlink((fp + editLockFileExt).c_str());
        ::unlink((fp + editLockFileRhelLinkExt).c_str());
    }
    filePaths_.erase(file);
}

void oaDMFileSysComp::fileDestroy(IDMFile* file, oaSaveRecoverTypeEnum type) {
    if (!file) {
        return;
    }
    std::string path = buildSaveRecoverPath(file, type);
    if (!path.empty()) {
        ::unlink(path.c_str());
    }
}

bool oaDMFileSysComp::fileGetCache(IDMFile* file, oaCommon::IString*& path) {
    path = nullptr;
    if (!file) {
        return false;
    }

    std::string basePath;
    if (!getCachedFilePath(file, basePath) || ::access(basePath.c_str(), F_OK) != 0) {
        return false;
    }

    std::string cachePath = buildCachePath(file);
    if (cachePath.empty()) {
        return false;
    }

    if (::access(cachePath.c_str(), F_OK) != 0) {
        if (::link(basePath.c_str(), cachePath.c_str()) != 0) {
            if (!copyFileContents(basePath, cachePath)) {
                return false;
            }
        }
    }

    path = makePersistentString(makeRelativeToRoot(libPath_, cachePath));
    return true;
}

void oaDMFileSysComp::fileReleaseCache(IDMFile* file, const char* path) {
    std::string cachePath;
    if (path && *path) {
        cachePath = path;
        if (!cachePath.empty() && cachePath[0] != '/') {
            cachePath = joinPath(trimTrailingSlashes(libPath_), cachePath);
        }
    } else if (file) {
        cachePath = buildCachePath(file);
    }
    if (!cachePath.empty()) {
        ::unlink(cachePath.c_str());
    }
}

// ILib — paths
void oaDMFileSysComp::getPath(IDMFile* file, oaCommon::IString*& p) {
    p = nullptr;
    if (!file) {
        return;
    }
    std::string fullPath = buildFilePath(file);
    if (fullPath.empty()) {
        return;
    }
    cacheFilePath(file, fullPath);
    p = makePersistentString(makeRelativeToRoot(libPath_, fullPath));
}

void oaDMFileSysComp::getTempFile(IDMObject*, bool, oaCommon::IString*& p) {
    std::string dir = libPath_;
    if (!currentCell_.empty() && !currentView_.empty()) {
        dir = buildCellViewPath(currentCell_.c_str(), currentView_.c_str());
        mkdir(buildCellPath(currentCell_.c_str()).c_str(), 0777);
        mkdir(dir.c_str(), 0777);
    } else if (!currentCell_.empty()) {
        dir = buildCellPath(currentCell_.c_str());
        mkdir(dir.c_str(), 0777);
    }

    std::string templ = dir;
    if (!templ.empty() && templ.back() != '/') templ += "/";
    templ += ".oa_tmp_XXXXXX";

    std::vector<char> buf(templ.begin(), templ.end());
    buf.push_back('\0');

    int fd = ::mkstemp(buf.data());
    if (fd >= 0) {
        ::close(fd);
    }

    p = makePersistentString(buf.data());

    FILE* log = fopen("/tmp/dmfilesys_debug.log", "a");
    if (log) {
        fprintf(log, "getTempFile: path=%s\n", buf.data());
        fclose(log);
    }
}
void oaDMFileSysComp::getDMSystemName(oaCommon::IString*& n) { static oaCommon::StringImp nm("oaAiviDMFileSys"); n = &nm; }
void oaDMFileSysComp::getAttributes(oaCommon::IIter<IAttr*, &IID_IAttr>*& a) { a = nullptr; }
void oaDMFileSysComp::setAttributes(oaCommon::IIter<IAttr*, &IID_IAttr>*) {}
void oaDMFileSysComp::getPath(IDMFile* file, oaSaveRecoverTypeEnum type, oaCommon::IString*& p) {
    p = nullptr;
    if (!file) {
        return;
    }
    std::string path = buildSaveRecoverPath(file, type);
    if (!path.empty()) {
        p = makePersistentString(makeRelativeToRoot(libPath_, path));
    }
}

void oaDMFileSysComp::fileCreate(IDMFile* file, oaSaveRecoverTypeEnum type) {
    if (!file) {
        return;
    }
    std::string dstPath = buildSaveRecoverPath(file, type);
    if (dstPath.empty()) {
        return;
    }
    createDirectory(dirnameOf(dstPath));
    std::string srcPath;
    if (getCachedFilePath(file, srcPath) && ::access(srcPath.c_str(), F_OK) == 0) {
        copyFileContents(srcPath, dstPath);
    } else {
        FILE* f = fopen(dstPath.c_str(), "wb");
        if (f) {
            fclose(f);
        }
    }
}

bool oaDMFileSysComp::exists(IDMFile* file, oaSaveRecoverTypeEnum type) {
    if (!file) {
        return false;
    }
    std::string path = buildSaveRecoverPath(file, type);
    return !path.empty() && (::access(path.c_str(), F_OK) == 0);
}

// IAccessControl
bool oaDMFileSysComp::getAccess(oaLibAccessEnum accessType, oa::oaUInt4 timeOut) {
    const std::string root = trimTrailingSlashes(libPath_);
    if (root.empty()) {
        return false;
    }

    if (accessType == oacReadLibAccess) {
        return ::access(root.c_str(), R_OK | X_OK) == 0;
    }

    if (libReadOnly_ || ::access(root.c_str(), W_OK | X_OK) != 0) {
        return false;
    }

    if (accessLocked_) {
        ++accessLockCount_;
        return true;
    }

    accessLockPath_ = joinPath(root, ".alock");
    const oa::oaUInt4 maxAttempts = timeOut == 0 ? 1 : (timeOut * 10 + 1);
    for (oa::oaUInt4 attempt = 0; attempt < maxAttempts; ++attempt) {
        if (tryAcquireAccessLock(accessLockPath_, accessType)) {
            accessLocked_ = true;
            accessLockCount_ = 1;
            return true;
        }
        if (attempt + 1 < maxAttempts) {
            ::usleep(100000);
        }
    }

    return false;
}

void oaDMFileSysComp::releaseAccess() {
    if (!accessLocked_) {
        return;
    }

    if (accessLockCount_ > 1) {
        --accessLockCount_;
        return;
    }

    AccessLockRecord record;
    if (!accessLockPath_.empty() &&
        readAccessLock(accessLockPath_, record) &&
        isAccessLockOwnedByCurrentProcess(record)) {
        ::unlink(accessLockPath_.c_str());
    }

    accessLocked_ = false;
    accessLockCount_ = 0;
    accessLockPath_.clear();
}

// ILocking
bool oaDMFileSysComp::lock(IDMFile* file) {
    if (!file || locking_ == nullptr) {
        return false;
    }
    std::string path = buildFilePath(file);
    if (path.empty()) {
        return false;
    }
    cacheFilePath(file, path);
    return locking_->exclusiveLock(path);
}

void oaDMFileSysComp::unlock(IDMFile* file) {
    if (!file || locking_ == nullptr) {
        return;
    }
    std::string path = buildFilePath(file);
    if (!path.empty()) {
        locking_->exclusiveUnLock(path);
    }
}

oa::oaUInt4 oaDMFileSysComp::getLockStatus(IDMFile* file) {
    if (!file || locking_ == nullptr) {
        return oacNotLocked;
    }
    std::string path = buildFilePath(file);
    if (path.empty()) {
        return oacNotLocked;
    }
    return locking_->getLockStatusPvt(file);
}

void oaDMFileSysComp::setPlugInMessage(IPlugInMessage* plugInMsg) {
    if (locking_ != nullptr) {
        locking_->setPlugInMessage(plugInMsg);
    }
    FileLocking::get().setPlugInMessage(plugInMsg);
}

// IDMSystemCaps
bool oaDMFileSysComp::queryCapability(const char* n) { return n && strcmp(n, "MaxCellViews") == 0; }
bool oaDMFileSysComp::getMetric(const char*, double& m) { m = 1e6; return true; }
bool oaDMFileSysComp::getMetric(const char*, oa::oaUInt4& m) { m = 1024; return true; }

// IPlugInAbort
void oaDMFileSysComp::onAbort() {
    releaseAccess();
}

// Internal helpers
std::string oaDMFileSysComp::buildLibPath() const { return libPath_ + "/"; }
std::string oaDMFileSysComp::buildCellPath(const char* n) const { return libPath_ + "/" + (n ? n : "") + "/"; }
std::string oaDMFileSysComp::buildCellViewPath(const char* cn, const char* vn) const { return libPath_ + "/" + (cn ? cn : "") + "/" + (vn ? vn : "") + "/"; }
std::string oaDMFileSysComp::buildFilePath(IDMObject* parent, const char* n) const {
    if (!n || !*n) {
        return "";
    }

    std::string logicalName(n);
    std::string root = trimTrailingSlashes(libPath_);

    if (parent == nullptr) {
        std::string cell = currentCell_;
        std::string view = currentView_;
        if (!cell.empty() && !view.empty()) {
            std::string actualName = resolvePrimaryFileName(cell, view, logicalName);
            return joinPath(buildCellViewPath(cell.c_str(), view.c_str()), actualName);
        }
        if (!cell.empty()) {
            return joinPath(buildCellPath(cell.c_str()), logicalName);
        }
        return joinPath(root, logicalName);
    }

    ICellView* cv = nullptr;
    if (parent->queryInterface(IID_ICellView, reinterpret_cast<void**>(&cv)) == oaCommon::IBase::cOK && cv) {
        std::string cell = toStdString(cv->getCellName());
        std::string view = toStdString(cv->getViewName());
        cv->release();
        std::string actualName = resolvePrimaryFileName(cell, view, logicalName);
        return joinPath(buildCellViewPath(cell.c_str(), view.c_str()), actualName);
    }

    ICell* cell = nullptr;
    if (parent->queryInterface(IID_ICell, reinterpret_cast<void**>(&cell)) == oaCommon::IBase::cOK && cell) {
        std::string cellName = toStdString(cell->getName());
        cell->release();
        return joinPath(buildCellPath(cellName.c_str()), logicalName);
    }

    IDMLib* lib = nullptr;
    if (parent->queryInterface(IID_IDMLib, reinterpret_cast<void**>(&lib)) == oaCommon::IBase::cOK && lib) {
        lib->release();
        return joinPath(root, logicalName);
    }

    IDMFile* file = nullptr;
    if (parent->queryInterface(IID_IDMFile, reinterpret_cast<void**>(&file)) == oaCommon::IBase::cOK && file) {
        IDMObject* fileParent = nullptr;
        file->getParent(fileParent);
        std::string path = buildFilePath(fileParent, n);
        if (fileParent) {
            fileParent->release();
        }
        file->release();
        return path;
    }

    return joinPath(root, logicalName);
}

std::string oaDMFileSysComp::buildFilePath(IDMFile* file) const
{
    if (file == nullptr) {
        return "";
    }
    std::map<IDMFile*, std::string>::const_iterator it = filePaths_.find(file);
    if (it != filePaths_.end() && !it->second.empty()) {
        return it->second;
    }

    oaCommon::SRef<oaCommon::IString> nameRef = file->getName();
    const char* name = nameRef ? static_cast<const char*>(nameRef) : "";
    IDMObject* parent = nullptr;
    file->getParent(parent);
    std::string path = buildFilePath(parent, name);
    if (parent) {
        parent->release();
    }
    return path;
}

std::string oaDMFileSysComp::buildPhysicalFilePath(IDMObject* parent, const char* n) const {
    return buildFilePath(parent, n);
}

std::string oaDMFileSysComp::buildSaveRecoverPath(IDMFile* file, oaSaveRecoverTypeEnum type) const
{
    std::string basePath = buildFilePath(file);
    if (basePath.empty()) {
        return "";
    }
    return replaceDotOaExtension(basePath,
                                 type == oacCriticalSaveType ? criticalSaveExt : autoSaveExt);
}

std::string oaDMFileSysComp::buildCachePath(IDMFile* file) const
{
    std::string basePath = buildFilePath(file);
    if (basePath.empty()) {
        return "";
    }
    return joinPath(dirnameOf(basePath), ".oacache");
}

void oaDMFileSysComp::createDirectory(const std::string& p) {
    if (p.empty()) {
        return;
    }
    std::string path = trimTrailingSlashes(p);
    if (path.empty()) {
        return;
    }
    if (directoryExists(path)) {
        return;
    }

    std::string parent = dirnameOf(path);
    if (!parent.empty() && parent != path) {
        createDirectory(parent);
    }
    ::mkdir(path.c_str(), 0777);
}

bool oaDMFileSysComp::directoryExists(const std::string& p) {
    struct stat st; return (::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}
void oaDMFileSysComp::createDataDMFile(const std::string& d) {
    std::string p = d + (d.empty() || d.back()=='/' ? "" : "/") + "data.dm";
    FILE* f = fopen(p.c_str(), "w"); if (f) fclose(f);
}
bool oaDMFileSysComp::fileExistsOnDisk(const std::string& p) { return access(p.c_str(), F_OK) == 0; }
void oaDMFileSysComp::writeFile(const std::string& p, const std::string& c) {
    FILE* f = fopen(p.c_str(), "w"); if (f) { fwrite(c.c_str(), 1, c.size(), f); fclose(f); }
}
std::string oaDMFileSysComp::readFile(const std::string& p) {
    std::string r; FILE* f = fopen(p.c_str(), "r");
    if (f) { char b[4096]; size_t n; while ((n = fread(b, 1, sizeof(b), f)) > 0) r.append(b, n); fclose(f); }
    return r;
}
void oaDMFileSysComp::collectDirectory(const std::string& dirPath, std::set<std::string>& entries) {
    DIR* dir = opendir(dirPath.c_str());
    if (!dir) {
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strcmp(name, "data.dm") == 0 || strcmp(name, "master.tag") == 0) continue;
        entries.insert(name);
    }
    closedir(dir);
}

} // namespace oaDMFileSys
