#include <oa/oaDM.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace oa;
using namespace std;

#define LOG(msg) cerr << msg << endl

static void require(bool expr, const string& msg) {
    if (!expr) {
        throw runtime_error(msg);
    }
}

static void writeFile(const char* path, const string& body) {
    ofstream f(path);
    require(f.is_open(), string("cannot write ") + path);
    f << body;
}

static void makeDir(const char* path) {
    if (mkdir(path, 0775) != 0) {
        struct stat st;
        require(stat(path, &st) == 0 && S_ISDIR(st.st_mode),
                string("cannot mkdir ") + path);
    }
}

static string oaStr(const oaString& s) {
    return static_cast<const char*>(s);
}

static string scalarToString(const oaScalarName& name) {
    oaString s;
    name.get(oaUnixNS(), s);
    return oaStr(s);
}

static oaLibDef* findLib(oaLibDefList* ldl, const char* name) {
    oaScalarName scalar(oaUnixNS(), name);
    return oaLibDef::find(ldl, scalar);
}

static void removeIfExists(const char* path) {
    unlink(path);
}

static void cleanupFiles() {
    removeIfExists("ai.lib");
    removeIfExists("lib.defs");
    removeIfExists("cds.lib");
    removeIfExists("roundtrip.lib");
}

int main() {
    try {
        LOG("=== Test AIVI libdef plugin ===");

        oaDMInit(oacAPIMajorRevNumber, oacAPIMinorRevNumber,
                 oacDataModelRevNumber);

        cleanupFiles();

        LOG("1. Default filename priority");
        writeFile("cds.lib", "DEFINE cdsOnly ./cdsOnly\n");
        oaString defaultPath;
        oaLibDefList::getDefaultPath(defaultPath);
        require(oaStr(defaultPath) == "cds.lib", "cds.lib fallback failed");

        writeFile("lib.defs", "DEFINE defsOnly ./defsOnly\n");
        oaLibDefList::getDefaultPath(defaultPath);
        require(oaStr(defaultPath) == "lib.defs", "lib.defs priority failed");

        writeFile("ai.lib", "DEFINE aiOnly ./aiOnly\n");
        oaLibDefList::getDefaultPath(defaultPath);
        require(oaStr(defaultPath) == "ai.lib", "ai.lib priority failed");
        cleanupFiles();

        LOG("2. openLibs with cds.lib statements");
        makeDir("child");

        writeFile("child/cds.lib",
                  "DEFINE childLib ./childLibDir\n"
                  "ASSIGN childLib DISPLAY ChildDisplay\n");

        writeFile("lib.defs",
                  "-- top-level comment\n"
                  "SOFTINCLUDE missing.lib\n"
                  "INCLUDE child/cds.lib\n"
                  "DEFINE removedLib ./removed\n"
                  "UNDEFINE removedLib\n"
                  "SOFTDEFINE softMissing ./missingSoftDir\n"
                  "DEFINE finalLib ./finalLibDir\n"
                  "ASSIGN AllLibs TmpRootDir ./tmp\n"
                  "ASSIGN finalLib writePath ./tmp/finalLib\n"
                  "ASSIGN finalLib libMode readOnly\n"
                  "ASSIGN finalLib DISPLAY RefLibs\n"
                  "UNASSIGN finalLib DISPLAY\n"
                  "ASSIGN childLib TMP ./tmp/childLib\n");

        oaLibDefList::openLibs();
        oaLibDefList* top = oaLibDefList::getTopList();
        require(top && top->isValid(), "top list is invalid");
        require(top->getMembers().getCount() == 3,
                "top list should contain childLib, softMissing, finalLib");
        require(oaLibDefList::getLibDefLists().getCount() == 1,
                "INCLUDE should be flattened into the top LibDefList");
        require(!findLib(top, "removedLib"), "UNDEFINE failed");
        require(findLib(top, "softMissing"), "SOFTDEFINE failed");

        oaLibDef* finalLib = findLib(top, "finalLib");
        require(finalLib, "finalLib missing");
        require(finalLib->getLibMode() == oacReadOnlyLibMode,
                "libMode assignment failed");
        oaString finalWritePath;
        finalLib->getLibWritePath(finalWritePath);
        require(oaStr(finalWritePath).find("/tmp/finalLib") != string::npos,
                "writePath assignment failed");
        oaDMAttrArray finalAttrs;
        finalLib->getLibAttributes(finalAttrs);
        for (oaUInt4 i = 0; i < finalAttrs.getNumElements(); ++i) {
            require(oaStr(finalAttrs[i].getName()) != "DISPLAY",
                    "UNASSIGN DISPLAY failed");
        }

        oaLibDef* childLib = findLib(top, "childLib");
        require(childLib, "childLib missing");
        oaString childWritePath;
        childLib->getLibWritePath(childWritePath);
        require(oaStr(childWritePath).find("/tmp/childLib") != string::npos,
                "cross-file ASSIGN TMP failed");

        LOG("3. saveAs preserves included definitions and assignments");
        top->saveAs("roundtrip.lib");
        ifstream roundtrip("roundtrip.lib");
        require(roundtrip.is_open(), "roundtrip.lib not written");
        string body((istreambuf_iterator<char>(roundtrip)),
                    istreambuf_iterator<char>());
        require(body.find("DEFINE childLib ") != string::npos,
                "included DEFINE not written");
        require(body.find("DEFINE finalLib ") != string::npos,
                "DEFINE not written");
        require(body.find("ASSIGN finalLib libMode readOnly") != string::npos,
                "libMode not written");

        cleanupFiles();
        LOG("=== DONE ===");
        return 0;
    } catch (oaException& e) {
        LOG(string("OA ERROR: ") + static_cast<const char*>(e.getMsg()));
        return 1;
    } catch (exception& e) {
        LOG(string("ERROR: ") + e.what());
        return 1;
    }
}
