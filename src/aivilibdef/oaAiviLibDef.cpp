#include <oa/oaPlugInLibDefInterfaces.h>
#include <oa/oaString.h>
#include <oa/oaCommonPlugInBase.h>
#include <oa/oaCommonPlugInMgr.h>
#include <oa/oaCommonFactory.h>
#include <oa/oaDM.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits.h>
#include <map>
#include <pwd.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace oaAiviLibDef {

namespace {

static const char* const kDefaultNames[] = {
    "ai.lib",
    "lib.defs",
    "cds.lib"
};

static bool debugEnabled() {
    const char* value = std::getenv("AIVI_LIBDEF_DEBUG");
    return value && *value && std::strcmp(value, "0") != 0;
}

static void debugLog(const std::string& message) {
    if (debugEnabled()) {
        std::fprintf(stderr, "[oaAiviLibDef] %s\n", message.c_str());
    }
}

struct FileContext {
    std::string path;
    std::string canonicalPath;
    std::string dir;
};

struct ParseContext {
    oaPlugIn::ILibDefAccess* access = nullptr;
    std::set<std::string> activeFiles;
    std::map<std::string, oa::oaLibDef*> libsByName;
    std::map<std::string, bool> explicitWritePath;
    std::string allLibsTmpRoot;
};

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static bool iequals(const std::string& a, const std::string& b) {
    return toLower(a) == toLower(b);
}

static bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool isCommentBoundary(char c) {
    return c == '\0' || isSpace(c);
}

static std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static std::string stripComment(const std::string& line) {
    bool inSingleQuote = false;
    bool inDoubleQuote = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\'' && !inDoubleQuote) {
            inSingleQuote = !inSingleQuote;
            continue;
        }
        if (c == '"' && !inSingleQuote) {
            inDoubleQuote = !inDoubleQuote;
            continue;
        }
        if (inSingleQuote || inDoubleQuote) {
            continue;
        }

        char prev = i == 0 ? '\0' : line[i - 1];
        char next = i + 1 < line.size() ? line[i + 1] : '\0';

        if (c == '#' && (i == 0 || isCommentBoundary(prev)) &&
            (i == 0 || isCommentBoundary(next))) {
            return trim(line.substr(0, i));
        }

        if (c == '-' && i + 1 < line.size() && line[i + 1] == '-') {
            char after = i + 2 < line.size() ? line[i + 2] : '\0';
            if ((i == 0 || isCommentBoundary(prev)) &&
                (i == 0 || isCommentBoundary(after))) {
                return trim(line.substr(0, i));
            }
        }
    }

    return trim(line);
}

static bool tokenize(const std::string& line,
                     std::vector<std::string>& tokens,
                     std::string& error) {
    tokens.clear();

    for (size_t i = 0; i < line.size();) {
        while (i < line.size() && isSpace(line[i])) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }

        std::string token;
        if (line[i] == '\'' || line[i] == '"') {
            char quote = line[i++];
            bool closed = false;
            while (i < line.size()) {
                char c = line[i++];
                if (c == quote) {
                    closed = true;
                    break;
                }
                if (c == '\\' && i < line.size()) {
                    token += line[i++];
                } else {
                    token += c;
                }
            }
            if (!closed) {
                error = "unterminated quoted string";
                return false;
            }
        } else {
            while (i < line.size() && !isSpace(line[i])) {
                token += line[i++];
            }
        }
        tokens.push_back(token);
    }

    return true;
}

static std::string joinTokens(const std::vector<std::string>& tokens,
                              size_t first) {
    std::string value;
    for (size_t i = first; i < tokens.size(); ++i) {
        if (!value.empty()) {
            value += ' ';
        }
        value += tokens[i];
    }
    return value;
}

static bool isAbsolutePath(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

static std::vector<std::string> splitPathList(const char* value) {
    std::vector<std::string> out;
    if (!value) {
        return out;
    }

    std::string current;
    for (const char* p = value; *p; ++p) {
        if (*p == ':') {
            if (!current.empty()) {
                out.push_back(current);
            }
            current.clear();
        } else {
            current += *p;
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

static std::string dirnameOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) {
        return ".";
    }
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

static std::string basenameOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string lexicalNormalize(const std::string& path) {
    if (path.empty()) {
        return path;
    }

    bool absolute = isAbsolutePath(path);
    std::vector<std::string> parts;
    std::string part;

    for (size_t i = 0; i <= path.size(); ++i) {
        char c = i < path.size() ? path[i] : '/';
        if (c == '/') {
            if (part.empty() || part == ".") {
                part.clear();
            } else if (part == "..") {
                if (!parts.empty() && parts.back() != "..") {
                    parts.pop_back();
                } else if (!absolute) {
                    parts.push_back(part);
                }
                part.clear();
            } else {
                parts.push_back(part);
                part.clear();
            }
        } else {
            part += c;
        }
    }

    std::string result = absolute ? "/" : "";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0 || absolute) {
            if (result.size() > 1 || (!absolute && !result.empty())) {
                result += '/';
            }
        }
        result += parts[i];
    }

    if (result.empty()) {
        return absolute ? "/" : ".";
    }
    return result;
}

static std::string currentWorkingDirectory() {
    char buf[PATH_MAX];
    if (::getcwd(buf, sizeof(buf))) {
        return buf;
    }
    return ".";
}

static std::string makeAbsolute(const std::string& path,
                                const std::string& baseDir) {
    if (isAbsolutePath(path)) {
        return lexicalNormalize(path);
    }
    return lexicalNormalize(baseDir + "/" + path);
}

static bool pathExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

static bool dirExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static bool fileReadable(const std::string& path) {
    return ::access(path.c_str(), R_OK) == 0;
}

static std::string realPathIfPossible(const std::string& path) {
    char buf[PATH_MAX];
    if (::realpath(path.c_str(), buf)) {
        return buf;
    }
    return makeAbsolute(path, currentWorkingDirectory());
}

static FileContext makeFileContext(const std::string& path) {
    FileContext ctx;
    ctx.path = path;
    ctx.canonicalPath = realPathIfPossible(path);
    ctx.dir = dirnameOf(ctx.canonicalPath);
    return ctx;
}

static bool containsWildcard(const std::string& path) {
    return path.find('*') != std::string::npos ||
           path.find('?') != std::string::npos;
}

static void warn(ParseContext& ctx,
                 const oa::oaLibDefList* ldl,
                 const std::string& message) {
    if (ctx.access) {
        oa::oaString msg(message.c_str());
        ctx.access->warn(ldl, msg);
    }
}

static std::string findAncestorContaining(const std::string& start,
                                          const std::string& relPath) {
    std::string candidate = makeAbsolute(start, currentWorkingDirectory());
    for (;;) {
        std::string probe = relPath.empty() ? candidate
                                            : lexicalNormalize(candidate + "/" + relPath);
        if (pathExists(probe)) {
            return candidate;
        }

        std::string parent = dirnameOf(candidate);
        if (parent == candidate || candidate == "/") {
            break;
        }
        candidate = parent;
    }
    return "";
}

static std::string evalInstallRootExpression(const std::string& expr,
                                             const FileContext& fileCtx,
                                             bool& ok) {
    ok = true;

    const std::string computePrefix = "compute:";
    const std::string instRootPrefix = "inst_root_with:";

    if (expr.compare(0, computePrefix.size(), computePrefix) == 0) {
        std::string key = expr.substr(computePrefix.size());
        if (key == "THIS_FILE_INST_ROOT") {
            std::string root = findAncestorContaining(fileCtx.dir, "tools");
            return root.empty() ? fileCtx.dir : root;
        }
        if (key == "THIS_TOOL_INST_ROOT") {
            const char* envNames[] = {
                "CDS_INST_DIR",
                "CDSHOME",
                "OA_HOME",
                "OA_ROOT",
                "OPENACCESS_HOME"
            };
            for (const char* envName : envNames) {
                const char* value = std::getenv(envName);
                if (value && *value) {
                    return lexicalNormalize(value);
                }
            }
        }
        ok = false;
        return "";
    }

    if (expr.compare(0, instRootPrefix.size(), instRootPrefix) == 0) {
        std::string rel = expr.substr(instRootPrefix.size());

        std::vector<std::string> roots = splitPathList(std::getenv("CDS_STRM_DIR_LIST"));
        for (const std::string& root : roots) {
            std::string absRoot = makeAbsolute(root, currentWorkingDirectory());
            if (pathExists(lexicalNormalize(absRoot + "/" + rel))) {
                return absRoot;
            }
        }

        std::vector<std::string> pathDirs = splitPathList(std::getenv("PATH"));
        for (const std::string& pathDir : pathDirs) {
            std::string root = findAncestorContaining(pathDir, rel);
            if (!root.empty()) {
                return root;
            }
        }

        ok = false;
        return "";
    }

    ok = false;
    return "";
}

static std::string expandTilde(const std::string& input, bool& ok) {
    ok = true;
    if (input.empty() || input[0] != '~') {
        return input;
    }

    size_t slash = input.find('/');
    std::string user = slash == std::string::npos ? input.substr(1)
                                                   : input.substr(1, slash - 1);
    std::string rest = slash == std::string::npos ? "" : input.substr(slash);
    std::string home;

    if (user.empty()) {
        const char* value = std::getenv("HOME");
        if (value && *value) {
            home = value;
        } else {
            struct passwd* pw = ::getpwuid(::geteuid());
            if (pw && pw->pw_dir) {
                home = pw->pw_dir;
            }
        }
    } else {
        struct passwd* pw = ::getpwnam(user.c_str());
        if (pw && pw->pw_dir) {
            home = pw->pw_dir;
        }
    }

    if (home.empty()) {
        ok = false;
        return "";
    }
    return home + rest;
}

static std::string expandVariables(const std::string& input,
                                   const FileContext& fileCtx,
                                   bool& ok) {
    ok = true;
    std::string output;

    for (size_t i = 0; i < input.size();) {
        if (input[i] != '$') {
            output += input[i++];
            continue;
        }

        if (i + 1 >= input.size()) {
            output += input[i++];
            continue;
        }

        if (input[i + 1] == '(') {
            size_t close = input.find(')', i + 2);
            if (close == std::string::npos) {
                ok = false;
                return "";
            }
            std::string expr = input.substr(i + 2, close - i - 2);
            bool exprOk = true;
            std::string value = evalInstallRootExpression(expr, fileCtx, exprOk);
            if (!exprOk) {
                ok = false;
                return "";
            }
            output += value;
            i = close + 1;
            continue;
        }

        if (input[i + 1] == '{') {
            size_t close = input.find('}', i + 2);
            if (close == std::string::npos) {
                ok = false;
                return "";
            }
            std::string name = input.substr(i + 2, close - i - 2);
            const char* value = std::getenv(name.c_str());
            if (!value) {
                ok = false;
                return "";
            }
            output += value;
            i = close + 1;
            continue;
        }

        size_t start = i + 1;
        size_t end = start;
        while (end < input.size() &&
               (std::isalnum(static_cast<unsigned char>(input[end])) ||
                input[end] == '_')) {
            ++end;
        }

        if (end == start) {
            output += input[i++];
            continue;
        }

        std::string name = input.substr(start, end - start);
        const char* value = std::getenv(name.c_str());
        if (!value) {
            ok = false;
            return "";
        }
        output += value;
        i = end;
    }

    return output;
}

static std::string resolvePath(const std::string& rawPath,
                               const FileContext& fileCtx,
                               bool& ok) {
    bool tildeOk = true;
    std::string path = expandTilde(rawPath, tildeOk);
    if (!tildeOk) {
        ok = false;
        return "";
    }

    bool varOk = true;
    path = expandVariables(path, fileCtx, varOk);
    if (!varOk) {
        ok = false;
        return "";
    }

    ok = true;
    return makeAbsolute(path, fileCtx.dir);
}

static oa::oaLibModeEnum parseLibMode(const std::string& value,
                                      bool& ok) {
    if (iequals(value, "shared")) {
        ok = true;
        return oa::oacSharedLibMode;
    }
    if (iequals(value, "nonShared") || iequals(value, "nonshared")) {
        ok = true;
        return oa::oacNonSharedLibMode;
    }
    if (iequals(value, "readOnly") || iequals(value, "readonly")) {
        ok = true;
        return oa::oacReadOnlyLibMode;
    }
    ok = false;
    return oa::oacSharedLibMode;
}

static std::string libModeName(oa::oaLibMode mode) {
    oa::oaString name = mode.getName();
    return static_cast<const char*>(name);
}

static std::string tmpRootPathForLib(const std::string& tmpRoot,
                                     const std::string& libName) {
    if (tmpRoot.empty()) {
        return "";
    }
    return lexicalNormalize(tmpRoot + "/" + libName);
}

static oa::oaScalarName makeLibName(const std::string& libName) {
    return oa::oaScalarName(oa::oaUnixNS(), libName.c_str());
}

static std::string libNameToString(const oa::oaScalarName& name) {
    oa::oaString s;
    name.get(oa::oaUnixNS(), s);
    return static_cast<const char*>(s);
}

static void destroyExistingLibDef(ParseContext& ctx,
                                  const std::string& libName,
                                  oa::oaLibDefList* ldl) {
    auto it = ctx.libsByName.find(libName);
    if (it != ctx.libsByName.end() && it->second) {
        try {
            it->second->destroy();
        } catch (...) {
        }
        ctx.libsByName.erase(it);
    }

    try {
        oa::oaLibDef* existing = oa::oaLibDef::find(ldl, makeLibName(libName));
        if (existing) {
            existing->destroy();
        }
    } catch (...) {
    }
}

static oa::oaLibDef* createOrReplaceLibDef(ParseContext& ctx,
                                           oa::oaLibDefList* ldl,
                                           const std::string& libName,
                                           const std::string& libPath) {
    debugLog("DEFINE " + libName + " " + libPath);
    destroyExistingLibDef(ctx, libName, ldl);

    std::string defaultWritePath = tmpRootPathForLib(ctx.allLibsTmpRoot, libName);
    oa::oaString oaPath(libPath.c_str());
    oa::oaString oaWritePath(defaultWritePath.c_str());
    oa::oaLibMode mode(oa::oacSharedLibMode);

    oa::oaLibDef* libDef = nullptr;
    try {
        libDef = oa::oaLibDef::create(ldl, makeLibName(libName), oaPath,
                                      oaWritePath, mode, nullptr);
    } catch (...) {
        return nullptr;
    }

    ctx.libsByName[libName] = libDef;
    ctx.explicitWritePath[libName] = false;
    debugLog("DEFINE done " + libName);
    return libDef;
}

static void applyAllLibsTmpRoot(ParseContext& ctx) {
    for (const auto& item : ctx.libsByName) {
        const std::string& libName = item.first;
        oa::oaLibDef* libDef = item.second;
        if (!libDef || ctx.explicitWritePath[libName]) {
            continue;
        }
        std::string writePath = tmpRootPathForLib(ctx.allLibsTmpRoot, libName);
        try {
            libDef->setLibWritePath(oa::oaString(writePath.c_str()));
        } catch (...) {
        }
    }
}

static void assignLib(ParseContext& ctx,
                      oa::oaLibDefList* ldl,
                      const FileContext& fileCtx,
                      int lineNum,
                      const std::string& libName,
                      const std::string& attrName,
                      const std::string& rawValue) {
    if (iequals(libName, "AllLibs") && iequals(attrName, "TmpRootDir")) {
        debugLog("ASSIGN AllLibs TmpRootDir " + rawValue);
        bool pathOk = true;
        std::string path = resolvePath(rawValue, fileCtx, pathOk);
        if (!pathOk) {
            warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                           ": invalid TmpRootDir path: " + rawValue);
            return;
        }
        ctx.allLibsTmpRoot = path;
        applyAllLibsTmpRoot(ctx);
        return;
    }

    auto it = ctx.libsByName.find(libName);
    if (it == ctx.libsByName.end() || !it->second) {
        warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                       ": ASSIGN references undefined library: " + libName);
        return;
    }

    oa::oaLibDef* libDef = it->second;
    debugLog("ASSIGN " + libName + " " + attrName + " " + rawValue);

    if (iequals(attrName, "writePath") || iequals(attrName, "TMP")) {
        bool pathOk = true;
        std::string path = resolvePath(rawValue, fileCtx, pathOk);
        if (!pathOk) {
            warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                           ": invalid writePath: " + rawValue);
            return;
        }
        try {
            libDef->setLibWritePath(oa::oaString(path.c_str()));
            ctx.explicitWritePath[libName] = true;
        } catch (...) {
        }
        return;
    }

    if (iequals(attrName, "libMode")) {
        bool modeOk = true;
        oa::oaLibModeEnum mode = parseLibMode(rawValue, modeOk);
        if (!modeOk) {
            warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                           ": invalid libMode: " + rawValue);
            return;
        }
        try {
            libDef->setLibMode(oa::oaLibMode(mode));
        } catch (...) {
        }
        return;
    }

    try {
        libDef->addLibAttribute(oa::oaString(attrName.c_str()),
                                oa::oaString(rawValue.c_str()));
    } catch (...) {
    }
}

static void unassignLib(ParseContext& ctx,
                        oa::oaLibDefList* ldl,
                        const FileContext& fileCtx,
                        int lineNum,
                        const std::string& libName,
                        const std::string& attrName) {
    if (iequals(libName, "AllLibs") && iequals(attrName, "TmpRootDir")) {
        warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                       ": UNASSIGN AllLibs TmpRootDir is not supported");
        return;
    }

    auto it = ctx.libsByName.find(libName);
    if (it == ctx.libsByName.end() || !it->second) {
        warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                       ": UNASSIGN references undefined library: " + libName);
        return;
    }

    oa::oaLibDef* libDef = it->second;
    debugLog("UNASSIGN " + libName + " " + attrName);

    if (iequals(attrName, "writePath") || iequals(attrName, "TMP")) {
        std::string writePath = tmpRootPathForLib(ctx.allLibsTmpRoot, libName);
        try {
            libDef->setLibWritePath(oa::oaString(writePath.c_str()));
            ctx.explicitWritePath[libName] = false;
        } catch (...) {
        }
        return;
    }

    try {
        libDef->removeLibAttribute(oa::oaString(attrName.c_str()));
    } catch (...) {
    }
}

static bool loadLibDefList(ParseContext& ctx,
                           oa::oaLibDefList* ldl,
                           const std::string& filePath,
                           oa::oaBoolean openReferences);

static void includeFile(ParseContext& ctx,
                        oa::oaLibDefList* ldl,
                        const FileContext& fileCtx,
                        int lineNum,
                        const std::string& includePath,
                        bool soft,
                        oa::oaBoolean openReferences) {
    bool pathOk = true;
    std::string resolved = resolvePath(includePath, fileCtx, pathOk);
    debugLog(std::string(soft ? "SOFTINCLUDE " : "INCLUDE ") + includePath +
             " -> " + resolved);
    if (!pathOk || containsWildcard(resolved)) {
        if (!soft) {
            warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                           ": invalid INCLUDE path: " + includePath);
        }
        return;
    }

    if (!fileReadable(resolved)) {
        if (!soft) {
            warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                           ": cannot read INCLUDE file: " + resolved);
        }
        return;
    }

    debugLog("inline INCLUDE " + resolved);
    loadLibDefList(ctx, ldl, resolved, openReferences);
}

static bool loadLibDefList(ParseContext& ctx,
                           oa::oaLibDefList* ldl,
                           const std::string& filePath,
                           oa::oaBoolean openReferences) {
    FileContext fileCtx = makeFileContext(filePath);
    debugLog("load file " + filePath + " canonical " + fileCtx.canonicalPath);

    if (ctx.activeFiles.count(fileCtx.canonicalPath)) {
        warn(ctx, ldl, "recursive INCLUDE detected: " + fileCtx.path);
        return false;
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        warn(ctx, ldl, "cannot open library definition file: " + filePath);
        return false;
    }

    ctx.activeFiles.insert(fileCtx.canonicalPath);

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        ++lineNum;
        std::string stripped = stripComment(line);
        if (stripped.empty()) {
            continue;
        }

        std::vector<std::string> tokens;
        std::string tokenError;
        if (!tokenize(stripped, tokens, tokenError)) {
            warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                           ": " + tokenError);
            continue;
        }
        if (tokens.empty()) {
            continue;
        }

        const std::string keyword = tokens[0];
        debugLog(fileCtx.path + ":" + std::to_string(lineNum) + " " + keyword);

        if (iequals(keyword, "DEFINE") || iequals(keyword, "SOFTDEFINE")) {
            bool soft = iequals(keyword, "SOFTDEFINE");
            if (tokens.size() < 3) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": invalid " + keyword + " syntax");
                continue;
            }

            bool pathOk = true;
            std::string libPath = resolvePath(tokens[2], fileCtx, pathOk);
            if (!pathOk || containsWildcard(libPath)) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": invalid library path: " + tokens[2]);
                continue;
            }

            oa::oaLibDef* libDef = createOrReplaceLibDef(ctx, ldl, tokens[1], libPath);
            if (!libDef) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": could not create library definition: " + tokens[1]);
                continue;
            }

            if (!soft && !dirExists(libPath)) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": library path does not exist: " + libPath);
            }
            continue;
        }

        if (iequals(keyword, "UNDEFINE")) {
            if (tokens.size() < 2) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": invalid UNDEFINE syntax");
                continue;
            }
            destroyExistingLibDef(ctx, tokens[1], ldl);
            ctx.explicitWritePath.erase(tokens[1]);
            debugLog("UNDEFINE " + tokens[1]);
            continue;
        }

        if (iequals(keyword, "INCLUDE") || iequals(keyword, "SOFTINCLUDE")) {
            if (tokens.size() < 2) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": invalid " + keyword + " syntax");
                continue;
            }
            includeFile(ctx, ldl, fileCtx, lineNum, tokens[1],
                        iequals(keyword, "SOFTINCLUDE"), openReferences);
            continue;
        }

        if (iequals(keyword, "ASSIGN")) {
            if (tokens.size() < 4) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": invalid ASSIGN syntax");
                continue;
            }
            assignLib(ctx, ldl, fileCtx, lineNum, tokens[1], tokens[2],
                      joinTokens(tokens, 3));
            continue;
        }

        if (iequals(keyword, "UNASSIGN")) {
            if (tokens.size() < 3) {
                warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                               ": invalid UNASSIGN syntax");
                continue;
            }
            unassignLib(ctx, ldl, fileCtx, lineNum, tokens[1], tokens[2]);
            continue;
        }

        warn(ctx, ldl, fileCtx.path + ":" + std::to_string(lineNum) +
                       ": unknown keyword: " + keyword);
    }

    ctx.activeFiles.erase(fileCtx.canonicalPath);
    debugLog("done file " + filePath);
    return true;
}

static std::vector<std::string> defaultSearchDirs() {
    std::vector<std::string> dirs;
    dirs.push_back(".");

    const char* home = std::getenv("HOME");
    if (home && *home) {
        dirs.push_back(home);
    } else {
        struct passwd* pw = ::getpwuid(::geteuid());
        if (pw && pw->pw_dir) {
            dirs.push_back(pw->pw_dir);
        }
    }

    const char* envNames[] = {
        "OA_DATA_DIR",
        "OA_HOME",
        "OA_ROOT",
        "OPENACCESS_HOME"
    };
    for (const char* envName : envNames) {
        const char* value = std::getenv(envName);
        if (!value || !*value) {
            continue;
        }
        std::string root = lexicalNormalize(value);
        if (envName == std::string("OA_DATA_DIR")) {
            dirs.push_back(root + "/libraries");
        } else {
            dirs.push_back(root + "/data/libraries");
        }
    }

    return dirs;
}

static std::string findDefaultPath() {
    for (const std::string& dir : defaultSearchDirs()) {
        for (const char* name : kDefaultNames) {
            std::string path = dir == "." ? std::string(name)
                                          : lexicalNormalize(dir + "/" + name);
            if (fileReadable(path)) {
                return path;
            }
        }
    }
    return "";
}

static int writeLibDef(const oa::oaString& fp,
                       const oa::oaLibDefList* ldl) {
    if (!ldl) {
        return 0;
    }

    std::ofstream file(static_cast<const char*>(fp));
    if (!file.is_open()) {
        return 0;
    }

    int count = 0;
    oa::oaCollection<oa::oaLibDefListMem, oa::oaLibDefList> members =
        ldl->getMembers();
    oa::oaIter<oa::oaLibDefListMem> iter(members);

    while (oa::oaLibDefListMem* mem = iter.getNext()) {
        if (mem->getType() == oa::oacLibDefType) {
            oa::oaLibDef* libDef = static_cast<oa::oaLibDef*>(mem);

            oa::oaScalarName libName;
            libDef->getLibName(libName);
            std::string name = libNameToString(libName);

            oa::oaString libPath;
            libDef->getLibPath(libPath);
            file << "DEFINE " << name << " "
                 << static_cast<const char*>(libPath) << "\n";

            oa::oaString writePath;
            libDef->getLibWritePath(writePath);
            if (writePath != libPath) {
                file << "ASSIGN " << name << " writePath "
                     << static_cast<const char*>(writePath) << "\n";
            }

            file << "ASSIGN " << name << " libMode "
                 << libModeName(libDef->getLibMode()) << "\n";

            oa::oaDMAttrArray attrs;
            libDef->getLibAttributes(attrs);
            for (oa::oaUInt4 i = 0; i < attrs.getNumElements(); ++i) {
                file << "ASSIGN " << name << " "
                     << static_cast<const char*>(attrs[i].getName()) << " "
                     << static_cast<const char*>(attrs[i].getValue()) << "\n";
            }

            ++count;
        } else if (mem->getType() == oa::oacLibDefListRefType) {
            oa::oaLibDefListRef* ref = static_cast<oa::oaLibDefListRef*>(mem);
            oa::oaString refPath;
            ref->getRefListPath(refPath);
            file << "INCLUDE " << static_cast<const char*>(refPath) << "\n";
            ++count;
        }
    }

    return count;
}

} // namespace

class AiviLibDef : public oaPlugIn::ILibDef {
public:
    unsigned long addRef() override { return ++ref_; }

    unsigned long release() override {
        unsigned long r = --ref_;
        if (!r) {
            delete this;
        }
        return r;
    }

    unsigned long getRefCount() override { return ref_; }

    long queryInterface(const oaCommon::Guid& id, void** iPtr) override {
        if (!iPtr) {
            return oaCommon::IBase::cFail;
        }
        if (std::memcmp(&id, &oaCommon::IID_IBase, sizeof(oaCommon::Guid)) == 0 ||
            std::memcmp(&id, &oaPlugIn::IID_ILibDef, sizeof(oaCommon::Guid)) == 0) {
            *iPtr = static_cast<oaPlugIn::ILibDef*>(this);
            addRef();
            return oaCommon::IBase::cOK;
        }
        *iPtr = nullptr;
        return oaCommon::IBase::cNoInterface;
    }

    void init(oaPlugIn::ILibDefAccess* access) override {
        access_ = access;
    }

    oa::oaLibDefList* open() override {
        std::string path = findDefaultPath();
        if (path.empty()) {
            return nullptr;
        }
        return open(oa::oaString(path.c_str()), true);
    }

    oa::oaLibDefList* open(const oa::oaString& fp, oa::oaBoolean openReferences) override {
        oa::oaLibDefList* ldl = nullptr;
        try {
            ldl = oa::oaLibDefList::create(fp);
        } catch (...) {
            ldl = oa::oaLibDefList::find(fp);
        }
        if (!ldl) {
            return nullptr;
        }

        ParseContext ctx;
        ctx.access = access_;
        loadLibDefList(ctx, ldl, static_cast<const char*>(fp), openReferences);
        return ldl;
    }

    void save(const oa::oaLibDefList* ldl) override {
        if (!ldl) {
            return;
        }

        oa::oaString path;
        ldl->getPath(path);
        if (path.isEmpty()) {
            return;
        }
        writeLibDef(path, ldl);
    }

    void saveAs(const oa::oaLibDefList* ldl, const oa::oaString& newPath) override {
        if (!ldl || newPath.isEmpty()) {
            return;
        }
        writeLibDef(newPath, ldl);
    }

    void getDefaultPath(oa::oaString& p) override {
        std::string path = findDefaultPath();
        p = path.c_str();
    }

    void getDefaultFileName(oa::oaString& n) override {
        std::string path = findDefaultPath();
        std::string name = path.empty() ? "ai.lib" : basenameOf(path);
        n = name.c_str();
    }

    oa::oaBoolean hasPath() const override { return false; }
    oa::oaBoolean hasWritePath() const override { return false; }

    void getLibPath(const oa::oaLibDef*, oa::oaString& p) override {
        p = "";
    }

    void getLibWritePath(const oa::oaLibDef*, oa::oaString& p) override {
        p = "";
    }

    static class Factory : public oaCommon::IFactory {
    public:
        oa::oaUInt4 createInstance(oaCommon::IBase*, const oaCommon::Guid& id,
                                   void** i) override {
            AiviLibDef* c = new AiviLibDef();
            oa::oaUInt4 r = c->queryInterface(id, i);
            if (r != 0) {
                delete c;
                *i = nullptr;
            }
            return r;
        }

        unsigned long addRef() override { return 1; }
        unsigned long release() override { return 1; }
        unsigned long getRefCount() override { return 1; }

        long queryInterface(const oaCommon::Guid&, void** iPtr) override {
            if (!iPtr) {
                return oaCommon::IBase::cFail;
            }
            *iPtr = static_cast<oaCommon::IFactory*>(this);
            addRef();
            return oaCommon::IBase::cOK;
        }
    } factory;

private:
    unsigned long ref_ = 0;
    oaPlugIn::ILibDefAccess* access_ = nullptr;
};

AiviLibDef::Factory AiviLibDef::factory;

} // namespace oaAiviLibDef

extern "C" __attribute__((constructor)) void oaAiviLibDefInit() {
    oaCommon::oaPlugInMgr::registerFactory("oaAiviLibDef",
        &oaAiviLibDef::AiviLibDef::factory);
    oaCommon::oaPlugInMgr::registerFactory("oaAiviLibDefSystem",
        &oaAiviLibDef::AiviLibDef::factory);
    oaCommon::oaPlugInMgr::registerFactory("oaLibDef",
        &oaAiviLibDef::AiviLibDef::factory);
    oaCommon::oaPlugInMgr::registerFactory("oaLibDefSystem",
        &oaAiviLibDef::AiviLibDef::factory);
}

extern "C" long getClassObject(const char* cid,
                               const oaCommon::Guid& iid,
                               void** inst) {
    if (!cid || !inst) {
        return oaCommon::IBase::cFail;
    }
    if (std::strcmp(cid, "oaAiviLibDef") &&
        std::strcmp(cid, "oaAiviLibDefSystem") &&
        std::strcmp(cid, "oaLibDef") &&
        std::strcmp(cid, "oaLibDefSystem")) {
        return oaCommon::IBase::cFail;
    }
    return oaAiviLibDef::AiviLibDef::factory.queryInterface(iid, inst);
}
