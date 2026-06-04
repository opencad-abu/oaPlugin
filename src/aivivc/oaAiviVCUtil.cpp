#include "oaAiviVC.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace oaAiviVC {

extern const char *const kClassId = "oaAiviVC";
extern const char *const kAliasClassId = "aivivc";
extern const char *const kSystemClassId = "oaAiviVCSystem";
extern const char *const kRootAttr = "AiviVCRoot";
extern const char *const kBackendAttr = "AiviVCBackend";
extern const char *const kGitCommandAttr = "AiviVCGitCommand";
extern const char *const kSvnCommandAttr = "AiviVCSvnCommand";
extern const char *const kLibPathAttr = "libPath";
extern const char *const kWritePathAttr = "writePath";

bool guidEqual(const oaCommon::Guid &a, const oaCommon::Guid &b)
{
    return std::memcmp(&a, &b, sizeof(oaCommon::Guid)) == 0;
}

std::string toStdString(const oaCommon::SRef<oaCommon::IString> &value)
{
    return value ? std::string(value->str() ? value->str() : "") : std::string();
}

std::string lower(std::string value)
{
    for (std::string::size_type i = 0; i < value.size(); ++i) {
        if (value[i] >= 'A' && value[i] <= 'Z') {
            value[i] = static_cast<char>(value[i] - 'A' + 'a');
        }
    }
    return value;
}

std::string trim(const std::string &value)
{
    const char *ws = " \t\r\n";
    std::string::size_type begin = value.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return "";
    }
    std::string::size_type end = value.find_last_not_of(ws);
    return value.substr(begin, end - begin + 1);
}

bool pathExists(const std::string &path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0;
}

bool pathIsDir(const std::string &path)
{
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool pathWritable(const std::string &path)
{
    return !path.empty() && ::access(path.c_str(), W_OK) == 0;
}

std::string dirName(const std::string &path)
{
    if (path.empty()) {
        return ".";
    }
    std::string::size_type slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

bool mkdirs(const std::string &path)
{
    if (path.empty() || path == ".") {
        return true;
    }
    if (pathIsDir(path)) {
        return true;
    }
    std::string parent = dirName(path);
    if (parent != path && !mkdirs(parent)) {
        return false;
    }
    if (::mkdir(path.c_str(), 0777) == 0) {
        return true;
    }
    return errno == EEXIST && pathIsDir(path);
}

std::string checkoutKey(const std::string &rel)
{
    static const char *digits = "0123456789abcdef";
    std::string text = rel.empty() || rel == "." ? "__root__" : rel;
    std::string out;
    out.reserve(text.size() * 2);
    for (std::string::const_iterator it = text.begin(); it != text.end(); ++it) {
        unsigned char ch = static_cast<unsigned char>(*it);
        out.push_back(digits[(ch >> 4) & 0xf]);
        out.push_back(digits[ch & 0xf]);
    }
    return out;
}

std::string currentIdentity()
{
    const char *user = std::getenv("USER");
    if (!user || !*user) {
        user = std::getenv("LOGNAME");
    }
    if (!user || !*user) {
        user = "unknown";
    }

    char host[256];
    if (::gethostname(host, sizeof(host)) != 0) {
        std::strncpy(host, "unknown", sizeof(host));
    }
    host[sizeof(host) - 1] = '\0';
    return std::string(user) + "@" + host;
}

std::string checkoutMarkerOwner(const std::string &path)
{
    std::ifstream input(path.c_str());
    if (!input) {
        return "";
    }
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.find("owner=") == 0) {
            return trim(line.substr(6));
        }
        if (!line.empty()) {
            return line;
        }
    }
    return "";
}

std::string joinPath(const std::string &a, const std::string &b)
{
    if (a.empty() || a == ".") {
        return b.empty() ? "." : b;
    }
    if (b.empty() || b == ".") {
        return a;
    }
    if (b[0] == '/') {
        return b;
    }
    if (a[a.size() - 1] == '/') {
        return a + b;
    }
    return a + "/" + b;
}

static std::string shellQuote(const std::string &arg)
{
    if (arg.empty()) {
        return "''";
    }
    std::string out("'");
    for (std::string::const_iterator it = arg.begin(); it != arg.end(); ++it) {
        if (*it == '\'') {
            out += "'\\''";
        } else {
            out += *it;
        }
    }
    out += "'";
    return out;
}

CommandResult runCommand(const std::vector<std::string> &args)
{
    std::ostringstream cmd;
    for (std::vector<std::string>::const_iterator it = args.begin(); it != args.end(); ++it) {
        if (it != args.begin()) {
            cmd << ' ';
        }
        cmd << shellQuote(*it);
    }
    cmd << " 2>&1";

    CommandResult result;
    result.status = 127;

    FILE *pipe = ::popen(cmd.str().c_str(), "r");
    if (!pipe) {
        result.output = std::string("popen failed: ") + std::strerror(errno);
        return result;
    }

    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        result.output += buffer;
    }

    int rc = ::pclose(pipe);
    if (WIFEXITED(rc)) {
        result.status = WEXITSTATUS(rc);
    } else {
        result.status = rc;
    }
    return result;
}

std::vector<std::string> splitLines(const std::string &text)
{
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.resize(line.size() - 1);
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

AiviVCVersion::AiviVCVersion(const char *displayName)
    : refCount_(0), displayName_(displayName && *displayName ? displayName : "HEAD")
{
}

unsigned long AiviVCVersion::addRef()
{
    return ++refCount_;
}

unsigned long AiviVCVersion::release()
{
    unsigned long next = --refCount_;
    if (!next) {
        delete this;
    }
    return next;
}

unsigned long AiviVCVersion::getRefCount()
{
    return refCount_;
}

long AiviVCVersion::queryInterface(const oaCommon::Guid &id, void **iPtr)
{
    if (!iPtr) {
        return oaCommon::IBase::cFail;
    }
    *iPtr = NULL;
    if (guidEqual(id, oaPlugIn::IID_IVersion) || guidEqual(id, oaCommon::IID_IBase)) {
        *iPtr = static_cast<oaPlugIn::IVersion *>(this);
        addRef();
        return oaCommon::IBase::cOK;
    }
    return oaCommon::IBase::cNoInterface;
}

oaCommon::SRef<oaCommon::IString> AiviVCVersion::getVCSystemName()
{
    return oaCommon::SRef<oaCommon::IString>(new oaCommon::StringImp(kClassId));
}

oaCommon::SRef<oaCommon::IString> AiviVCVersion::getDisplayName()
{
    return oaCommon::SRef<oaCommon::IString>(new oaCommon::StringImp(displayName_.c_str()));
}

oa::oaUInt4 AiviVCVersion::compare(oaPlugIn::IVersion *other)
{
    if (!other) {
        return oaPlugIn::oacVersionCompIncompatable;
    }
    std::string rhs = toStdString(other->getDisplayName());
    return rhs == displayName_ ? oaPlugIn::oacVersionCompEqual
                               : oaPlugIn::oacVersionCompDifferentBranch;
}

} // namespace oaAiviVC
