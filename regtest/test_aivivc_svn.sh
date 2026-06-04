#!/bin/bash
# Regression for oaAiviVC SVN bridge and OA VC depth enumeration.
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
SVN_ROOT="${SVN_ROOT:-/software/pkgs/subversion/usr}"
TMPDIR="$(mktemp -d /tmp/aivivc-svn-regtest.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

if [ -x "$SVN_ROOT/bin/svn" ]; then
    export PATH="$SVN_ROOT/bin:$PATH"
    export LD_LIBRARY_PATH="$SVN_ROOT/lib64:${LD_LIBRARY_PATH:-}"
fi
command -v svn >/dev/null
command -v svnadmin >/dev/null

cat > "$TMPDIR/test_aivivc_svn.cpp" <<'CPP'
#include <oa/oaCommonPlugInBase.inl>
#include <oa/oaCommonSPtr.inl>
#include <oa/oaPlugInIDMObject.h>
#include <oa/oaPlugInVCInterfaces.h>

#include "oaPlugInDMObject.h"
#include "oaPlugInDMObject.inl"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

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

static void requireStatus(oa::oaUInt4 status, oa::oaUInt4 mask, const char *message)
{
    require((status & mask) != 0, message);
}

int main(int argc, char **argv)
{
    require(argc == 3, "usage: test_aivivc_svn ROOT SVN_COMMAND");

    oaPlugIn::DMAttrArrayIter attrs;
    attrs.add(new oaPlugIn::DMAttr("AiviVCRoot", argv[1]));
    attrs.add(new oaPlugIn::DMAttr("AiviVCBackend", "svn"));
    attrs.add(new oaPlugIn::DMAttr("AiviVCSvnCommand", argv[2]));

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

    oa::oaUInt4 status = vc->getStatus(primary, oaPlugIn::IDMObjectStatus::cAllStatus);
    requireStatus(status, oaPlugIn::IDMObjectStatus::cControlled, "expected controlled status");
    requireStatus(status, oaPlugIn::IDMObjectStatus::cUpToDate, "expected up-to-date status");
    require((status & oaPlugIn::IDMObjectStatus::cEditable) == 0,
            "expected protected status before makeEditable");

    vc->makeEditable(cellView, false, false, NULL);
    std::ofstream changed(std::string(argv[1]) + "/myCell/schematic/sch.oa",
                          std::ios::app);
    changed << "changed\n";
    changed.close();
    status = vc->getStatus(primary, oaPlugIn::IDMObjectStatus::cAllStatus);
    requireStatus(status, oaPlugIn::IDMObjectStatus::cModified, "expected modified status");
    requireStatus(status, oaPlugIn::IDMObjectStatus::cEditable, "expected editable status");

    vc->cancelEdit(cellView, false, NULL);
    status = vc->getStatus(primary, oaPlugIn::IDMObjectStatus::cAllStatus);
    requireStatus(status, oaPlugIn::IDMObjectStatus::cUpToDate, "expected reverted status");

    oaPlugIn::IDMObjectVersionIter *versions = NULL;
    vc->getWorkingVersions(versions, lib, oaPlugIn::oacCellViewFileVCQueryDepth);
    oaPlugIn::IDMObjectVersion *versionInfo = NULL;
    require(versions && versions->next(versionInfo), "expected working version info");
    versionInfo->release();
    versions->release();

    vc->commitEdits(lib, "aivivc svn no-op", true, false, NULL);
    std::cout << "aivivc svn regtest ok\n";
    return 0;
}
CPP

svnadmin create "$TMPDIR/repo"
svn checkout "file://$TMPDIR/repo" "$TMPDIR/wc" >/dev/null
mkdir -p "$TMPDIR/wc/myCell/schematic"
printf '%s\n%s\n' '-- Master.tag File, Rev:1.0' 'ViewType=schematic' \
    > "$TMPDIR/wc/myCell/schematic/master.tag"
printf 'oa-data\n' > "$TMPDIR/wc/myCell/schematic/sch.oa"
svn add "$TMPDIR/wc/myCell" >/dev/null
svn commit -m initial "$TMPDIR/wc" >/dev/null

cat > "$TMPDIR/svn-wrapper" <<'SH'
#!/bin/sh
exec svn "$@"
SH
chmod +x "$TMPDIR/svn-wrapper"

"$CXX" -std=c++17 -O0 -g \
    -I"$OA_HEADERS" -I"$OACPP/src/plugIn" -I"$OACPP/src/common" \
    -L"$CORE_LIBDIR" "$TMPDIR/test_aivivc_svn.cpp" \
    -loaPlugIn -loaCommon -ldl -o "$TMPDIR/test_aivivc_svn"

LD_LIBRARY_PATH="$AIVIVC_LIBDIR:$CORE_LIBDIR:${LD_LIBRARY_PATH:-}" \
OA_PLUGIN_PATH="$AIVIVC_ROOT/data/plugins:$OACPP/data/plugins" \
    "$TMPDIR/test_aivivc_svn" "$TMPDIR/wc" "$TMPDIR/svn-wrapper"
