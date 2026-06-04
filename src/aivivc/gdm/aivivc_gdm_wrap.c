#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AIVIVC_GDM_ADAPTER_PATH
#define AIVIVC_GDM_ADAPTER_PATH "./aivivc-gdm"
#endif

#define AIVIVC_GDM_WRAP_FUNC_COUNT 30
#define AIVIVC_GDM_MAX_ARGS 256
#define AIVIVC_GDM_STATUS_BUFFER 2048

typedef void (*AiviGdmSlot)(void);

typedef struct AiviGdmStatus {
    char display[64];
    char co[64];
    char perm[64];
    int modified;
    int deleted;
    int update;
    int managed;
    int is_dir;
} AiviGdmStatus;

static const char *aivivc_adapter_path(void)
{
    const char *path = getenv("AIVIVC_GDM_ADAPTER");
    return (path && *path) ? path : AIVIVC_GDM_ADAPTER_PATH;
}

static int aivivc_wait_status(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static int aivivc_run_adapter(const char *command, const char *cwd,
                              const char *const *extra_args)
{
    const char *adapter = aivivc_adapter_path();
    size_t extra_count = 0;
    char **argv;
    pid_t pid;
    int status = 0;

    if (!command || !*command) {
        command = "status";
    }

    while (extra_args && extra_args[extra_count]) {
        if (extra_count >= AIVIVC_GDM_MAX_ARGS) {
            fprintf(stderr, "gdmaivivc: too many adapter arguments\n");
            return 2;
        }
        ++extra_count;
    }

    argv = (char **)calloc(extra_count + 3, sizeof(char *));
    if (!argv) {
        fprintf(stderr, "gdmaivivc: calloc failed: %s\n", strerror(errno));
        return 2;
    }

    argv[0] = (char *)adapter;
    argv[1] = (char *)command;
    for (size_t i = 0; i < extra_count; ++i) {
        argv[i + 2] = (char *)extra_args[i];
    }

    pid = fork();
    if (pid == 0) {
        if (cwd && *cwd && chdir(cwd) != 0) {
            fprintf(stderr, "gdmaivivc: chdir(%s) failed: %s\n", cwd,
                    strerror(errno));
            _exit(127);
        }
        execv(adapter, argv);
        fprintf(stderr, "gdmaivivc: execv(%s) failed: %s\n", adapter,
                strerror(errno));
        _exit(127);
    }
    if (pid < 0) {
        fprintf(stderr, "gdmaivivc: fork failed: %s\n", strerror(errno));
        free(argv);
        return 2;
    }

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fprintf(stderr, "gdmaivivc: waitpid failed: %s\n", strerror(errno));
            free(argv);
            return 2;
        }
    }

    free(argv);
    return aivivc_wait_status(status);
}

static int aivivc_capture_adapter(const char *command, const char *cwd,
                                  const char *const *extra_args,
                                  char *buffer, size_t buffer_size)
{
    const char *adapter = aivivc_adapter_path();
    size_t extra_count = 0;
    char **argv;
    pid_t pid;
    int pipefd[2];
    int status = 0;
    size_t offset = 0;

    if (!buffer || buffer_size == 0) {
        return 2;
    }
    buffer[0] = '\0';

    if (!command || !*command) {
        command = "status";
    }
    while (extra_args && extra_args[extra_count]) {
        if (extra_count >= AIVIVC_GDM_MAX_ARGS) {
            return 2;
        }
        ++extra_count;
    }

    argv = (char **)calloc(extra_count + 3, sizeof(char *));
    if (!argv) {
        return 2;
    }
    argv[0] = (char *)adapter;
    argv[1] = (char *)command;
    for (size_t i = 0; i < extra_count; ++i) {
        argv[i + 2] = (char *)extra_args[i];
    }

    if (pipe(pipefd) != 0) {
        free(argv);
        return 2;
    }

    pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        if (cwd && *cwd && chdir(cwd) != 0) {
            _exit(127);
        }
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execv(adapter, argv);
        _exit(127);
    }
    close(pipefd[1]);
    if (pid < 0) {
        close(pipefd[0]);
        free(argv);
        return 2;
    }

    while (offset + 1 < buffer_size) {
        ssize_t got = read(pipefd[0], buffer + offset, buffer_size - offset - 1);
        if (got <= 0) {
            break;
        }
        offset += (size_t)got;
    }
    close(pipefd[0]);
    buffer[offset] = '\0';

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            free(argv);
            return 2;
        }
    }
    free(argv);
    return aivivc_wait_status(status);
}

static int aivivc_contains_token(const char *text, const char *token)
{
    size_t token_len;

    if (!text || !token) {
        return 0;
    }
    token_len = strlen(token);
    if (token_len == 0) {
        return 0;
    }

    for (const char *p = text; *p; ++p) {
        size_t i = 0;
        while (token[i] && p[i]) {
            char a = p[i];
            char b = token[i];
            if (a >= 'A' && a <= 'Z') {
                a = (char)(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = (char)(b - 'A' + 'a');
            }
            if (a != b) {
                break;
            }
            ++i;
        }
        if (i == token_len) {
            return 1;
        }
    }
    return 0;
}

static const char *aivivc_normalize_operation(const char *operation)
{
    if (!operation || !*operation) {
        return "status";
    }
    if (aivivc_contains_token(operation, "submit")) {
        return "submit";
    }
    if (aivivc_contains_token(operation, "ci") ||
        aivivc_contains_token(operation, "checkin")) {
        return "ci";
    }
    if (aivivc_contains_token(operation, "cancel") ||
        aivivc_contains_token(operation, "revert")) {
        return "cancel";
    }
    if (aivivc_contains_token(operation, "delete") ||
        aivivc_contains_token(operation, "remove")) {
        return "delete";
    }
    if (aivivc_contains_token(operation, "update")) {
        return "update";
    }
    if (aivivc_contains_token(operation, "co") ||
        aivivc_contains_token(operation, "checkout")) {
        return "co";
    }
    if (aivivc_contains_token(operation, "history") ||
        aivivc_contains_token(operation, "log")) {
        return "history";
    }
    return "status";
}

static char *aivivc_strdup_or_null(const char *value)
{
    char *copy = strdup(value ? value : "");
    if (!copy) {
        fprintf(stderr, "gdmaivivc: strdup failed: %s\n", strerror(errno));
    }
    return copy;
}

static const char *aivivc_gdm_co_status(const char *status)
{
    if (!status || !*status) {
        return NULL;
    }
    if (strcmp(status, "zCI") == 0 || strcmp(status, "CIN") == 0 ||
        strcmp(status, "zNOTMANAGED") == 0 ||
        strcmp(status, "UNMANAGEABLE") == 0) {
        return NULL;
    }
    if (strcmp(status, "zCO") == 0 || strcmp(status, "COUT") == 0) {
        return "CO";
    }
    if (strcmp(status, "zCO_OTHERS") == 0 || strcmp(status, "COTH") == 0) {
        return "COE";
    }
    return status;
}

static const char *aivivc_gdm_permission_status(const char *status)
{
    if (!status || !*status) {
        return "READ";
    }
    if (strcmp(status, "zREAD") == 0) {
        return "READ";
    }
    if (strcmp(status, "zWRITE") == 0) {
        return "WRITE";
    }
    return status;
}

static char *aivivc_dmServerGetWrapName(void)
{
    return aivivc_strdup_or_null("aivivc");
}

static int aivivc_dmVerifyServerVersion(void)
{
    return 1;
}

static void aivivc_dmExit(void)
{
}

static int aivivc_dmFileIsDirectory(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *aivivc_basename_ptr(const char *path)
{
    const char *base;

    if (!path || !*path) {
        return path;
    }

    base = strrchr(path, '/');
    if (!base) {
        return path;
    }
    return base[1] ? base + 1 : base;
}

static int aivivc_parse_bool_token(const char *text, const char *key)
{
    char pattern[64];
    const char *p;
    snprintf(pattern, sizeof(pattern), "%s=", key);
    p = strstr(text ? text : "", pattern);
    if (!p) {
        return 0;
    }
    p += strlen(pattern);
    return *p == '1';
}

static void aivivc_parse_string_token(const char *text, const char *key,
                                      char *out, size_t out_size)
{
    char pattern[64];
    const char *p;
    size_t i = 0;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';
    snprintf(pattern, sizeof(pattern), "%s=", key);
    p = strstr(text ? text : "", pattern);
    if (!p) {
        return;
    }
    p += strlen(pattern);
    while (*p && *p != '\n' && *p != ' ' && i + 1 < out_size) {
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static int aivivc_status_for_path(const char *cwd, const char *path,
                                  AiviGdmStatus *status)
{
    const char *args[3];
    char buffer[AIVIVC_GDM_STATUS_BUFFER];
    int rc;

    if (!path || !*path || !status) {
        return 0;
    }

    memset(status, 0, sizeof(*status));
    args[0] = "-file";
    args[1] = path;
    args[2] = NULL;
    rc = aivivc_capture_adapter("status-code", cwd, args, buffer, sizeof(buffer));
    if (rc != 0) {
        return 0;
    }

    aivivc_parse_string_token(buffer, "display", status->display, sizeof(status->display));
    aivivc_parse_string_token(buffer, "co", status->co, sizeof(status->co));
    aivivc_parse_string_token(buffer, "perm", status->perm, sizeof(status->perm));
    status->modified = aivivc_parse_bool_token(buffer, "modified");
    status->deleted = aivivc_parse_bool_token(buffer, "deleted");
    status->update = aivivc_parse_bool_token(buffer, "update");
    status->managed = aivivc_parse_bool_token(buffer, "managed");
    status->is_dir = aivivc_parse_bool_token(buffer, "dir");
    return 1;
}

static int aivivc_dmServerFileHasVersion(const char *login, const char *cwd,
                                         unsigned int flags, const char *xtra,
                                         const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)flags;
    (void)xtra;
    return aivivc_status_for_path(cwd, path, &status) && status.managed;
}

static void *aivivc_dmNullList(void)
{
    return NULL;
}

static void *aivivc_lookup_symbol(const char *name)
{
    static void *handle = NULL;
    if (!handle) {
        handle = dlopen(NULL, RTLD_NOW | RTLD_LOCAL);
    }
    if (!handle) {
        return NULL;
    }
    return dlsym(handle, name);
}

typedef void *(*AiviAllocFileInfoElemFn)(const char *, const char *, int);

static void *aivivc_dmServerGetStsAndDirExpand(const char *login, const char *cwd,
                                               unsigned int flags, const char *parent,
                                               const char *expand, const char *path)
{
    AiviAllocFileInfoElemFn alloc_fn =
        (AiviAllocFileInfoElemFn)aivivc_lookup_symbol("gdmIfsAllocFileInfoElem");
    AiviGdmStatus status;
    const char *name;
    int kind;
    (void)login;
    (void)flags;
    (void)parent;
    (void)expand;
    if (!alloc_fn || !aivivc_status_for_path(cwd, path, &status)) {
        return NULL;
    }
    name = aivivc_basename_ptr(path);
    kind = status.is_dir ? 1 : 0;
    return alloc_fn(name && *name ? name : ".", path, kind);
}

static void aivivc_dmBeginOperation(const char *login, const char *cwd,
                                    const char *operation,
                                    const char *const *paths)
{
    (void)login;
    (void)cwd;
    (void)operation;
    (void)paths;
}

static void aivivc_dmDoneOperation(const char *login, const char *cwd,
                                   const char *operation,
                                   const char *const *paths)
{
    (void)login;
    (void)cwd;
    (void)operation;
    (void)paths;
}

static char **aivivc_dmServerOperation(const char *login, const char *cwd,
                                       const char *operation,
                                       const char *const *args,
                                       int *exit_status)
{
    int rc;

    (void)login;
    rc = aivivc_run_adapter(aivivc_normalize_operation(operation), cwd, args);
    if (exit_status) {
        *exit_status = rc;
    }
    return NULL;
}

static int aivivc_dmServerIsManageable(const char *login, const char *cwd,
                                       unsigned int flags, const char *xtra,
                                       const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)flags;
    (void)xtra;
    return aivivc_status_for_path(cwd, path, &status) && status.managed;
}

static int aivivc_dmServerIsFileUnmanaged(const char *login, const char *cwd,
                                          unsigned int flags, const char *xtra,
                                          const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)flags;
    (void)xtra;
    return !aivivc_status_for_path(cwd, path, &status) || !status.managed;
}

static char *aivivc_dmServerGetCoFileStatus(const char *login, const char *kind,
                                            unsigned int flags, const char *cwd,
                                            const char *path)
{
    AiviGdmStatus status;
    const char *co_status;
    (void)login;
    (void)kind;
    (void)flags;
    if (!aivivc_status_for_path(cwd, path, &status)) {
        return NULL;
    }
    co_status = aivivc_gdm_co_status(status.co);
    return co_status ? aivivc_strdup_or_null(co_status) : NULL;
}

static char *aivivc_dmServerGetDelFileStatus(const char *login, const char *kind,
                                             unsigned int flags, const char *cwd,
                                             const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)kind;
    (void)flags;
    if (!aivivc_status_for_path(cwd, path, &status) || !status.deleted) {
        return NULL;
    }
    return aivivc_strdup_or_null("DEL");
}

static char *aivivc_dmServerGetModFileStatus(const char *login, const char *kind,
                                             unsigned int flags, const char *cwd,
                                             const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)kind;
    (void)flags;
    if (!aivivc_status_for_path(cwd, path, &status) || !status.modified) {
        return NULL;
    }
    return aivivc_strdup_or_null("MOD");
}

static char *aivivc_dmServerGetFilePermissionStatus(const char *login, const char *kind,
                                                    unsigned int flags, const char *cwd,
                                                    const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)kind;
    (void)flags;
    if (!aivivc_status_for_path(cwd, path, &status)) {
        return NULL;
    }
    return aivivc_strdup_or_null(aivivc_gdm_permission_status(status.perm));
}

static int aivivc_dmServerFileIsDirectory(const char *login, const char *kind,
                                          unsigned int flags, const char *cwd,
                                          const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)kind;
    (void)flags;
    return aivivc_status_for_path(cwd, path, &status) && status.is_dir;
}

static char *aivivc_dmServerHeadString(const char *login, const char *kind,
                                       unsigned int flags, const char *cwd,
                                       const char *path)
{
    (void)login;
    (void)kind;
    (void)flags;
    (void)cwd;
    (void)path;
    return aivivc_strdup_or_null("HEAD");
}

static char *aivivc_dmServerGetCiWhoUser(const char *login, const char *kind,
                                         unsigned int flags, const char *cwd,
                                         const char *path)
{
    (void)login;
    (void)kind;
    (void)flags;
    (void)cwd;
    (void)path;
    return aivivc_strdup_or_null("");
}

static int aivivc_dmServerGetUpdateNeededStatus(const char *login, const char *cwd,
                                                unsigned int flags, const char *parent,
                                                const char *xtra, const char *path)
{
    AiviGdmStatus status;
    (void)login;
    (void)flags;
    (void)parent;
    (void)xtra;
    return aivivc_status_for_path(cwd, path, &status) && status.update;
}

int aivivcGdmRunForTest(const char *command, int argc,
                        const char *const *argv)
{
    char **args;
    int rc;

    if (argc < 0) {
        return 2;
    }

    args = (char **)calloc((size_t)argc + 1, sizeof(char *));
    if (!args) {
        return 2;
    }
    for (int i = 0; i < argc; ++i) {
        args[i] = (char *)argv[i];
    }

    rc = aivivc_run_adapter(command, NULL, (const char *const *)args);
    free(args);
    return rc;
}

__attribute__((visibility("default")))
AiviGdmSlot gdmaivivcWrapFuncs[AIVIVC_GDM_WRAP_FUNC_COUNT] = {
    [0] = (AiviGdmSlot)aivivc_dmServerGetWrapName,
    [1] = (AiviGdmSlot)aivivc_dmVerifyServerVersion,
    [2] = (AiviGdmSlot)aivivc_dmExit,
    [3] = (AiviGdmSlot)aivivc_dmFileIsDirectory,
    [4] = (AiviGdmSlot)aivivc_dmServerFileHasVersion,
    [5] = (AiviGdmSlot)aivivc_dmBeginOperation,
    [6] = (AiviGdmSlot)aivivc_dmDoneOperation,
    [7] = (AiviGdmSlot)aivivc_dmServerOperation,
    [8] = (AiviGdmSlot)aivivc_dmServerGetStsAndDirExpand,
    [9] = (AiviGdmSlot)aivivc_dmNullList,
    [10] = (AiviGdmSlot)aivivc_dmNullList,
    [11] = (AiviGdmSlot)aivivc_dmNullList,
    [12] = (AiviGdmSlot)aivivc_dmNullList,
    [13] = (AiviGdmSlot)aivivc_dmServerIsManageable,
    [14] = (AiviGdmSlot)aivivc_dmServerIsFileUnmanaged,
    [15] = (AiviGdmSlot)aivivc_dmServerGetCoFileStatus,
    [16] = (AiviGdmSlot)aivivc_dmServerGetDelFileStatus,
    [17] = (AiviGdmSlot)aivivc_dmServerGetModFileStatus,
    [18] = (AiviGdmSlot)aivivc_dmServerGetFilePermissionStatus,
    [19] = (AiviGdmSlot)aivivc_dmServerFileIsDirectory,
    [20] = (AiviGdmSlot)aivivc_dmServerHeadString,
    [21] = (AiviGdmSlot)aivivc_dmServerHeadString,
    [22] = (AiviGdmSlot)aivivc_dmServerHeadString,
    [23] = (AiviGdmSlot)aivivc_dmNullList,
    [24] = (AiviGdmSlot)aivivc_dmServerGetCiWhoUser,
    [25] = (AiviGdmSlot)aivivc_dmNullList,
    [29] = (AiviGdmSlot)aivivc_dmServerGetUpdateNeededStatus,
};
