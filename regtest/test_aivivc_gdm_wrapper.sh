#!/bin/bash
# Regression for the native AIVIVC GDM wrapper entry table.
set -euo pipefail

AIVIVC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OACPP="${OACPP:-/workarea/ai/openclaw/oacpp}"
SYSNAME="${SYSNAME:-$("$AIVIVC_ROOT/build/bin/sysname")_64}"
OPTMODE="${OPTMODE:-opt}"
CC="${CC:-gcc}"
WRAPPER="$AIVIVC_ROOT/src/lib/aivivc/gdm/$SYSNAME/$OPTMODE/libgdmaivivc_sh.so"
TMPDIR="$(mktemp -d /tmp/aivivc-gdm-wrapper-regtest.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

cat > "$TMPDIR/git-wrapper" <<'SH'
#!/bin/sh
exec git "$@"
SH
chmod +x "$TMPDIR/git-wrapper"
export AIVIVC_GIT="$TMPDIR/git-wrapper"

if [ ! -f "$WRAPPER" ]; then
    OACPP="$OACPP" make -C "$AIVIVC_ROOT/src/aivivc" OPTMODE="$OPTMODE"
fi

[ -f "$WRAPPER" ] || fail "missing wrapper: $WRAPPER"

nm -D --defined-only "$WRAPPER" | grep -F "gdmaivivcWrapFuncs" >/dev/null ||
    fail "missing gdmaivivcWrapFuncs export"
readelf -Ws "$WRAPPER" |
    awk '$8 == "gdmaivivcWrapFuncs" && $3 == 240 && $4 == "OBJECT" { found = 1 }
         END { exit found ? 0 : 1 }' ||
    fail "gdmaivivcWrapFuncs is not a 240-byte object"

mkdir -p "$TMPDIR/repo/libA/cell1/schematic"
printf 'DEFINE libA libA\n' > "$TMPDIR/repo/cds.lib"
printf '%s\n%s\n' '-- Master.tag File, Rev:1.0' 'ViewType=schematic' \
    > "$TMPDIR/repo/libA/cell1/schematic/master.tag"
printf 'oa-data\n' > "$TMPDIR/repo/libA/cell1/schematic/sch.oa"

git init "$TMPDIR/repo" >/dev/null
git -C "$TMPDIR/repo" config user.email aivivc@example.invalid
git -C "$TMPDIR/repo" config user.name aivivc
git -C "$TMPDIR/repo" config commit.gpgsign false
git -C "$TMPDIR/repo" add .
git -C "$TMPDIR/repo" commit -m initial >/dev/null

cat > "$TMPDIR/test_aivivc_gdm_wrapper.c" <<'C'
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef const char *(*get_name_fn)(void);
typedef int (*verify_fn)(void);
typedef int (*run_fn)(const char *, int, const char *const *);
typedef char **(*operation_fn)(const char *, const char *, const char *,
                               const char *const *, int *);
typedef char *(*status_fn)(const char *, const char *, unsigned int,
                           const char *, const char *);
typedef int (*update_needed_fn)(const char *, const char *, unsigned int,
                                const char *, const char *, const char *);

static void require(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    void *handle;
    void **table;
    get_name_fn get_name;
    verify_fn verify;
    run_fn run;
    operation_fn operation;
    status_fn co_status_fn;
    status_fn perm_status_fn;
    update_needed_fn update_needed_fn_ptr;
    const char *status_args[3];
    const char *co_args[3];
    const char *op_args[5];
    int exit_status = -1;
    char *co_status;
    char *perm_status;

    require(argc == 4, "usage: test WRAPPER REPO FILE");

    handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(handle != NULL, dlerror());

    table = (void **)dlsym(handle, "gdmaivivcWrapFuncs");
    require(table != NULL, "missing gdmaivivcWrapFuncs");
    for (int i = 0; i <= 25; ++i) {
        require(i >= 26 || table[i] != NULL, "mandatory wrapper slot is NULL");
    }
    require(table[29] != NULL, "update-needed slot is NULL");

    get_name = (get_name_fn)table[0];
    verify = (verify_fn)table[1];
    operation = (operation_fn)table[7];
    require(strcmp(get_name(), "aivivc") == 0, "unexpected wrapper name");
    require(verify() != 0, "server version verification failed");

    run = (run_fn)dlsym(handle, "aivivcGdmRunForTest");
    require(run != NULL, "missing aivivcGdmRunForTest");

    status_args[0] = "-file";
    status_args[1] = argv[2];
    status_args[2] = NULL;
    require(run("status", 2, status_args) == 0, "status adapter call failed");

    co_args[0] = "-file";
    co_args[1] = argv[3];
    co_args[2] = NULL;
    require(run("co", 2, co_args) == 0, "checkout adapter call failed");

    co_status_fn = (status_fn)table[15];
    perm_status_fn = (status_fn)table[18];
    update_needed_fn_ptr = (update_needed_fn)table[29];

    co_status = co_status_fn("aivivc", "status", 0, argv[2], argv[3]);
    require(co_status != NULL && strcmp(co_status, "CO") == 0,
            "checkout status did not return CO");
    free(co_status);

    perm_status = perm_status_fn("aivivc", "status", 0, argv[2], argv[3]);
    require(perm_status != NULL && strcmp(perm_status, "WRITE") == 0,
            "permission status did not return WRITE");
    free(perm_status);

    require(update_needed_fn_ptr("aivivc", argv[2], 0, NULL, NULL, argv[3]) == 0,
            "update-needed status should be false in local test repo");

    op_args[0] = "-message";
    op_args[1] = "native wrapper update";
    op_args[2] = "-file";
    op_args[3] = argv[3];
    op_args[4] = NULL;
    operation("aivivc", argv[2], "submit", op_args, &exit_status);
    require(exit_status == 0, "operation slot submit failed");

    dlclose(handle);
    return 0;
}
C

"$CC" -std=c99 -O0 -g "$TMPDIR/test_aivivc_gdm_wrapper.c" -ldl \
    -o "$TMPDIR/test_aivivc_gdm_wrapper"

printf 'changed\n' >> "$TMPDIR/repo/libA/cell1/schematic/sch.oa"
AIVIVC_GDM_ADAPTER="$AIVIVC_ROOT/src/aivivc/gdm/aivivc-gdm" \
    "$TMPDIR/test_aivivc_gdm_wrapper" "$WRAPPER" "$TMPDIR/repo" \
    "$TMPDIR/repo/libA/cell1/schematic/sch.oa"

git -C "$TMPDIR/repo" log --oneline -1 | grep -F "native wrapper update" >/dev/null ||
    fail "native wrapper operation did not create the expected commit"
[ -z "$(git -C "$TMPDIR/repo" status --porcelain -- libA/cell1/schematic/sch.oa)" ] ||
    fail "native wrapper operation left tracked file dirty"

echo "aivivc gdm native wrapper regtest ok"
