#include "oaAiviVC.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

namespace oaAiviVC {

namespace {

struct CheckoutMarkerInfo {
    std::string rel;
    std::string owner;
};

std::string normalizeRel(const std::string &rel)
{
    if (rel.empty() || rel == ".") {
        return ".";
    }
    std::string out = rel;
    while (!out.empty() && out[0] == '/') {
        out.erase(0, 1);
    }
    while (!out.empty() && out[out.size() - 1] == '/') {
        out.erase(out.size() - 1);
    }
    return out.empty() ? "." : out;
}

bool relIsUnder(const std::string &rel, const std::string &ancestor)
{
    std::string normRel = normalizeRel(rel);
    std::string normAncestor = normalizeRel(ancestor);
    return normAncestor == "." ||
           normRel == normAncestor ||
           normRel.find(normAncestor + "/") == 0;
}

std::string parentRel(const std::string &rel)
{
    std::string normRel = normalizeRel(rel);
    if (normRel == ".") {
        return ".";
    }
    std::string::size_type slash = normRel.find_last_of('/');
    return slash == std::string::npos ? "." : normRel.substr(0, slash);
}

std::string markerOwnerFromText(const std::string &text)
{
    std::vector<std::string> lines = splitLines(text);
    for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->find("owner=") == 0) {
            return trim(it->substr(6));
        }
        if (!trim(*it).empty()) {
            return trim(*it);
        }
    }
    return "";
}

std::string decodeCheckoutKey(const std::string &key)
{
    if (key.empty()) {
        return "";
    }
    std::string decoded;
    decoded.reserve(key.size() / 2);
    for (std::string::size_type i = 0; i + 1 < key.size(); i += 2) {
        char hi = key[i];
        char lo = key[i + 1];
        int hiVal = (hi >= '0' && hi <= '9') ? hi - '0' :
                    (hi >= 'a' && hi <= 'f') ? hi - 'a' + 10 :
                    (hi >= 'A' && hi <= 'F') ? hi - 'A' + 10 : -1;
        int loVal = (lo >= '0' && lo <= '9') ? lo - '0' :
                    (lo >= 'a' && lo <= 'f') ? lo - 'a' + 10 :
                    (lo >= 'A' && lo <= 'F') ? lo - 'A' + 10 : -1;
        if (hiVal < 0 || loVal < 0) {
            return "";
        }
        decoded.push_back(static_cast<char>((hiVal << 4) | loVal));
    }
    return decoded;
}

std::string markerRelFromText(const std::string &text, const std::string &fallbackKey)
{
    std::vector<std::string> lines = splitLines(text);
    for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->find("rel=") == 0) {
            return normalizeRel(trim(it->substr(4)));
        }
    }
    return normalizeRel(decodeCheckoutKey(fallbackKey));
}

std::string pathToRel(const std::string &root, const std::string &path)
{
    std::string normRoot = root;
    while (!normRoot.empty() && normRoot[normRoot.size() - 1] == '/') {
        normRoot.erase(normRoot.size() - 1);
    }
    if (path == normRoot) {
        return ".";
    }
    if (path.size() > normRoot.size() &&
        path.compare(0, normRoot.size(), normRoot) == 0 &&
        path[normRoot.size()] == '/') {
        return normalizeRel(path.substr(normRoot.size() + 1));
    }
    return normalizeRel(path);
}

bool relCheckedOutByMe(const std::string &rel, const std::vector<std::string> &checkedOutRels)
{
    for (std::vector<std::string>::const_iterator it = checkedOutRels.begin();
         it != checkedOutRels.end(); ++it) {
        if (relIsUnder(rel, *it)) {
            return true;
        }
    }
    return false;
}

std::vector<CheckoutMarkerInfo> loadCheckoutMarkers(const std::string &markerDir)
{
    std::vector<CheckoutMarkerInfo> markers;
    DIR *dir = ::opendir(markerDir.c_str());
    if (!dir) {
        return markers;
    }

    struct dirent *entry = NULL;
    while ((entry = ::readdir(dir)) != NULL) {
        std::string name = entry->d_name ? entry->d_name : "";
        if (name.empty() || name == "." || name == "..") {
            continue;
        }

        std::string path = joinPath(markerDir, name);
        std::ifstream input(path.c_str());
        if (!input) {
            continue;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        CheckoutMarkerInfo marker;
        marker.owner = markerOwnerFromText(buffer.str());
        marker.rel = markerRelFromText(buffer.str(), name);
        if (!marker.rel.empty()) {
            markers.push_back(marker);
        }
    }

    ::closedir(dir);
    return markers;
}

} // namespace

void AiviVC::parseAttrs(oaPlugIn::IAttrIter *attrs)
{
    if (!attrs) {
        return;
    }
    attrs->reset();
    oaPlugIn::IAttr *attr = NULL;
    while (attrs->next(attr)) {
        std::string name = lower(toStdString(attr->getName()));
        std::string value = toStdString(attr->getValue());
        if (name == lower(kRootAttr) || name == lower(kLibPathAttr)) {
            root_ = value;
        } else if (name == lower(kWritePathAttr)) {
            writePath_ = value;
        } else if (name == lower(kBackendAttr) || name == "backend" || name == "aivivcbackend") {
            backend_ = lower(value);
        } else if (name == lower(kGitCommandAttr) || name == "gitcommand" ||
                   name == "aivivcgit" || name == "aivivcgitcommand") {
            gitCommand_ = value;
        } else if (name == lower(kSvnCommandAttr) || name == "svncommand" ||
                   name == "subversioncommand" || name == "aivivcsvn" ||
                   name == "aivivcsvncommand") {
            svnCommand_ = value;
        }
        attr->release();
    }
    attrs->reset();
    if (root_.empty() && !writePath_.empty()) {
        root_ = writePath_;
    }
}

Backend AiviVC::detectBackend() const
{
    std::string configured = lower(backend_);
    if (configured == "svn" || configured == "subversion") {
        return kBackendSvn;
    }
    if (configured == "git") {
        return kBackendGit;
    }
    if (pathIsDir(joinPath(root_, ".svn"))) {
        return kBackendSvn;
    }
    CommandResult git = runCommand(std::vector<std::string>{gitCommand(), "-C", root_,
                                                            "rev-parse", "--is-inside-work-tree"});
    if (git.ok() && git.output.find("true") != std::string::npos) {
        return kBackendGit;
    }
    CommandResult svn = runCommand(std::vector<std::string>{svnCommand(), "info", root_});
    if (svn.ok()) {
        return kBackendSvn;
    }
    return kBackendGit;
}

bool AiviVC::ensureGitRepo()
{
    CommandResult probe = runCommand(std::vector<std::string>{gitCommand(), "-C", root_,
                                                              "rev-parse", "--is-inside-work-tree"});
    if (probe.ok() && probe.output.find("true") != std::string::npos) {
        return true;
    }
    if (!pathExists(root_)) {
        ::mkdir(root_.c_str(), 0777);
    }
    CommandResult init = runCommand(std::vector<std::string>{gitCommand(), "init", root_});
    return init.ok();
}

std::string AiviVC::fullPath(const std::string &rel) const
{
    return joinPath(root_, rel.empty() ? "." : rel);
}

std::string AiviVC::objectRelPath(oaPlugIn::IDMObject *object) const
{
    if (!object) {
        return ".";
    }

    if (object->isLib()) {
        return ".";
    }

    if (object->isCellView()) {
        oaPlugIn::ICellView *cv = static_cast<oaPlugIn::ICellView *>(object);
        std::string cell = toStdString(cv->getCellName());
        std::string view = toStdString(cv->getViewName());
        return joinPath(cell, view);
    }

    if (object->isCell()) {
        oaPlugIn::ICell *cell = static_cast<oaPlugIn::ICell *>(object);
        std::string name = toStdString(cell->getName());
        return name.empty() ? "." : name;
    }

    if (object->isView()) {
        oaPlugIn::IView *view = static_cast<oaPlugIn::IView *>(object);
        std::string name = toStdString(view->getName());
        return name.empty() ? "." : name;
    }

    if (object->isDMFile()) {
        oaPlugIn::IDMFile *file = static_cast<oaPlugIn::IDMFile *>(object);
        std::string name = toStdString(file->getName());
        oaPlugIn::IDMObject *parent = NULL;
        file->getParent(parent);
        std::string parentRel = parent ? objectRelPath(parent) : ".";
        if (parent) {
            parent->release();
        }

        std::string mappedName = mapPrimaryFileName(parentRel, name);
        return joinPath(parentRel, mappedName.empty() ? name : mappedName);
    }

    return ".";
}

std::string AiviVC::mapPrimaryFileName(const std::string &parentRel,
                                       const std::string &logicalName) const
{
    std::string dir = fullPath(parentRel);
    std::string direct = joinPath(dir, logicalName);
    if (pathExists(direct)) {
        return logicalName;
    }

    std::ifstream master(joinPath(dir, "master.tag").c_str());
    if (master) {
        std::string header;
        std::string line;
        std::getline(master, header);
        std::getline(master, line);
        line = trim(line);
        if (!line.empty()) {
            if (line.find("ViewType=") == 0) {
                std::string vt = line.substr(9);
                if (vt == "schematicSymbol") {
                    return "symbol.oa";
                }
                if (vt == "schematic") {
                    return "sch.oa";
                }
                if (vt == "maskLayout") {
                    return "layout.oa";
                }
            } else {
                return line;
            }
        }
    }
    return logicalName;
}

std::string AiviVC::checkoutMarkerPath(const std::string &rel) const
{
    std::string key = checkoutKey(rel);
    if (detectBackend() == kBackendGit) {
        CommandResult result = runCommand(std::vector<std::string>{
            gitCommand(), "-C", root_, "rev-parse", "--git-path",
            joinPath("aivivc/checkout", key)});
        std::string path = trim(result.output);
        if (result.ok() && !path.empty()) {
            return path[0] == '/' ? path : joinPath(root_, path);
        }
        return joinPath(joinPath(root_, ".git/aivivc/checkout"), key);
    }
    return joinPath(joinPath(root_, ".aivivc/checkout"), key);
}

std::string AiviVC::checkoutMarkerDirPath() const
{
    if (detectBackend() == kBackendGit) {
        CommandResult result = runCommand(std::vector<std::string>{
            gitCommand(), "-C", root_, "rev-parse", "--git-path", "aivivc/checkout"});
        std::string path = trim(result.output);
        if (result.ok() && !path.empty()) {
            return path[0] == '/' ? path : joinPath(root_, path);
        }
        return joinPath(root_, ".git/aivivc/checkout");
    }
    return joinPath(root_, ".aivivc/checkout");
}

std::string AiviVC::effectiveCheckoutOwner(const std::string &rel) const
{
    std::vector<CheckoutMarkerInfo> markers = loadCheckoutMarkers(checkoutMarkerDirPath());
    std::string current = normalizeRel(rel);
    while (true) {
        for (std::vector<CheckoutMarkerInfo>::const_iterator it = markers.begin();
             it != markers.end(); ++it) {
            if (normalizeRel(it->rel) == current && !it->owner.empty()) {
                return it->owner;
            }
        }
        if (current == ".") {
            break;
        }
        current = parentRel(current);
    }
    return "";
}

bool AiviVC::checkoutMarkerOverlapsOther(const std::string &rel) const
{
    std::vector<CheckoutMarkerInfo> markers = loadCheckoutMarkers(checkoutMarkerDirPath());
    std::string target = normalizeRel(rel);
    std::string me = currentIdentity();
    for (std::vector<CheckoutMarkerInfo>::const_iterator it = markers.begin();
         it != markers.end(); ++it) {
        if (!it->owner.empty() && it->owner != me &&
            (relIsUnder(target, it->rel) || relIsUnder(it->rel, target))) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> AiviVC::checkedOutRelsByCurrentUser() const
{
    std::vector<std::string> rels;
    std::vector<CheckoutMarkerInfo> markers = loadCheckoutMarkers(checkoutMarkerDirPath());
    std::string me = currentIdentity();
    for (std::vector<CheckoutMarkerInfo>::const_iterator it = markers.begin();
         it != markers.end(); ++it) {
        if (it->owner == me) {
            rels.push_back(normalizeRel(it->rel));
        }
    }
    return rels;
}

bool AiviVC::checkoutMarkerOwnedByCurrentUser(const std::string &rel) const
{
    return effectiveCheckoutOwner(rel) == currentIdentity();
}

bool AiviVC::checkoutMarkerOwnedByOther(const std::string &rel) const
{
    std::string owner = effectiveCheckoutOwner(rel);
    return !owner.empty() && owner != currentIdentity();
}

void AiviVC::markCheckedOut(const std::string &rel) const
{
    std::string marker = checkoutMarkerPath(rel);
    mkdirs(dirName(marker));
    std::ofstream output(marker.c_str());
    if (output) {
        output << "owner=" << currentIdentity() << "\n";
        output << "rel=" << (rel.empty() ? "." : rel) << "\n";
    }
}

void AiviVC::clearCheckedOut(const std::string &rel) const
{
    std::string marker = checkoutMarkerPath(rel);
    ::unlink(marker.c_str());
}

void AiviVC::makePathWritable(const std::string &path) const
{
    runCommand(std::vector<std::string>{"chmod", "-R", "u+w", path});
}

void AiviVC::protectPathFiles(const std::string &path) const
{
    if (!pathExists(path)) {
        return;
    }
    std::vector<std::string> myRels = checkedOutRelsByCurrentUser();
    if (!pathIsDir(path)) {
        if (!relCheckedOutByMe(pathToRel(root_, path), myRels)) {
            runCommand(std::vector<std::string>{"chmod", "u-w", path});
        }
        return;
    }
    CommandResult listed = runCommand(std::vector<std::string>{"find", path, "-type", "f"});
    if (!listed.ok()) {
        return;
    }
    std::vector<std::string> files = splitLines(listed.output);
    for (std::vector<std::string>::const_iterator it = files.begin(); it != files.end(); ++it) {
        if (!relCheckedOutByMe(pathToRel(root_, *it), myRels)) {
            runCommand(std::vector<std::string>{"chmod", "u-w", *it});
        }
    }
}

oa::oaUInt4 AiviVC::statusForObject(oaPlugIn::IDMObject *object) const
{
    std::string rel = objectRelPath(object);
    std::string path = fullPath(rel);
    oa::oaUInt4 status = 0;
    bool checkedOutByCurrentUser = checkoutMarkerOwnedByCurrentUser(rel);
    bool checkedOutByOther = checkoutMarkerOwnedByOther(rel);
    if (checkedOutByCurrentUser && pathWritable(path)) {
        status |= oaPlugIn::IDMObjectStatus::cEditable;
    }
    if (checkedOutByOther) {
        status |= oaPlugIn::IDMObjectStatus::cLocked;
    }
    if (!pathExists(path)) {
        status |= oaPlugIn::IDMObjectStatus::cMissing;
    }

    Backend backend = detectBackend();
    if (backend == kBackendGit) {
        CommandResult tracked = runCommand(std::vector<std::string>{gitCommand(), "-C", root_,
                                                                    "ls-files", "--error-unmatch",
                                                                    "--", rel.empty() ? "." : rel});
        if (tracked.ok()) {
            status |= oaPlugIn::IDMObjectStatus::cControlled;
        }

        CommandResult por = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "status",
                                                                "--porcelain=v1", "-b", "--",
                                                                rel.empty() ? "." : rel});
        if (por.ok()) {
            mapGitStatus(por.output, status);
            oa::oaUInt4 changed = oaPlugIn::IDMObjectStatus::cAdded |
                                  oaPlugIn::IDMObjectStatus::cDeleted |
                                  oaPlugIn::IDMObjectStatus::cModified |
                                  oaPlugIn::IDMObjectStatus::cConflict |
                                  oaPlugIn::IDMObjectStatus::cMissing |
                                  oaPlugIn::IDMObjectStatus::cRemote;
            if ((status & oaPlugIn::IDMObjectStatus::cControlled) &&
                !(status & changed)) {
                status |= oaPlugIn::IDMObjectStatus::cUpToDate;
            }
        }
        if ((status & oaPlugIn::IDMObjectStatus::cControlled) && !checkedOutByCurrentUser) {
            protectPathFiles(path);
        }
        return status;
    }

    CommandResult info = runCommand(std::vector<std::string>{svnCommand(), "info", path});
    if (info.ok()) {
        status |= oaPlugIn::IDMObjectStatus::cControlled;
    }
    CommandResult svnStatus = runCommand(std::vector<std::string>{svnCommand(), "status", "-u", path});
    if (svnStatus.ok()) {
        mapSvnStatus(svnStatus.output, status);
        oa::oaUInt4 changed = oaPlugIn::IDMObjectStatus::cAdded |
                              oaPlugIn::IDMObjectStatus::cDeleted |
                              oaPlugIn::IDMObjectStatus::cModified |
                              oaPlugIn::IDMObjectStatus::cConflict |
                              oaPlugIn::IDMObjectStatus::cMissing |
                              oaPlugIn::IDMObjectStatus::cRemote;
        if ((status & oaPlugIn::IDMObjectStatus::cControlled) &&
            !(status & changed)) {
            status |= oaPlugIn::IDMObjectStatus::cUpToDate;
        }
    }
    if ((status & oaPlugIn::IDMObjectStatus::cControlled) && !checkedOutByCurrentUser) {
        protectPathFiles(path);
    }
    return status;
}

void AiviVC::mapGitStatus(const std::string &output, oa::oaUInt4 &status)
{
    std::vector<std::string> lines = splitLines(output);
    for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->size() < 2) {
            continue;
        }
        if (it->find("## ") == 0) {
            if (it->find("behind") != std::string::npos ||
                it->find("diverged") != std::string::npos ||
                it->find("gone") != std::string::npos) {
                status |= oaPlugIn::IDMObjectStatus::cRemote;
            }
            if (it->find("ahead") != std::string::npos) {
                status |= oaPlugIn::IDMObjectStatus::cModified;
            }
            continue;
        }
        char x = (*it)[0];
        char y = (*it)[1];
        if (x == '?' && y == '?') {
            status |= oaPlugIn::IDMObjectStatus::cAdded;
            continue;
        }
        if (x == 'A' || y == 'A') {
            status |= oaPlugIn::IDMObjectStatus::cControlled | oaPlugIn::IDMObjectStatus::cAdded;
        }
        if (x == 'D' || y == 'D') {
            status |= oaPlugIn::IDMObjectStatus::cDeleted;
        }
        if (x == 'M' || y == 'M' || x == 'R' || y == 'R' || x == 'C' || y == 'C') {
            status |= oaPlugIn::IDMObjectStatus::cModified;
        }
        if (x == 'U' || y == 'U' || x == '!' || y == '!') {
            status |= oaPlugIn::IDMObjectStatus::cConflict;
        }
    }
}

void AiviVC::mapSvnStatus(const std::string &output, oa::oaUInt4 &status)
{
    std::vector<std::string> lines = splitLines(output);
    for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        if (it->empty()) {
            continue;
        }
        if (it->find("Status against revision:") == 0) {
            continue;
        }
        char c = (*it)[0];
        if (c != '?') {
            status |= oaPlugIn::IDMObjectStatus::cControlled;
        }
        if (c == 'A') {
            status |= oaPlugIn::IDMObjectStatus::cAdded;
        } else if (c == 'D') {
            status |= oaPlugIn::IDMObjectStatus::cDeleted;
        } else if (c == 'M') {
            status |= oaPlugIn::IDMObjectStatus::cModified;
        } else if (c == 'C') {
            status |= oaPlugIn::IDMObjectStatus::cConflict;
        } else if (c == '!') {
            status |= oaPlugIn::IDMObjectStatus::cMissing;
        }
        if (it->find('*') != std::string::npos) {
            status |= oaPlugIn::IDMObjectStatus::cRemote;
        }
        if (it->find('K') != std::string::npos || it->find('L') != std::string::npos) {
            status |= oaPlugIn::IDMObjectStatus::cLocked;
        }
    }
}

std::string AiviVC::workingVersion(oaPlugIn::IDMObject *object) const
{
    std::string rel = objectRelPath(object);
    Backend backend = detectBackend();
    if (backend == kBackendGit) {
        CommandResult result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "log",
                                                                   "-n", "1", "--format=%h", "--",
                                                                   rel.empty() ? "." : rel});
        std::string label = trim(result.output);
        if (result.ok() && !label.empty()) {
            if (!runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "status", "--porcelain",
                                                     "--", rel.empty() ? "." : rel}).output.empty()) {
                label += "+dirty";
            }
            return label;
        }
        CommandResult head = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "rev-parse",
                                                                 "--short", "HEAD"});
        label = trim(head.output);
        return head.ok() && !label.empty() ? label : "git:uncommitted";
    }

    CommandResult result = runCommand(std::vector<std::string>{svnCommand(), "info", "--show-item",
                                                               "revision", fullPath(rel)});
    std::string label = trim(result.output);
    return result.ok() && !label.empty() ? std::string("r") + label : "svn:unknown";
}

std::string AiviVC::parseVersionLine(Backend backend, const std::string &line)
{
    std::string s = trim(line);
    if (backend == kBackendGit) {
        return s;
    }
    if (!s.empty() && s[0] == 'r') {
        std::string::size_type end = s.find(' ');
        return end == std::string::npos ? s : s.substr(0, end);
    }
    return "";
}

std::string AiviVC::versionLabel(oaPlugIn::IVersion *version)
{
    return version ? trim(toStdString(version->getDisplayName())) : std::string();
}

std::string AiviVC::normalizeVersionLabel(std::string label)
{
    label = trim(label);
    if (label.find("git:") == 0 || label.find("svn:") == 0) {
        label = label.substr(4);
    }
    if (!label.empty() && label[0] == 'r') {
        bool numeric = true;
        for (std::string::size_type i = 1; i < label.size(); ++i) {
            if (label[i] < '0' || label[i] > '9') {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            label = label.substr(1);
        }
    }
    std::string::size_type dirty = label.find("+dirty");
    if (dirty != std::string::npos) {
        label = label.substr(0, dirty);
    }
    return label;
}

void AiviVC::reportCommand(oaPlugIn::IDMObject *object,
                           oa::oaUInt4 operation,
                           const CommandResult &result)
{
    oa::oaUInt4 type = result.ok() ? oaPlugIn::oacVCMsgTypeInfo : oaPlugIn::oacVCMsgTypeError;
    std::string msg = trim(result.output);
    if (msg.empty()) {
        msg = result.ok() ? "ok" : "command failed";
    }
    notify(object, operation, type, msg);
}

void AiviVC::notify(oaPlugIn::IDMObject *object,
                    oa::oaUInt4 operation,
                    oa::oaUInt4 msgType,
                    const std::string &text)
{
    for (std::vector<oaPlugIn::IVCMessageObserver *>::iterator it = observers_.begin();
         it != observers_.end(); ++it) {
        (*it)->onMessageOut(object, operation, msgType, text.c_str());
    }
}

} // namespace oaAiviVC
