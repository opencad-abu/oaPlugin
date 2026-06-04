// oaAiviVC.h - Git/SVN backed OpenAccess version-control plug-in.

#ifndef OA_AIVI_VC_H
#define OA_AIVI_VC_H

#include <oa/oaCommonFactory.h>
#include <oa/oaCommonIBase.h>
#include <oa/oaCommonPlugInBase.inl>
#include <oa/oaCommonPlugInMgr.h>
#include <oa/oaCommonSPtr.inl>
#include <oa/oaPlugInIDMObject.h>
#include <oa/oaPlugInVCInterfaces.h>

#include "oaPlugInDMObject.h"

#include <string>
#include <vector>

namespace oaAiviVC {

extern const char *const kClassId;
extern const char *const kAliasClassId;
extern const char *const kSystemClassId;
extern const char *const kRootAttr;
extern const char *const kBackendAttr;
extern const char *const kGitCommandAttr;
extern const char *const kSvnCommandAttr;
extern const char *const kLibPathAttr;
extern const char *const kWritePathAttr;

bool guidEqual(const oaCommon::Guid &a, const oaCommon::Guid &b);
std::string toStdString(const oaCommon::SRef<oaCommon::IString> &value);
std::string lower(std::string value);
std::string trim(const std::string &value);
bool pathExists(const std::string &path);
bool pathIsDir(const std::string &path);
bool pathWritable(const std::string &path);
std::string joinPath(const std::string &a, const std::string &b);
std::string dirName(const std::string &path);
bool mkdirs(const std::string &path);
std::string checkoutKey(const std::string &rel);
std::string currentIdentity();
std::string checkoutMarkerOwner(const std::string &path);

struct CommandResult {
    int status;
    std::string output;

    bool ok() const { return status == 0; }
};

CommandResult runCommand(const std::vector<std::string> &args);
std::vector<std::string> splitLines(const std::string &text);

class AiviVCVersion : public oaPlugIn::IVersion {
public:
    explicit AiviVCVersion(const char *displayName = NULL);

    unsigned long addRef() override;
    unsigned long release() override;
    unsigned long getRefCount() override;
    long queryInterface(const oaCommon::Guid &id, void **iPtr) override;

    oaCommon::SRef<oaCommon::IString> getVCSystemName() override;
    oaCommon::SRef<oaCommon::IString> getDisplayName() override;
    oa::oaUInt4 compare(oaPlugIn::IVersion *other) override;

private:
    unsigned long refCount_;
    std::string displayName_;
};

template <class IterT, class ItemT, const oaCommon::Guid *IterId>
class PtrIter : public IterT {
public:
    PtrIter() : refCount_(1), index_(0) {}
    explicit PtrIter(const std::vector<oaCommon::SPtr<ItemT> > &items)
        : refCount_(1), items_(items), index_(0)
    {
    }

    unsigned long addRef() override { return ++refCount_; }
    unsigned long release() override
    {
        unsigned long next = --refCount_;
        if (!next) {
            delete this;
        }
        return next;
    }
    unsigned long getRefCount() override { return refCount_; }

    long queryInterface(const oaCommon::Guid &id, void **iPtr) override
    {
        if (!iPtr) {
            return oaCommon::IBase::cFail;
        }
        *iPtr = NULL;
        if (guidEqual(id, *IterId) || guidEqual(id, oaCommon::IID_IBase)) {
            *iPtr = static_cast<IterT *>(this);
            addRef();
            return oaCommon::IBase::cOK;
        }
        return oaCommon::IBase::cNoInterface;
    }

    bool next(ItemT *&objOut) override
    {
        if (index_ >= items_.size()) {
            objOut = NULL;
            return false;
        }
        objOut = items_[index_++].getRef();
        return true;
    }

    void reset() override { index_ = 0; }

private:
    unsigned long refCount_;
    std::vector<oaCommon::SPtr<ItemT> > items_;
    typename std::vector<oaCommon::SPtr<ItemT> >::size_type index_;
};

typedef PtrIter<oaPlugIn::IVersionIter, oaPlugIn::IVersion,
                &oaPlugIn::IID_IVersionIter> VersionIter;
typedef PtrIter<oaPlugIn::IDMObjectStatusIter, oaPlugIn::IDMObjectStatus,
                &oaPlugIn::IID_IDMObjectStatusIter> StatusIter;
typedef PtrIter<oaPlugIn::IDMObjectVersionIter, oaPlugIn::IDMObjectVersion,
                &oaPlugIn::IID_IDMObjectVersionIter> ObjectVersionIter;
typedef PtrIter<oaPlugIn::IDMLibIter, oaPlugIn::IDMLib,
                &oaPlugIn::IID_IDMLibIter> LibIter;
typedef PtrIter<oaPlugIn::IDMObjectIter, oaPlugIn::IDMObject,
                &oaPlugIn::IID_IDMObjectIter> ObjectIter;

enum Backend {
    kBackendGit,
    kBackendSvn
};

class AiviVC : public oaPlugIn::IVCPlugIn,
               public oaPlugIn::IVersionControl,
               public oaPlugIn::IVCSystem {
public:
    AiviVC();
    ~AiviVC();

    unsigned long addRef() override;
    unsigned long release() override;
    unsigned long getRefCount() override;
    long queryInterface(const oaCommon::Guid &id, void **iPtr) override;

    bool init(const char *dmSystemName, const char *libName,
              oaPlugIn::IDMAccess *dmAccess, oaPlugIn::IAttrIter *attrs) override;
    bool setAttributes(oaPlugIn::IAttrIter *attrs) override;
    void getAttributes(oaPlugIn::IAttrIter *&attrs) override;
    void newVersionObject(oaPlugIn::IVersion *&version,
                          const char *displayName = NULL) override;

    void getVCSystem(oaPlugIn::IVCSystem *&system) override;
    void addObserver(oaPlugIn::IVCMessageObserver *observer) override;
    void removeObserver(oaPlugIn::IVCMessageObserver *observer) override;
    oa::oaUInt4 getStatus(oaPlugIn::IDMObject *object,
                          oa::oaUInt4 mask = oaPlugIn::IDMObjectStatus::cAllStatus) override;
    void getStatus(oaPlugIn::IDMObjectStatusIter *&info, oaPlugIn::IDMContainer *cont,
                   oa::oaUInt4 depth,
                   oa::oaUInt4 mask = oaPlugIn::IDMObjectStatus::cAllStatus) override;
    void getControlledObjects(oaPlugIn::IDMObjectIter *&objects, oa::oaUInt4 depth,
                              oaPlugIn::IDMObject *object, bool recurse) override;
    void getVersions(oaPlugIn::IVersionIter *&versions,
                     oaPlugIn::IDMObject *object) override;
    void getWorkingVersion(oaPlugIn::IVersion *&version,
                           oaPlugIn::IDMObject *object) override;
    void getWorkingVersions(oaPlugIn::IDMObjectVersionIter *&objects,
                            oaPlugIn::IDMContainer *cont, oa::oaUInt4 depth) override;
    void update(oaPlugIn::IVersion *version, oaPlugIn::IDMObject *object,
                bool recurse, const char *comment) override;
    void makeEditable(oaPlugIn::IDMObject *object, bool lock, bool recurse,
                      const char *comment) override;
    void cancelEdit(oaPlugIn::IDMObject *object, bool recurse,
                    const char *comment) override;
    void commitEdits(oaPlugIn::IDMObject *object, const char *comment,
                     bool recurse, bool keepLocks, const char *tag) override;
    void setControlled(oaPlugIn::IDMObject *object, bool recurse,
                       const char *comment) override;
    void unsetControlled(oaPlugIn::IDMObject *object, bool recurse, bool keepLocal,
                         const char *comment) override;

    oaCommon::SRef<oaCommon::IString> getName() override;
    bool testCapability(oa::oaUInt4 capability) override;
    void getControlledLibs(oaPlugIn::IDMLibIter *&libs) override;

private:
    void parseAttrs(oaPlugIn::IAttrIter *attrs);
    Backend detectBackend() const;
    bool ensureGitRepo();
    const std::string &gitCommand() const { return gitCommand_; }
    const std::string &svnCommand() const { return svnCommand_; }
    std::string fullPath(const std::string &rel) const;
    std::string objectRelPath(oaPlugIn::IDMObject *object) const;
    std::string mapPrimaryFileName(const std::string &parentRel,
                                   const std::string &logicalName) const;
    std::string checkoutMarkerPath(const std::string &rel) const;
    std::string checkoutMarkerDirPath() const;
    std::string effectiveCheckoutOwner(const std::string &rel) const;
    bool checkoutMarkerOverlapsOther(const std::string &rel) const;
    std::vector<std::string> checkedOutRelsByCurrentUser() const;
    bool checkoutMarkerOwnedByCurrentUser(const std::string &rel) const;
    bool checkoutMarkerOwnedByOther(const std::string &rel) const;
    void markCheckedOut(const std::string &rel) const;
    void clearCheckedOut(const std::string &rel) const;
    void makePathWritable(const std::string &path) const;
    void protectPathFiles(const std::string &path) const;
    oa::oaUInt4 statusForObject(oaPlugIn::IDMObject *object) const;
    void collectVersionableObjects(std::vector<oaCommon::SPtr<oaPlugIn::IDMObject> > &objects,
                                   oaPlugIn::IDMObject *top,
                                   oa::oaUInt4 depth,
                                   bool controlledOnly,
                                   bool localOnly) const;
    static void mapGitStatus(const std::string &output, oa::oaUInt4 &status);
    static void mapSvnStatus(const std::string &output, oa::oaUInt4 &status);
    std::string workingVersion(oaPlugIn::IDMObject *object) const;
    static std::string parseVersionLine(Backend backend, const std::string &line);
    static std::string versionLabel(oaPlugIn::IVersion *version);
    static std::string normalizeVersionLabel(std::string label);
    void reportCommand(oaPlugIn::IDMObject *object, oa::oaUInt4 operation,
                       const CommandResult &result);
    void notify(oaPlugIn::IDMObject *object, oa::oaUInt4 operation,
                oa::oaUInt4 msgType, const std::string &text);

    unsigned long refCount_;
    oaCommon::SPtr<oaPlugIn::IDMAccess> dmAccess_;
    std::string dmSystemName_;
    std::string libName_;
    std::string root_;
    std::string writePath_;
    std::string backend_;
    std::string gitCommand_;
    std::string svnCommand_;
    std::vector<oaPlugIn::IVCMessageObserver *> observers_;
};

} // namespace oaAiviVC

#endif // OA_AIVI_VC_H
