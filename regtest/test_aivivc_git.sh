#!/bin/bash
# Regression for oaAiviVC Git bridge and OA VC depth enumeration.
set -eu

AIVIVC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OACPP="${OACPP:-/workarea/ai/openclaw/oacpp}"
CXX="${CXX:-g++}"
OA_HEADERS="${OA_HEADERS:-/workarea/xh/oa/oasrc/include}"
SYSNAME="${SYSNAME:-$("$AIVIVC_ROOT/build/bin/sysname")_64}"
OPTMODE="${OPTMODE:-opt}"
CORE_LIBDIR="${CORE_LIBDIR:-$OACPP/lib/$SYSNAME/$OPTMODE}"
AIVIVC_LIBDIR="${AIVIVC_LIBDIR:-$AIVIVC_ROOT/src/lib/aivivc/$SYSNAME/$OPTMODE}"
if [ ! -f "$AIVIVC_LIBDIR/liboaAiviVC.so" ]; then
    AIVIVC_LIBDIR="$AIVIVC_ROOT/lib/$SYSNAME/$OPTMODE"
fi
TMPDIR="$(mktemp -d /tmp/aivivc-regtest.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

cat > "$TMPDIR/test_aivivc_git.cpp" <<'CPP'
#include <oa/oaCommonPlugInBase.inl>
#include <oa/oaCommonSPtr.inl>
#include <oa/oaPlugInIDMObject.h>
#include <oa/oaPlugInVCInterfaces.h>

#include "oaPlugInDMObject.h"
#include "oaPlugInDMObject.inl"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

typedef oaCommon::ArrayIter<oaPlugIn::IDMFileIter,
                            oaCommon::SPtr<oaPlugIn::IDMFile> > FileIter;
typedef oaCommon::ArrayIter<oaPlugIn::ICellViewIter,
                            oaCommon::SPtr<oaPlugIn::ICellView> > CellViewIter;

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

static oaCommon::SRef<oaCommon::IString> strRef(const char *text)
{
    return oaCommon::SRef<oaCommon::IString>(new oaCommon::StringImp(text));
}

int main(int argc, char **argv)
{
    require(argc == 3, "usage: test_aivivc_git ROOT GIT_COMMAND");

    oaPlugIn::DMAttrArrayIter attrs;
    attrs.add(new oaPlugIn::DMAttr("AiviVCRoot", argv[1]));
    attrs.add(new oaPlugIn::DMAttr("AiviVCBackend", "git"));
    attrs.add(new oaPlugIn::DMAttr("AiviVCGitCommand", argv[2]));

    oaCommon::SPtr<oaPlugIn::IVCPlugIn> plugin("oaAiviVC");
    require(plugin.ptr(), "oaAiviVC load failed");
    require(plugin->init("oaDMFileSys", "regtest", NULL, &attrs), "oaAiviVC init failed");

    oaPlugIn::IVersionControl *rawVC = NULL;
    require(plugin->queryInterface(oaPlugIn::IID_IVersionControl,
                                   reinterpret_cast<void **>(&rawVC)) == oaCommon::IBase::cOK &&
                rawVC,
            "IVersionControl query failed");
    oaCommon::SPtr<oaPlugIn::IVersionControl> vc(rawVC);
    rawVC->release();

    oaPlugIn::CellView *fileParent =
        new oaPlugIn::CellView("myCell", "schematic", "schematic");
    oaPlugIn::DMFile *primary =
        new oaPlugIn::DMFile(fileParent, "sch.oa", NULL, true, true);
    FileIter *files = new FileIter();
    files->add(oaCommon::SPtr<oaPlugIn::IDMFile>(primary));

    oaPlugIn::Cell *cell = new oaPlugIn::Cell("myCell");
    oaPlugIn::View *view = new oaPlugIn::View("schematic", "schematic");
    oaPlugIn::CellView *cellView = new oaPlugIn::CellView(cell, view, files);
    CellViewIter *cellViews = new CellViewIter();
    cellViews->add(oaCommon::SPtr<oaPlugIn::ICellView>(cellView));
    oaPlugIn::DMLib *lib = new oaPlugIn::DMLib("regtest", NULL, NULL, cellViews, NULL);

    oaPlugIn::IDMObjectIter *objects = NULL;
    vc->getControlledObjects(objects, oaPlugIn::oacCellViewFileVCQueryDepth, lib, false);
    int objectCount = 0;
    oaPlugIn::IDMObject *object = NULL;
    while (objects && objects->next(object)) {
        ++objectCount;
        object->release();
    }
    require(objectCount == 1, "expected one controlled cellView file");
    objects->release();

    oaPlugIn::IDMObjectStatusIter *statuses = NULL;
    vc->getStatus(statuses, lib, oaPlugIn::oacCellViewFileVCQueryDepth,
                  oaPlugIn::IDMObjectStatus::cAllStatus);
    oaPlugIn::IDMObjectStatus *statusInfo = NULL;
    require(statuses && statuses->next(statusInfo), "expected status info");
    oa::oaUInt4 status = statusInfo->getStatus();
    require(status & oaPlugIn::IDMObjectStatus::cControlled, "expected controlled status");
    require(status & oaPlugIn::IDMObjectStatus::cUpToDate, "expected up-to-date status");
    statusInfo->release();
    statuses->release();

    require((vc->getStatus(primary, oaPlugIn::IDMObjectStatus::cAllStatus) &
             oaPlugIn::IDMObjectStatus::cEditable) == 0,
            "expected protected status before makeEditable");
    vc->makeEditable(cellView, false, false, NULL);
    require(access((std::string(argv[1]) + "/myCell/schematic/sch.oa").c_str(), W_OK) == 0,
            "expected writable file after cellView makeEditable");
    status = vc->getStatus(primary, oaPlugIn::IDMObjectStatus::cAllStatus);
    require(status & oaPlugIn::IDMObjectStatus::cEditable,
            "expected file under checked out cellView to be editable");
    vc->cancelEdit(cellView, false, NULL);
    require(access((std::string(argv[1]) + "/myCell/schematic/sch.oa").c_str(), W_OK) != 0,
            "expected protected file after cellView cancelEdit");
    require((vc->getStatus(primary, oaPlugIn::IDMObjectStatus::cAllStatus) &
             oaPlugIn::IDMObjectStatus::cEditable) == 0,
            "expected non-editable file after cancelEdit");

    oaPlugIn::IDMObjectVersionIter *versions = NULL;
    vc->getWorkingVersions(versions, lib, oaPlugIn::oacCellViewFileVCQueryDepth);
    oaPlugIn::IDMObjectVersion *versionInfo = NULL;
    require(versions && versions->next(versionInfo), "expected working version info");
    versionInfo->release();
    versions->release();

    vc->commitEdits(lib, "aivivc no-op", true, false, NULL);
    std::cout << "aivivc git regtest ok\n";
    return 0;
}
CPP

mkdir -p "$TMPDIR/repo/myCell/schematic"
printf '%s\n%s\n' '-- Master.tag File, Rev:1.0' 'ViewType=schematic' \
    > "$TMPDIR/repo/myCell/schematic/master.tag"
printf 'oa-data\n' > "$TMPDIR/repo/myCell/schematic/sch.oa"

git init "$TMPDIR/repo" >/dev/null
git -C "$TMPDIR/repo" config user.email aivivc@example.invalid
git -C "$TMPDIR/repo" config user.name aivivc
git -C "$TMPDIR/repo" config commit.gpgsign false
git -C "$TMPDIR/repo" add .
git -C "$TMPDIR/repo" commit -m initial >/dev/null

cat > "$TMPDIR/git-wrapper" <<'SH'
#!/bin/sh
exec git "$@"
SH
chmod +x "$TMPDIR/git-wrapper"

"$CXX" -std=c++17 -O0 -g \
    -I"$OA_HEADERS" -I"$OACPP/src/plugIn" -I"$OACPP/src/common" \
    -L"$CORE_LIBDIR" "$TMPDIR/test_aivivc_git.cpp" \
    -loaPlugIn -loaCommon -ldl -o "$TMPDIR/test_aivivc_git"

LD_LIBRARY_PATH="$AIVIVC_LIBDIR:$CORE_LIBDIR" \
OA_PLUGIN_PATH="$AIVIVC_ROOT/data/plugins:$OACPP/data/plugins" \
    "$TMPDIR/test_aivivc_git" "$TMPDIR/repo" "$TMPDIR/git-wrapper"
