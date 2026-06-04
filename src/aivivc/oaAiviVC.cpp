#include "oaAiviVC.h"
#include "oaPlugInDMObject.inl"

#include <cstdlib>
#include <cstring>

namespace oaAiviVC {

AiviVC::AiviVC() : refCount_(0), backend_("auto"), gitCommand_("git"), svnCommand_("svn")
{
}

AiviVC::~AiviVC()
{
    for (std::vector<oaPlugIn::IVCMessageObserver *>::iterator it = observers_.begin();
         it != observers_.end(); ++it) {
        (*it)->release();
    }
}

unsigned long AiviVC::addRef()
{
    return ++refCount_;
}

unsigned long AiviVC::release()
{
    unsigned long next = --refCount_;
    if (!next) {
        delete this;
    }
    return next;
}

unsigned long AiviVC::getRefCount()
{
    return refCount_;
}

long AiviVC::queryInterface(const oaCommon::Guid &id, void **iPtr)
{
    if (!iPtr) {
        return oaCommon::IBase::cFail;
    }
    *iPtr = NULL;
    if (guidEqual(id, oaPlugIn::IID_IVCPlugIn)) {
        *iPtr = static_cast<oaPlugIn::IVCPlugIn *>(this);
    } else if (guidEqual(id, oaPlugIn::IID_IVersionControl)) {
        *iPtr = static_cast<oaPlugIn::IVersionControl *>(this);
    } else if (guidEqual(id, oaPlugIn::IID_IVCSystem)) {
        *iPtr = static_cast<oaPlugIn::IVCSystem *>(this);
    } else if (guidEqual(id, oaCommon::IID_IBase)) {
        *iPtr = static_cast<oaPlugIn::IVersionControl *>(this);
    } else {
        return oaCommon::IBase::cNoInterface;
    }
    addRef();
    return oaCommon::IBase::cOK;
}

bool AiviVC::init(const char *dmSystemName, const char *libName,
                  oaPlugIn::IDMAccess *dmAccess, oaPlugIn::IAttrIter *attrs)
{
    dmSystemName_ = dmSystemName ? dmSystemName : "";
    libName_ = libName ? libName : "";
    dmAccess_ = dmAccess;
    parseAttrs(attrs);

    if (root_.empty()) {
        const char *envRoot = std::getenv("AIVIVC_ROOT");
        if (envRoot && *envRoot) {
            root_ = envRoot;
        }
    }
    if (backend_ == "auto") {
        const char *envBackend = std::getenv("AIVIVC_BACKEND");
        if (envBackend && *envBackend) {
            backend_ = lower(envBackend);
        }
    }
    const char *envGitCommand = std::getenv("AIVIVC_GIT_COMMAND");
    if (!envGitCommand || !*envGitCommand) {
        envGitCommand = std::getenv("AIVIVC_GIT");
    }
    if (envGitCommand && *envGitCommand) {
        gitCommand_ = envGitCommand;
    }
    const char *envSvnCommand = std::getenv("AIVIVC_SVN_COMMAND");
    if (!envSvnCommand || !*envSvnCommand) {
        envSvnCommand = std::getenv("AIVIVC_SVN");
    }
    if (envSvnCommand && *envSvnCommand) {
        svnCommand_ = envSvnCommand;
    }
    if (root_.empty()) {
        root_ = ".";
    }
    notify(NULL, oaPlugIn::oacVCOperationGetStatus, oaPlugIn::oacVCMsgTypeInfo,
           std::string("oaAiviVC initialized for ") + libName_ + " at " + root_);
    return true;
}

bool AiviVC::setAttributes(oaPlugIn::IAttrIter *attrs)
{
    parseAttrs(attrs);
    return true;
}

void AiviVC::getAttributes(oaPlugIn::IAttrIter *&attrs)
{
    oaPlugIn::DMAttrArrayIter *iter = new oaPlugIn::DMAttrArrayIter();
    iter->add(new oaPlugIn::DMAttr(kRootAttr, root_.c_str()));
    iter->add(new oaPlugIn::DMAttr(kBackendAttr, backend_.c_str()));
    iter->add(new oaPlugIn::DMAttr(kGitCommandAttr, gitCommand_.c_str()));
    iter->add(new oaPlugIn::DMAttr(kSvnCommandAttr, svnCommand_.c_str()));
    attrs = iter;
}

void AiviVC::newVersionObject(oaPlugIn::IVersion *&version, const char *displayName)
{
    version = new AiviVCVersion(displayName);
}

void AiviVC::getVCSystem(oaPlugIn::IVCSystem *&system)
{
    system = static_cast<oaPlugIn::IVCSystem *>(this);
    addRef();
}

void AiviVC::addObserver(oaPlugIn::IVCMessageObserver *observer)
{
    if (!observer) {
        return;
    }
    observer->addRef();
    observers_.push_back(observer);
}

void AiviVC::removeObserver(oaPlugIn::IVCMessageObserver *observer)
{
    for (std::vector<oaPlugIn::IVCMessageObserver *>::iterator it = observers_.begin();
         it != observers_.end(); ++it) {
        if (*it == observer) {
            (*it)->release();
            observers_.erase(it);
            return;
        }
    }
}

oa::oaUInt4 AiviVC::getStatus(oaPlugIn::IDMObject *object, oa::oaUInt4 mask)
{
    oa::oaUInt4 status = statusForObject(object);
    return status & mask;
}

void AiviVC::getStatus(oaPlugIn::IDMObjectStatusIter *&info,
                       oaPlugIn::IDMContainer *cont,
                       oa::oaUInt4 depth,
                       oa::oaUInt4 mask)
{
    std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> > objects;
    std::vector<oaCommon::SPtr<oaPlugIn::IDMObjectStatus> > items;
    collectVersionableObjects(objects, cont, depth, false, false);

    for (std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> >::iterator it = objects.begin();
         it != objects.end(); ++it) {
        oaPlugIn::IDMObject *obj = it->getRef();
        oa::oaUInt4 stat = getStatus(obj, mask);
        items.push_back(oaCommon::SPtr<oaPlugIn::IDMObjectStatus>(
            new oaPlugIn::DMObjectStatus(obj, stat)));
        obj->release();
    }
    info = new StatusIter(items);
}

void AiviVC::getControlledObjects(oaPlugIn::IDMObjectIter *&objects,
                                  oa::oaUInt4 depth,
                                  oaPlugIn::IDMObject *object,
                                  bool localOnly)
{
    std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> > items;
    collectVersionableObjects(items, object, depth, true, localOnly);
    objects = new ObjectIter(items);
}

void AiviVC::getVersions(oaPlugIn::IVersionIter *&versions, oaPlugIn::IDMObject *object)
{
    std::vector<oaCommon::SPtr<oaPlugIn::IVersion> > items;
    std::string rel = objectRelPath(object);
    Backend backend = detectBackend();
    CommandResult result;
    if (backend == kBackendGit) {
        result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "log", "-n",
                                                     "50", "--format=%h", "--",
                                                     rel.empty() ? "." : rel});
    } else {
        result = runCommand(std::vector<std::string>{svnCommand(), "log", "-q", "-l",
                                                     "50", fullPath(rel)});
    }

    if (result.ok()) {
        std::vector<std::string> lines = splitLines(result.output);
        for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
            std::string label = parseVersionLine(backend, *it);
            if (!label.empty()) {
                items.push_back(oaCommon::SPtr<oaPlugIn::IVersion>(
                    new AiviVCVersion(label.c_str())));
            }
        }
    }
    versions = new VersionIter(items);
}

void AiviVC::getWorkingVersion(oaPlugIn::IVersion *&version, oaPlugIn::IDMObject *object)
{
    std::string label = workingVersion(object);
    version = new AiviVCVersion(label.c_str());
}

void AiviVC::getWorkingVersions(oaPlugIn::IDMObjectVersionIter *&objects,
                                oaPlugIn::IDMContainer *cont,
                                oa::oaUInt4 depth)
{
    std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> > dmObjects;
    std::vector<oaCommon::SPtr<oaPlugIn::IDMObjectVersion> > items;
    collectVersionableObjects(dmObjects, cont, depth, false, false);

    for (std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> >::iterator it = dmObjects.begin();
         it != dmObjects.end(); ++it) {
        oaPlugIn::IDMObject *obj = it->getRef();
        oaCommon::SPtr<oaPlugIn::IVersion> ver(new AiviVCVersion(workingVersion(obj).c_str()));
        items.push_back(oaCommon::SPtr<oaPlugIn::IDMObjectVersion>(
            new oaPlugIn::DMObjectVersion(obj, ver.ptr())));
        obj->release();
    }
    objects = new ObjectVersionIter(items);
}

void AiviVC::update(oaPlugIn::IVersion *version,
                    oaPlugIn::IDMObject *object,
                    bool,
                    const char *)
{
    std::string rel = objectRelPath(object);
    std::string label = versionLabel(version);
    Backend backend = detectBackend();
    CommandResult result;
    if (backend == kBackendGit) {
        if (!label.empty() && label != "HEAD") {
            result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "checkout",
                                                         normalizeVersionLabel(label), "--",
                                                         rel.empty() ? "." : rel});
        } else {
            result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "pull", "--ff-only"});
        }
    } else {
        std::vector<std::string> args;
        args.push_back(svnCommand());
        args.push_back("update");
        if (!label.empty() && label != "HEAD") {
            args.push_back("-r");
            args.push_back(normalizeVersionLabel(label));
        }
        args.push_back(fullPath(rel));
        result = runCommand(args);
    }
    if (result.ok()) {
        protectPathFiles(fullPath(rel));
    }
    reportCommand(object, oaPlugIn::oacVCOperationUpdate, result);
}

void AiviVC::makeEditable(oaPlugIn::IDMObject *object,
                          bool lock,
                          bool,
                          const char *)
{
    std::string rel = objectRelPath(object);
    std::string target = fullPath(rel);
    if (checkoutMarkerOverlapsOther(rel)) {
        CommandResult locked;
        locked.status = 1;
        locked.output = "Locked (Other check out)";
        reportCommand(object, oaPlugIn::oacVCOperationMakeEditable, locked);
        return;
    }
    markCheckedOut(rel);
    makePathWritable(target);

    Backend backend = detectBackend();
    if (backend == kBackendSvn && lock) {
        CommandResult result = runCommand(std::vector<std::string>{svnCommand(), "lock", target});
        if (!result.ok()) {
            clearCheckedOut(rel);
            protectPathFiles(target);
        }
        reportCommand(object, oaPlugIn::oacVCOperationMakeEditable, result);
        return;
    }
    notify(object, oaPlugIn::oacVCOperationMakeEditable, oaPlugIn::oacVCMsgTypeInfo,
           "made editable");
}

void AiviVC::cancelEdit(oaPlugIn::IDMObject *object, bool recurse, const char *)
{
    std::string rel = objectRelPath(object);
    Backend backend = detectBackend();
    CommandResult result;
    if (backend == kBackendGit) {
        runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "reset", "--",
                                            rel.empty() ? "." : rel});
        result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "checkout", "--",
                                                     rel.empty() ? "." : rel});
        if (result.ok() && recurse) {
            runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "clean", "-fd", "--",
                                                rel.empty() ? "." : rel});
        }
    } else {
        std::vector<std::string> args;
        args.push_back(svnCommand());
        args.push_back("revert");
        if (recurse || pathIsDir(fullPath(rel))) {
            args.push_back("-R");
        }
        args.push_back(fullPath(rel));
        result = runCommand(args);
    }
    if (result.ok()) {
        clearCheckedOut(rel);
        protectPathFiles(fullPath(rel));
    }
    reportCommand(object, oaPlugIn::oacVCOperationCancelEdit, result);
}

void AiviVC::commitEdits(oaPlugIn::IDMObject *object,
                         const char *comment,
                         bool,
                         bool,
                         const char *)
{
    std::string rel = objectRelPath(object);
    std::string msg = comment && *comment ? comment : "OA commit";
    Backend backend = detectBackend();
    CommandResult result;
    if (backend == kBackendGit) {
        ensureGitRepo();
        CommandResult addResult = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "add",
                                                                      "--", rel.empty() ? "." : rel});
        if (!addResult.ok()) {
            reportCommand(object, oaPlugIn::oacVCOperationCommitEdits, addResult);
            return;
        }
        result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "commit", "-m",
                                                     msg, "--", rel.empty() ? "." : rel});
        if (!result.ok() &&
            (result.output.find("nothing to commit") != std::string::npos ||
             result.output.find("no changes added") != std::string::npos)) {
            result.status = 0;
        }
    } else {
        result = runCommand(std::vector<std::string>{svnCommand(), "commit", "-m", msg, fullPath(rel)});
    }
    if (result.ok()) {
        clearCheckedOut(rel);
        protectPathFiles(fullPath(rel));
    }
    reportCommand(object, oaPlugIn::oacVCOperationCommitEdits, result);
}

void AiviVC::setControlled(oaPlugIn::IDMObject *object, bool, const char *)
{
    std::string rel = objectRelPath(object);
    Backend backend = detectBackend();
    CommandResult result;
    if (backend == kBackendGit) {
        if (!ensureGitRepo()) {
            result.status = 1;
            result.output = "unable to initialize git repository";
        } else {
            result = runCommand(std::vector<std::string>{gitCommand(), "-C", root_, "add", "--",
                                                         rel.empty() ? "." : rel});
        }
    } else {
        result = runCommand(std::vector<std::string>{svnCommand(), "add", "--parents", fullPath(rel)});
    }
    if (result.ok()) {
        protectPathFiles(fullPath(rel));
    }
    reportCommand(object, oaPlugIn::oacVCOperationSetControlled, result);
}

void AiviVC::unsetControlled(oaPlugIn::IDMObject *object, bool, bool keepLocal, const char *)
{
    std::string rel = objectRelPath(object);
    Backend backend = detectBackend();
    CommandResult result;
    if (backend == kBackendGit) {
        std::vector<std::string> args;
        args.push_back(gitCommand());
        args.push_back("-C");
        args.push_back(root_);
        args.push_back("rm");
        args.push_back("-r");
        if (keepLocal) {
            args.push_back("--cached");
        }
        args.push_back("--");
        args.push_back(rel.empty() ? "." : rel);
        result = runCommand(args);
    } else {
        std::vector<std::string> args;
        args.push_back(svnCommand());
        args.push_back("delete");
        if (keepLocal) {
            args.push_back("--keep-local");
        }
        args.push_back(fullPath(rel));
        result = runCommand(args);
    }
    reportCommand(object, oaPlugIn::oacVCOperationUnsetControlled, result);
}

oaCommon::SRef<oaCommon::IString> AiviVC::getName()
{
    return oaCommon::SRef<oaCommon::IString>(new oaCommon::StringImp(kClassId));
}

bool AiviVC::testCapability(oa::oaUInt4 capability)
{
    return capability == oaPlugIn::oacHasVersionsVCCap ||
           capability == oaPlugIn::oacSupportsGetStatusVCCap ||
           capability == oaPlugIn::oacSupportsGetVersionsVCCap ||
           capability == oaPlugIn::oacSupportsGetWorkingVersionVCCap ||
           capability == oaPlugIn::oacSupportsGetWorkingVersionsVCCap ||
           capability == oaPlugIn::oacSupportsUpdateVCCap ||
           capability == oaPlugIn::oacSupportsMakeEditableVCCap ||
           capability == oaPlugIn::oacSupportsLockVCCap ||
           capability == oaPlugIn::oacSupportsCancelEditVCCap ||
           capability == oaPlugIn::oacSupportsCommitEditsVCCap ||
           capability == oaPlugIn::oacSupportsSetControlledVCCap ||
           capability == oaPlugIn::oacSupportsUnSetControlledVCCap;
}

void AiviVC::getControlledLibs(oaPlugIn::IDMLibIter *&libs)
{
    libs = new LibIter();
}

class AiviVCFactory : public oaCommon::IFactory {
public:
    oa::oaUInt4 createInstance(oaCommon::IBase *,
                               const oaCommon::Guid &id,
                               void **iPtr) override
    {
        if (!iPtr) {
            return oaCommon::IBase::cFail;
        }
        AiviVC *vc = new AiviVC();
        oa::oaUInt4 result = vc->queryInterface(id, iPtr);
        if (result != oaCommon::IBase::cOK) {
            delete vc;
            *iPtr = NULL;
        }
        return result;
    }

    unsigned long addRef() override { return 1; }
    unsigned long release() override { return 1; }
    unsigned long getRefCount() override { return 1; }

    long queryInterface(const oaCommon::Guid &id, void **iPtr) override
    {
        if (!iPtr) {
            return oaCommon::IBase::cFail;
        }
        *iPtr = NULL;
        if (guidEqual(id, oaCommon::IID_IFactory) || guidEqual(id, oaCommon::IID_IBase)) {
            *iPtr = static_cast<oaCommon::IFactory *>(this);
            return oaCommon::IBase::cOK;
        }
        return oaCommon::IBase::cNoInterface;
    }
};

AiviVCFactory gFactory;

static bool validClassId(const char *classId)
{
    return classId && (!std::strcmp(classId, kClassId) ||
                       !std::strcmp(classId, kAliasClassId) ||
                       !std::strcmp(classId, kSystemClassId));
}

} // namespace oaAiviVC

extern "C" void oaAiviVCInit()
{
    oaCommon::oaPlugInMgr::registerFactory(oaAiviVC::kClassId, &oaAiviVC::gFactory);
    oaCommon::oaPlugInMgr::registerFactory(oaAiviVC::kAliasClassId, &oaAiviVC::gFactory);
    oaCommon::oaPlugInMgr::registerFactory(oaAiviVC::kSystemClassId, &oaAiviVC::gFactory);
}

extern "C" long getClassObject(const char *classId,
                               const oaCommon::Guid &interfaceId,
                               void **instance)
{
    if (!oaAiviVC::validClassId(classId) || !instance) {
        return oaCommon::IBase::cFail;
    }
    return oaAiviVC::gFactory.queryInterface(interfaceId, instance);
}
