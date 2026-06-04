// *****************************************************************************
// oaAiviDMFileSysLocking.cpp — Locking and FileLocking implementation
//
// Reverse-engineered from liboaDMFileSys.so v22.61.p005
// Implements .cdslck file-based locking for OpenAccess design data.
//
// Lock file format (.cdslck):
//   AppIdentifier         OpenAccess edit lock
//   HostName              <hostname>
//   LoginName             <username>
//   ProcessIdentifier     <pid>
//   ProcessCreationTime_UTC <epoch>
//   TimeEditLocked        <timestamp>
//   FilePathUsedToEditLock <data_path>
//   LockStakeVersion      2.0
//   OSType                Unix
//   ReasonForPlacingEditLock <reason>
//
// Concurrency notes:
//   - Lock creation uses O_EXCL for atomicity
//   - Stale lock detection uses kill(pid, 0) to check if locker is alive
//   - The FileLocking singleton manages process identity (PID, host, user)
// *****************************************************************************

#include "oaDMFileSysComp.h"
#include "oaDMFileSysBase.h"

#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

namespace oaDMFileSys {

// Process-level identity (populated on first use)
static std::string g_hostName;
static std::string g_loginName;
static unsigned int g_pid = 0;
static time_t g_procStart = 0;
static bool g_identityInited = false;
static std::set<std::string> g_ownedDataPaths;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void ensureIdentity()
{
    if (g_identityInited) return;
    g_identityInited = true;

    g_pid = static_cast<unsigned int>(::getpid());
    g_procStart = ::time(nullptr);

    // Hostname
    char hostBuf[256];
    if (gethostname(hostBuf, sizeof(hostBuf)) == 0) {
        hostBuf[sizeof(hostBuf) - 1] = '\0';
        g_hostName = hostBuf;
    } else {
        g_hostName = "unknown";
    }

    // Login name
    const char* user = getenv("USER");
    if (!user || !*user) user = getenv("LOGNAME");
    if (!user || !*user) user = "unknown";
    g_loginName = user;

    // Process start time—read /proc/self/stat
    g_procStart = ::time(nullptr);
    std::ifstream stat("/proc/self/stat");
    if (stat.is_open()) {
        std::string line;
        std::getline(stat, line);
        // field 22 in /proc/self/stat is starttime (jiffies since boot)
        // We approximate by just recording current epoch—good enough for OA
        // In production OA they parse the stat file properly.
        // For safety we read /proc/self/stat field 22:
        int field = 0;
        size_t pos = 0;
        // skip process name in parens
        pos = line.find(')');
        if (pos != std::string::npos) {
            std::istringstream fields(line.substr(pos + 2));
            std::string token;
            while (fields >> token) {
                if (++field == 20) {        // starttime is field 22 (1-indexed),
                    // after ')' next is state(3), then ppid(4)... starttime=22
                    // Actually: after ')', fields are: state, ppid, pgrp, session, tty_nr, tpgid, flags, minflt, cminflt, majflt, cmajflt, utime, stime, cutime, cstime, priority, nice, num_threads, itrealvalue, starttime
                    // That's 20 fields after ')'
                    unsigned long long startJiffies = std::stoull(token);
                    // Get system uptime from /proc/uptime and CLK_TCK
                    long clkTck = sysconf(_SC_CLK_TCK);
                    if (clkTck <= 0) clkTck = 100;
                    std::ifstream uptime("/proc/uptime");
                    double upSecs = 0;
                    if (uptime.is_open()) {
                        uptime >> upSecs;
                    }
                    g_procStart = static_cast<time_t>(g_procStart - upSecs + (startJiffies / static_cast<double>(clkTck)));
                    break;
                }
            }
        }
    }
}

// Format a timestamp string matching OA's format: "Thu May 17 10:30:45 2026"
static std::string formatTime(time_t t)
{
    char buf[64];
    struct tm tmVal;
    localtime_r(&t, &tmVal);
    strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", &tmVal);
    return std::string(buf);
}

// Construct the path to the .cdslck file for a given data file path
static std::string lockFilePath(const std::string& dataPath)
{
    return dataPath + editLockFileExt;  // ".cdslck"
}

// Check if a process is alive by sending signal 0
static bool isProcessAlive_pid(unsigned int pid)
{
    if (pid == 0) return false;
    if (kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

static std::string trim(const std::string& value)
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

static bool hasSuffix(const std::string& value, const char* suffix)
{
    if (suffix == nullptr) {
        return false;
    }
    const size_t suffixLen = std::strlen(suffix);
    return value.size() >= suffixLen &&
           value.compare(value.size() - suffixLen, suffixLen, suffix) == 0;
}

static std::string trimTrailingSlashes(const std::string& path)
{
    std::string trimmed(path);
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }
    return trimmed;
}

static std::string joinPath(const std::string& lhs, const std::string& rhs)
{
    if (lhs.empty()) {
        return rhs;
    }
    if (rhs.empty()) {
        return lhs;
    }
    if (!rhs.empty() && rhs[0] == '/') {
        return rhs;
    }
    if (lhs.back() == '/') {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

static std::string toStdString(const oaCommon::SRef<oaCommon::IString>& value)
{
    if (!value) {
        return "";
    }
    const char* str = static_cast<const char*>(value);
    return str ? str : "";
}

static bool lockLineMatchesKey(const std::string& line, const char* key)
{
    const size_t keyLen = std::strlen(key);
    if (line.compare(0, keyLen, key) != 0) {
        return false;
    }
    return line.size() == keyLen || line[keyLen] == '=' ||
           std::isspace(static_cast<unsigned char>(line[keyLen]));
}

static std::string lockLineValue(const std::string& line, const char* key)
{
    const size_t keyLen = std::strlen(key);
    if (line.size() <= keyLen) {
        return "";
    }
    if (line[keyLen] == '=') {
        return trim(line.substr(keyLen + 1));
    }
    return trim(line.substr(keyLen));
}

struct LockRecord {
    bool exists = false;
    std::string hostName;
    std::string loginName;
    unsigned int pid = 0;
    time_t processCreationTime = 0;
    std::string dataPath;
};

static bool readLockRecord(const std::string& lckPath, LockRecord& record)
{
    std::ifstream input(lckPath.c_str());
    if (!input) {
        return false;
    }

    record = LockRecord();
    record.exists = true;

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (lockLineMatchesKey(line, "HostName")) {
            record.hostName = lockLineValue(line, "HostName");
        } else if (lockLineMatchesKey(line, "LoginName")) {
            record.loginName = lockLineValue(line, "LoginName");
        } else if (lockLineMatchesKey(line, "ProcessIdentifier")) {
            record.pid = static_cast<unsigned int>(
                std::strtoul(lockLineValue(line, "ProcessIdentifier").c_str(), nullptr, 10));
        } else if (lockLineMatchesKey(line, "ProcessCreationTime_UTC")) {
            record.processCreationTime = static_cast<time_t>(
                std::strtoll(lockLineValue(line, "ProcessCreationTime_UTC").c_str(), nullptr, 10));
        } else if (lockLineMatchesKey(line, "FilePathUsedToEditLock")) {
            record.dataPath = lockLineValue(line, "FilePathUsedToEditLock");
        }
    }

    return true;
}

static bool isOwnedByCurrentProcess(const LockRecord& record)
{
    ensureIdentity();
    if (!record.exists || record.pid != g_pid) {
        return false;
    }
    if (!record.hostName.empty() && record.hostName != g_hostName) {
        return false;
    }
    if (record.processCreationTime != 0 &&
        record.processCreationTime != g_procStart) {
        return false;
    }
    return true;
}

static bool isLockOwnerAlive(const LockRecord& record)
{
    if (!record.exists || record.pid == 0) {
        return false;
    }
    ensureIdentity();
    if (!record.hostName.empty() && record.hostName != g_hostName) {
        return true;
    }
    return isProcessAlive_pid(record.pid);
}

static unsigned int lockStatusForDataPath(const std::string& dataPath,
                                          bool removeStale)
{
    if (dataPath.empty()) {
        return oaPlugIn::oacNotLocked;
    }

    const std::string lckPath = lockFilePath(dataPath);
    LockRecord record;
    if (!readLockRecord(lckPath, record)) {
        return oaPlugIn::oacNotLocked;
    }

    if (isOwnedByCurrentProcess(record)) {
        return oaPlugIn::oacLockedByCurrentProcess;
    }

    if (isLockOwnerAlive(record)) {
        return oaPlugIn::oacLockedByForeignProcess;
    }

    if (removeStale) {
        ::unlink(lckPath.c_str());
    }
    return oaPlugIn::oacNotLocked;
}

static const char* mapViewTypeToPrimaryFile(const std::string& viewType)
{
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
                                                const std::string& logicalName)
{
    const std::string directPath = joinPath(cellViewDir, logicalName);
    if (::access(directPath.c_str(), F_OK) == 0) {
        return logicalName;
    }

    std::ifstream master(joinPath(cellViewDir, masterTagFileName).c_str());
    if (master) {
        std::string header;
        std::string line;
        std::getline(master, header);
        std::getline(master, line);
        line = trim(line);
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
        if (dir != nullptr) {
            struct dirent* entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name == "." || name == ".." || name == "data.dm" ||
                    name == masterTagFileName || hasSuffix(name, editLockFileExt) ||
                    hasSuffix(name, criticalSaveExt) || hasSuffix(name, autoSaveExt) ||
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

static std::string buildFilePathFromParent(oaPlugIn::IDMObject* parent,
                                           const std::string& logicalName,
                                           const std::string& libRoot)
{
    if (logicalName.empty()) {
        return "";
    }
    if (!logicalName.empty() && logicalName[0] == '/') {
        return logicalName;
    }

    const std::string root = trimTrailingSlashes(libRoot);
    if (parent == nullptr) {
        return joinPath(root, logicalName);
    }

    oaPlugIn::ICellView* cv = nullptr;
    if (parent->queryInterface(oaPlugIn::IID_ICellView,
                               reinterpret_cast<void**>(&cv)) == oaCommon::IBase::cOK && cv) {
        const std::string cell = toStdString(cv->getCellName());
        const std::string view = toStdString(cv->getViewName());
        cv->release();

        const std::string cellViewDir = joinPath(joinPath(root, cell), view);
        const std::string actualName =
            readPrimaryFileFromMasterTag(cellViewDir, logicalName);
        return joinPath(cellViewDir, actualName);
    }

    oaPlugIn::ICell* cell = nullptr;
    if (parent->queryInterface(oaPlugIn::IID_ICell,
                               reinterpret_cast<void**>(&cell)) == oaCommon::IBase::cOK && cell) {
        const std::string cellName = toStdString(cell->getName());
        cell->release();
        return joinPath(joinPath(root, cellName), logicalName);
    }

    oaPlugIn::IView* view = nullptr;
    if (parent->queryInterface(oaPlugIn::IID_IView,
                               reinterpret_cast<void**>(&view)) == oaCommon::IBase::cOK && view) {
        const std::string viewName = toStdString(view->getName());
        view->release();
        return joinPath(joinPath(root, viewName), logicalName);
    }

    oaPlugIn::IDMLib* lib = nullptr;
    if (parent->queryInterface(oaPlugIn::IID_IDMLib,
                               reinterpret_cast<void**>(&lib)) == oaCommon::IBase::cOK && lib) {
        lib->release();
        return joinPath(root, logicalName);
    }

    oaPlugIn::IDMFile* parentFile = nullptr;
    if (parent->queryInterface(oaPlugIn::IID_IDMFile,
                               reinterpret_cast<void**>(&parentFile)) == oaCommon::IBase::cOK &&
        parentFile) {
        oaPlugIn::IDMObject* grandParent = nullptr;
        parentFile->getParent(grandParent);
        const std::string path =
            buildFilePathFromParent(grandParent, logicalName, libRoot);
        if (grandParent != nullptr) {
            grandParent->release();
        }
        parentFile->release();
        return path;
    }

    return joinPath(root, logicalName);
}

static std::string buildFilePathFromIDMFile(oaPlugIn::IDMFile* file,
                                            const std::string& libRoot)
{
    if (file == nullptr) {
        return "";
    }

    const std::string name = toStdString(file->getName());
    oaPlugIn::IDMObject* parent = nullptr;
    file->getParent(parent);
    const std::string path = buildFilePathFromParent(parent, name, libRoot);
    if (parent != nullptr) {
        parent->release();
    }
    return path;
}

static bool lockFilesInContainer(const Locking& locking,
                                 oaPlugIn::IDMContainer* container)
{
    if (container == nullptr) {
        return false;
    }

    bool ok = true;
    oaPlugIn::IDMFileIter* files = nullptr;
    container->getDMFiles(files);
    if (files == nullptr) {
        return true;
    }

    oaPlugIn::IDMFile* file = nullptr;
    while (files->next(file)) {
        if (file != nullptr) {
            ok = locking.lockObj(file) && ok;
            file->release();
        }
    }
    files->release();
    return ok;
}

static void unlockFilesInContainer(const Locking& locking,
                                   oaPlugIn::IDMContainer* container)
{
    if (container == nullptr) {
        return;
    }

    oaPlugIn::IDMFileIter* files = nullptr;
    container->getDMFiles(files);
    if (files == nullptr) {
        return;
    }

    oaPlugIn::IDMFile* file = nullptr;
    while (files->next(file)) {
        if (file != nullptr) {
            locking.unlockObj(file);
            file->release();
        }
    }
    files->release();
}

static bool hasLockedFileInContainer(const Locking& locking,
                                     oaPlugIn::IDMContainer* container)
{
    if (container == nullptr) {
        return false;
    }

    oaPlugIn::IDMFileIter* files = nullptr;
    container->getDMFiles(files);
    if (files == nullptr) {
        return false;
    }

    bool locked = false;
    oaPlugIn::IDMFile* file = nullptr;
    while (!locked && files->next(file)) {
        if (file != nullptr) {
            locked = locking.getLockStatusPvt(file) != oaPlugIn::oacNotLocked;
            file->release();
        }
    }
    files->release();
    return locked;
}

// ==========================================================================
// Locking — implements ILocking for the DMFileSys
// ==========================================================================

Locking::Locking()
    : plugInMsg_(nullptr)
{
}

Locking::~Locking()
{
}

// ---------------------------------------------------------------------------
// ILocking interface — primary lock/unlock on a single DMFile
// ---------------------------------------------------------------------------

bool Locking::lock(oaPlugIn::IDMFile* obj)
{
    if (!obj) return false;
    return lockObj(obj);
}

void Locking::unlock(oaPlugIn::IDMFile* obj)
{
    if (!obj) return;
    unlockObj(obj);
}

unsigned int Locking::getLockStatus(oaPlugIn::IDMFile* obj)
{
    if (!obj) return 0;
    return getLockStatusPvt(obj);
}

void Locking::setPlugInMessage(oaPlugIn::IPlugInMessage* msg)
{
    plugInMsg_ = msg;
}

// ---------------------------------------------------------------------------
// lockObj — acquire an edit lock on an OA object
// ---------------------------------------------------------------------------

bool Locking::lockObj(oaPlugIn::ICell* cell) const
{
    if (cell == nullptr) {
        return false;
    }

    bool ok = lockFilesInContainer(*this, cell);
    oaPlugIn::ICellViewIter* cellViews = nullptr;
    cell->getCellViews(cellViews);
    if (cellViews == nullptr) {
        return ok;
    }

    oaPlugIn::ICellView* cv = nullptr;
    while (cellViews->next(cv)) {
        if (cv != nullptr) {
            ok = lockObj(cv) && ok;
            cv->release();
        }
    }
    cellViews->release();
    return ok;
}

bool Locking::lockObj(oaPlugIn::IView* view) const
{
    if (view == nullptr) {
        return false;
    }

    bool ok = lockFilesInContainer(*this, view);
    oaPlugIn::ICellViewIter* cellViews = nullptr;
    view->getCellViews(cellViews);
    if (cellViews == nullptr) {
        return ok;
    }

    oaPlugIn::ICellView* cv = nullptr;
    while (cellViews->next(cv)) {
        if (cv != nullptr) {
            ok = lockObj(cv) && ok;
            cv->release();
        }
    }
    cellViews->release();
    return ok;
}

bool Locking::lockObj(oaPlugIn::IDMFile* file) const
{
    if (!file) return false;

    const std::string dataPath = buildFilePathFromIDMFile(file, lockDir_);
    return const_cast<Locking*>(this)->exclusiveLock(dataPath);
}

bool Locking::lockObj(oaPlugIn::ICellView* cv) const
{
    if (cv == nullptr) {
        return false;
    }
    return lockFilesInContainer(*this, cv);
}

bool Locking::lockObj(oaPlugIn::IDMObject* obj, bool recursive) const
{
    if (obj == nullptr) {
        return false;
    }

    oaPlugIn::IDMFile* file = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMFile,
                            reinterpret_cast<void**>(&file)) == oaCommon::IBase::cOK && file) {
        const bool ok = lockObj(file);
        file->release();
        return ok;
    }

    oaPlugIn::ICellView* cv = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICellView,
                            reinterpret_cast<void**>(&cv)) == oaCommon::IBase::cOK && cv) {
        const bool ok = lockObj(cv);
        cv->release();
        return ok;
    }

    oaPlugIn::ICell* cell = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICell,
                            reinterpret_cast<void**>(&cell)) == oaCommon::IBase::cOK && cell) {
        bool ok = lockFilesInContainer(*this, cell);
        if (recursive) {
            oaPlugIn::ICellViewIter* cellViews = nullptr;
            cell->getCellViews(cellViews);
            if (cellViews != nullptr) {
                oaPlugIn::ICellView* nestedCv = nullptr;
                while (cellViews->next(nestedCv)) {
                    if (nestedCv != nullptr) {
                        ok = lockObj(nestedCv) && ok;
                        nestedCv->release();
                    }
                }
                cellViews->release();
            }
        }
        cell->release();
        return ok;
    }

    oaPlugIn::IView* view = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IView,
                            reinterpret_cast<void**>(&view)) == oaCommon::IBase::cOK && view) {
        bool ok = lockFilesInContainer(*this, view);
        if (recursive) {
            oaPlugIn::ICellViewIter* cellViews = nullptr;
            view->getCellViews(cellViews);
            if (cellViews != nullptr) {
                oaPlugIn::ICellView* nestedCv = nullptr;
                while (cellViews->next(nestedCv)) {
                    if (nestedCv != nullptr) {
                        ok = lockObj(nestedCv) && ok;
                        nestedCv->release();
                    }
                }
                cellViews->release();
            }
        }
        view->release();
        return ok;
    }

    oaPlugIn::IDMContainer* container = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMContainer,
                            reinterpret_cast<void**>(&container)) == oaCommon::IBase::cOK &&
        container) {
        const bool ok = lockFilesInContainer(*this, container);
        container->release();
        return ok;
    }

    return false;
}

// ---------------------------------------------------------------------------
// unlockObj — release an edit lock
// ---------------------------------------------------------------------------

void Locking::unlockObj(oaPlugIn::ICell* cell) const
{
    if (cell == nullptr) {
        return;
    }

    unlockFilesInContainer(*this, cell);
    oaPlugIn::ICellViewIter* cellViews = nullptr;
    cell->getCellViews(cellViews);
    if (cellViews == nullptr) {
        return;
    }

    oaPlugIn::ICellView* cv = nullptr;
    while (cellViews->next(cv)) {
        if (cv != nullptr) {
            unlockObj(cv);
            cv->release();
        }
    }
    cellViews->release();
}

void Locking::unlockObj(oaPlugIn::IView* view) const
{
    if (view == nullptr) {
        return;
    }

    unlockFilesInContainer(*this, view);
    oaPlugIn::ICellViewIter* cellViews = nullptr;
    view->getCellViews(cellViews);
    if (cellViews == nullptr) {
        return;
    }

    oaPlugIn::ICellView* cv = nullptr;
    while (cellViews->next(cv)) {
        if (cv != nullptr) {
            unlockObj(cv);
            cv->release();
        }
    }
    cellViews->release();
}

void Locking::unlockObj(oaPlugIn::IDMFile* file) const
{
    if (!file) return;

    const std::string dataPath = buildFilePathFromIDMFile(file, lockDir_);
    const_cast<Locking*>(this)->exclusiveUnLock(dataPath);
}

void Locking::unlockObj(oaPlugIn::ICellView* cv) const
{
    unlockFilesInContainer(*this, cv);
}

void Locking::unlockObj(oaPlugIn::IDMObject* obj) const
{
    if (obj == nullptr) {
        return;
    }

    oaPlugIn::IDMFile* file = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMFile,
                            reinterpret_cast<void**>(&file)) == oaCommon::IBase::cOK && file) {
        unlockObj(file);
        file->release();
        return;
    }

    oaPlugIn::ICellView* cv = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICellView,
                            reinterpret_cast<void**>(&cv)) == oaCommon::IBase::cOK && cv) {
        unlockObj(cv);
        cv->release();
        return;
    }

    oaPlugIn::ICell* cell = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICell,
                            reinterpret_cast<void**>(&cell)) == oaCommon::IBase::cOK && cell) {
        unlockObj(cell);
        cell->release();
        return;
    }

    oaPlugIn::IView* view = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IView,
                            reinterpret_cast<void**>(&view)) == oaCommon::IBase::cOK && view) {
        unlockObj(view);
        view->release();
        return;
    }

    oaPlugIn::IDMContainer* container = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMContainer,
                            reinterpret_cast<void**>(&container)) == oaCommon::IBase::cOK &&
        container) {
        unlockFilesInContainer(*this, container);
        container->release();
    }
}

// ---------------------------------------------------------------------------
// hasLockedFiles — recursive check for lock files
// ---------------------------------------------------------------------------

bool Locking::hasLockedFiles(oaPlugIn::ICell* cell, bool recursive) const
{
    if (cell == nullptr) {
        return false;
    }
    if (hasLockedFileInContainer(*this, cell)) {
        return true;
    }
    if (!recursive) {
        return false;
    }

    oaPlugIn::ICellViewIter* cellViews = nullptr;
    cell->getCellViews(cellViews);
    if (cellViews == nullptr) {
        return false;
    }

    bool locked = false;
    oaPlugIn::ICellView* cv = nullptr;
    while (!locked && cellViews->next(cv)) {
        if (cv != nullptr) {
            locked = hasLockedFiles(cv, recursive);
            cv->release();
        }
    }
    cellViews->release();
    return locked;
}

bool Locking::hasLockedFiles(oaPlugIn::IView* view, bool recursive) const
{
    if (view == nullptr) {
        return false;
    }
    if (hasLockedFileInContainer(*this, view)) {
        return true;
    }
    if (!recursive) {
        return false;
    }

    oaPlugIn::ICellViewIter* cellViews = nullptr;
    view->getCellViews(cellViews);
    if (cellViews == nullptr) {
        return false;
    }

    bool locked = false;
    oaPlugIn::ICellView* cv = nullptr;
    while (!locked && cellViews->next(cv)) {
        if (cv != nullptr) {
            locked = hasLockedFiles(cv, recursive);
            cv->release();
        }
    }
    cellViews->release();
    return locked;
}

bool Locking::hasLockedFiles(oaPlugIn::ICellView* cv, bool recursive) const
{
    (void)recursive;
    return hasLockedFileInContainer(*this, cv);
}

bool Locking::hasLockedFiles(oaPlugIn::IDMObject* obj, bool recursive) const
{
    if (obj == nullptr) {
        return false;
    }

    oaPlugIn::IDMFile* file = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMFile,
                            reinterpret_cast<void**>(&file)) == oaCommon::IBase::cOK && file) {
        const bool locked = getLockStatusPvt(file) != oaPlugIn::oacNotLocked;
        file->release();
        return locked;
    }

    oaPlugIn::ICellView* cv = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICellView,
                            reinterpret_cast<void**>(&cv)) == oaCommon::IBase::cOK && cv) {
        const bool locked = hasLockedFiles(cv, recursive);
        cv->release();
        return locked;
    }

    oaPlugIn::ICell* cell = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_ICell,
                            reinterpret_cast<void**>(&cell)) == oaCommon::IBase::cOK && cell) {
        const bool locked = hasLockedFiles(cell, recursive);
        cell->release();
        return locked;
    }

    oaPlugIn::IView* view = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IView,
                            reinterpret_cast<void**>(&view)) == oaCommon::IBase::cOK && view) {
        const bool locked = hasLockedFiles(view, recursive);
        view->release();
        return locked;
    }

    oaPlugIn::IDMContainer* container = nullptr;
    if (obj->queryInterface(oaPlugIn::IID_IDMContainer,
                            reinterpret_cast<void**>(&container)) == oaCommon::IBase::cOK &&
        container) {
        const bool locked = hasLockedFileInContainer(*this, container);
        container->release();
        return locked;
    }

    return false;
}

// ---------------------------------------------------------------------------
// isEditLockFile — check if a filename represents an edit lock
// ---------------------------------------------------------------------------

bool Locking::isEditLockFile(const OpenAccess_4::oaString& name) const
{
    // The .cdslck extension is 7 characters
    // Check if name ends with ".cdslck"
    const char* cName = name;  // oaString has operator const char*
    if (!cName) return false;
    size_t len = std::strlen(cName);
    if (len < editLockFileExtLen) return false;
    return (std::strcmp(cName + len - editLockFileExtLen, editLockFileExt) == 0);
}

// ---------------------------------------------------------------------------
// getLockStatusPvt — internal lock status check
// Returns: 0 = no lock, 1 = locked by self, 2 = locked by other
// ---------------------------------------------------------------------------

unsigned int Locking::getLockStatusPvt(oaPlugIn::IDMFile* file) const
{
    if (!file) return 0;

    const std::string dataPath = buildFilePathFromIDMFile(file, lockDir_);
    return lockStatusForDataPath(dataPath, true);
}

// ---------------------------------------------------------------------------
// hasExclusiveLock — check if path has an exclusive lock owned by THIS process
// Returns true if the lock is owned by the current process (exclusive hold);
// false if no lock or owned by another process.
// ---------------------------------------------------------------------------

bool Locking::hasExclusiveLock(const std::string& path)
{
    return lockStatusForDataPath(path, true) == oaPlugIn::oacLockedByCurrentProcess;
}

// ---------------------------------------------------------------------------
// getLockFilePath — helpers to compute .cdslck path
// ---------------------------------------------------------------------------

std::string Locking::getLockFilePath(oaPlugIn::IDMFile* file) const
{
    const std::string dataPath = buildFilePathFromIDMFile(file, lockDir_);
    return dataPath.empty() ? "" : lockFilePath(dataPath);
}

std::string Locking::getLockFilePath(const std::string& dataPath) const
{
    return lockFilePath(dataPath);
}

void Locking::setLibPath(const std::string& path)
{
    lockDir_ = trimTrailingSlashes(path);
}


// ==========================================================================
// FileLocking — Singleton for process-level file locking I/O
// ==========================================================================

// Static member definitions
std::string FileLocking::s_userName;
std::string FileLocking::loginName;
bool FileLocking::daemonStarted = false;

// ---------------------------------------------------------------------------
// Singleton access
// ---------------------------------------------------------------------------

FileLocking& FileLocking::get()
{
    static FileLocking instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

FileLocking::FileLocking()
    : plugInMsg_(nullptr)
{
    ensureIdentity();
    s_userName = g_hostName;
    loginName = g_loginName;

    // Register atexit handler for cleanup
    if (!daemonStarted) {
        std::atexit([]() {
            FileLocking::get().cleanUpOnExit();
        });
    }
}

FileLocking::~FileLocking()
{
}

// ---------------------------------------------------------------------------
// start — initialise lock-file daemon (Unix: register signal handlers)
// ---------------------------------------------------------------------------

void FileLocking::start()
{
    if (daemonStarted) return;
    daemonStarted = true;

    ensureIdentity();

    // In production OA the daemon uses a local socket for client-server
    // lock communication across processes on the same host.
    // For the reimplementation we use simple atexit-based cleanup.
    // The actual OA daemon would:
    //   1. Create a Unix domain socket in /tmp
    //   2. Fork a child process
    //   3. Listen for lock/unlock requests from other OA processes
}

// ---------------------------------------------------------------------------
// terminate — remove lock file for the given path
// ---------------------------------------------------------------------------

void FileLocking::terminate(const char* path)
{
    if (!path || !*path) return;

    std::string lckPath = lockFilePath(path);
    LockRecord record;
    if (readLockRecord(lckPath, record) && isOwnedByCurrentProcess(record)) {
        unlink(lckPath.c_str());
    }
    g_ownedDataPaths.erase(path);
}

// ---------------------------------------------------------------------------
// isActive — check if a lock exists and the owning process is alive
// ---------------------------------------------------------------------------

bool FileLocking::isActive(const char* path)
{
    if (!path || !*path) return false;

    LockRecord record;
    return readLockRecord(lockFilePath(path), record) && isLockOwnerAlive(record);
}

// ---------------------------------------------------------------------------
// load — parse a .cdslck file and populate ProcInfo
// ---------------------------------------------------------------------------

void FileLocking::load(FILE* fp, oaCommon::ProcInfo& procInfo)
{
    if (!fp) return;
    char lineBuf[1024];
    rewind(fp);
    std::string hostName, loginNameStr;
    unsigned long pid = 0;
    time_t procTime = 0;

    while (fgets(lineBuf, sizeof(lineBuf), fp)) {
        std::string line(lineBuf);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' '))
            line.pop_back();

        if (lockLineMatchesKey(line, "HostName")) {
            hostName = lockLineValue(line, "HostName");
        }
        else if (lockLineMatchesKey(line, "LoginName")) {
            loginNameStr = lockLineValue(line, "LoginName");
        }
        else if (lockLineMatchesKey(line, "ProcessIdentifier")) {
            pid = std::strtoul(lockLineValue(line, "ProcessIdentifier").c_str(), nullptr, 10);
        }
        else if (lockLineMatchesKey(line, "ProcessCreationTime_UTC")) {
            procTime = static_cast<time_t>(
                std::strtoll(lockLineValue(line, "ProcessCreationTime_UTC").c_str(), nullptr, 10));
        }
    }

    procInfo.set(hostName, pid);
    if (procTime > 0) procInfo.setTime(procTime);
}

// ---------------------------------------------------------------------------
// save — write a .cdslck file
// ---------------------------------------------------------------------------

bool FileLocking::save(const char* path) const
{
    if (!path || !*path) return false;
    ensureIdentity();
    s_userName = g_hostName;
    loginName = g_loginName;

    unsigned int status = lockStatusForDataPath(path, true);
    if (status == oaPlugIn::oacLockedByCurrentProcess) {
        g_ownedDataPaths.insert(path);
        return true;
    }
    if (status == oaPlugIn::oacLockedByForeignProcess) {
        return false;
    }

    std::string lckPath = lockFilePath(path);

    int fd = open(lckPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) {
        if (errno == EEXIST) {
            status = lockStatusForDataPath(path, true);
            if (status == oaPlugIn::oacLockedByCurrentProcess) {
                g_ownedDataPaths.insert(path);
                return true;
            }
            if (status == oaPlugIn::oacNotLocked) {
                fd = open(lckPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
            }
        }
        if (fd < 0) return false;
    }

    time_t now = ::time(nullptr);
    FILE* fp = fdopen(fd, "w");
    if (!fp) { close(fd); return false; }

    fprintf(fp, "AppIdentifier=OpenAccess edit lock\n");
    fprintf(fp, "HostName=%s\n", g_hostName.c_str());
    fprintf(fp, "LoginName=%s\n", g_loginName.c_str());
    fprintf(fp, "ProcessIdentifier=%lu\n", static_cast<unsigned long>(g_pid));
    fprintf(fp, "ProcessCreationTime_UTC=%lld\n", static_cast<long long>(g_procStart));
    fprintf(fp, "TimeEditLocked=%s\n", formatTime(now).c_str());
    fprintf(fp, "FilePathUsedToEditLock=%s\n", path);
    fprintf(fp, "LockStakeVersion=%s\n", cVersion);
    fprintf(fp, "OSType=%s\n", cPlatform);
    fprintf(fp, "ReasonForPlacingEditLock=%s\n", "Edit in progress");
    fclose(fp);
    g_ownedDataPaths.insert(path);
    return true;
}

// ---------------------------------------------------------------------------
// getVersion — read the LockStakeVersion from a .cdslck file
// ---------------------------------------------------------------------------

bool FileLocking::getVersion(const char* path)
{
    if (!path || !*path) return false;

    std::string lckPath = lockFilePath(path);
    std::ifstream lckFile(lckPath);
    if (!lckFile.is_open()) return false;

    std::string line;
    while (std::getline(lckFile, line)) {
        if (line.find("LockStakeVersion") != std::string::npos) {
            // We just need to know it's there and parseable
            // Returns true if version line exists
            lckFile.close();
            return true;
        }
    }
    lckFile.close();
    return false;
}

// ---------------------------------------------------------------------------
// cleanUpOnExit — remove all lock files owned by this process
// ---------------------------------------------------------------------------

void FileLocking::cleanUpOnExit()
{
    std::vector<std::string> paths(g_ownedDataPaths.begin(), g_ownedDataPaths.end());
    for (const std::string& path : paths) {
        terminate(path.c_str());
    }
}

// ---------------------------------------------------------------------------
// getLoginName — return the cached login name
// ---------------------------------------------------------------------------

std::string FileLocking::getLoginName()
{
    ensureIdentity();
    return g_loginName;
}

// ---------------------------------------------------------------------------
// isEditLockFile — check if the given filename represents an edit lock
// ---------------------------------------------------------------------------

bool FileLocking::isEditLockFile(const OpenAccess_4::oaString& path)
{
    const char* cPath = path;
    if (!cPath) return false;
    size_t len = std::strlen(cPath);
    if (len < editLockFileExtLen) return false;
    return (std::strcmp(cPath + len - editLockFileExtLen, editLockFileExt) == 0);
}

// ---------------------------------------------------------------------------
// isProcessAlive — check if a process with the given PID is alive
// ---------------------------------------------------------------------------

bool FileLocking::isProcessAlive(unsigned int pid, const char* procName)
{
    if (pid == 0) return false;

    // kill(pid, 0) does not send a signal; it just checks permissions
    // and process existence.
    if (kill(static_cast<pid_t>(pid), 0) != 0) {
        return false;  // ESRCH: no such process
    }

    // Optionally verify the process name matches (for extra safety)
    if (procName && *procName) {
        std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
        std::ifstream commFile(commPath);
        if (commFile.is_open()) {
            std::string actualName;
            std::getline(commFile, actualName);
            // comm is limited to 15 chars; do a prefix match
            if (actualName.find(procName) != 0) {
                return false;  // Process name mismatch
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// isProcessAliveWithTimeout — check process liveness with timeout window
//
// Parameters:
//   pid         - process ID to check
//   timeout     - [in/out] remaining timeout in seconds
//   procName    - expected process name (optional, nullptr to skip)
//   maxTimeout  - maximum timeout to allow
//
// Returns true if process is alive, or if within timeout window.
// The timeout allows for NFS/network delays where a process may appear
// dead briefly before recovering.
// ---------------------------------------------------------------------------

bool FileLocking::isProcessAliveWithTimeout(
    unsigned int pid,
    unsigned int& timeout,
    const char* procName,
    unsigned int maxTimeout)
{
    if (pid == 0) return false;

    // Check if process is currently alive
    if (isProcessAlive(pid, procName)) {
        timeout = maxTimeout;  // Reset timeout on successful check
        return true;
    }

    // Process appears dead—has the timeout expired?
    if (timeout == 0) {
        timeout = maxTimeout;
        return false;
    }

    // Within timeout window: decrement and treat as alive
    // (gives slow NFS mounts time to recover)
    --timeout;
    return true;
}

// ---------------------------------------------------------------------------
// queryInterface — COM-style interface query
// ---------------------------------------------------------------------------

long FileLocking::queryInterface(const oaCommon::Guid& id, void** iPtr)
{
    if (!iPtr) return -1;

    // In the real OA, this checks against known interface GUIDs
    // For the reimplementation, we support a limited set
    *iPtr = nullptr;

    // ILocking support
    // if (id == oaPlugIn::IID_ILocking) {
    //     *iPtr = static_cast<void*>(this);
    //     return 0;
    // }

    return -1;  // Interface not supported
}

// ---------------------------------------------------------------------------
// setPlugInMessage / getPlugInMessage
// ---------------------------------------------------------------------------

void FileLocking::setPlugInMessage(oaPlugIn::IPlugInMessage* msg)
{
    plugInMsg_ = msg;
}

oaPlugIn::IPlugInMessage* FileLocking::getPlugInMessage()
{
    return plugInMsg_;
}

// ---------------------------------------------------------------------------
// getRhelLinkFileName — get RHEL link file name for the lock
// (RHEL: Red Hat Enterprise Linux; handles .cdslck.rhel symlinks)
// ---------------------------------------------------------------------------

std::string FileLocking::getRhelLinkFileName(
    const char* path,
    const oaCommon::ProcInfo& procInfo,
    std::string& out)
{
    if (!path || !*path) return "";

    // In RHEL-specific locking, a .cdslck.rhel symlink is created
    // pointing to the actual .cdslck file, providing an extra level
    // of indirection for NFS-mounted home directories.

    std::string lckPath = lockFilePath(path);
    out = lckPath + ".rhel";   // editLockFileRhelLinkExt
    return out;
}

// ---------------------------------------------------------------------------
// removeRhelLink — remove the .cdslck.rhel symlink
// ---------------------------------------------------------------------------

void FileLocking::removeRhelLink(
    const std::string& path,
    const oaCommon::ProcInfo& procInfo)
{
    std::string rhelPath = path + editLockFileRhelLinkExt;  // ".cdslck.rhel"

    // Check if we own this link before removing
    // Read the target to find the actual .cdslck, then verify ownership
    char linkTarget[4096];
    ssize_t len = readlink(rhelPath.c_str(), linkTarget, sizeof(linkTarget) - 1);
    if (len > 0) {
        linkTarget[len] = '\0';

        // Read the .cdslck to verify ownership
        std::ifstream lckFile(linkTarget);
        if (lckFile.is_open()) {
            std::string line;
            unsigned int lockPid = 0;
            while (std::getline(lckFile, line)) {
                if (line.find("ProcessIdentifier") != std::string::npos) {
                    size_t pos = line.find_last_of(" \t");
                    if (pos != std::string::npos) {
                        lockPid = static_cast<unsigned int>(
                            std::strtoul(line.c_str() + pos, nullptr, 10));
                        break;
                    }
                }
            }
            lckFile.close();

            if (lockPid == getpid()) {
                unlink(rhelPath.c_str());
            }
        }
    }
}

// ============================================================================
// FileLocking — additional path-based methods (matching original .so exports)
// ============================================================================

bool FileLocking::lock(const std::string& path)
{
    return save(path.c_str());
}

void FileLocking::unlock(const std::string& path)
{
    terminate(path.c_str());
}

unsigned int FileLocking::getLockStatus(const std::string& path)
{
    return lockStatusForDataPath(path, true);
}

bool FileLocking::alive(const oaCommon::ProcInfo& procInfo)
{
    return isProcessAlive(static_cast<unsigned int>(procInfo.getId()), nullptr);
}

std::string FileLocking::getLockPath(const std::string& path)
{
    return lockFilePath(path);
}

std::string FileLocking::getLockingProc(const std::string& path)
{
    std::string lckPath = lockFilePath(path);
    FILE* fp = fopen(lckPath.c_str(), "r");
    if (!fp) return "";
    oaCommon::ProcInfo pi;
    load(fp, pi);
    fclose(fp);
    return std::to_string(pi.getId());
}

bool FileLocking::recover(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    const unsigned int status = lockStatusForDataPath(path, true);
    if (status == oaPlugIn::oacLockedByForeignProcess) {
        return false;
    }
    return ::unlink(lockFilePath(path).c_str()) == 0 || errno == ENOENT;
}

bool FileLocking::openRhel(const char* path)
{
    (void)path;
    return true;  // RHEL link not used in simplified implementation
}

void FileLocking::renameRhelLink(const std::string& oldPath, const std::string& newPath)
{
    std::string oldLink = oldPath + ".cdslck.rhel";
    std::string newLink = newPath + ".cdslck.rhel";
    rename(oldLink.c_str(), newLink.c_str());
}

// ============================================================================
// Locking — additional methods
// ============================================================================

bool Locking::exclusiveLock(const std::string& path)
{
    if (path.empty()) {
        return false;
    }

    const unsigned int status = lockStatusForDataPath(path, true);
    if (status == oaPlugIn::oacLockedByCurrentProcess) {
        return true;
    }
    if (status == oaPlugIn::oacLockedByForeignProcess) {
        return false;
    }
    return FileLocking::get().save(path.c_str());
}

void Locking::exclusiveUnLock(const std::string& path)
{
    if (!path.empty()) {
        FileLocking::get().terminate(path.c_str());
    }
}

void Locking::renameLock(oaPlugIn::IDMFile* file, const std::string& newPath)
{
    const std::string oldPath = buildFilePathFromIDMFile(file, lockDir_);
    if (oldPath.empty() || newPath.empty()) {
        return;
    }
    if (lockStatusForDataPath(oldPath, true) != oaPlugIn::oacLockedByCurrentProcess) {
        return;
    }

    const std::string oldLock = lockFilePath(oldPath);
    const std::string targetPath =
        (!newPath.empty() && newPath[0] == '/') ? newPath : joinPath(lockDir_, newPath);
    const std::string newLock = lockFilePath(targetPath);
    if (::rename(oldLock.c_str(), newLock.c_str()) == 0) {
        g_ownedDataPaths.erase(oldPath);
        g_ownedDataPaths.insert(targetPath);
    }
}

} // namespace oaDMFileSys
