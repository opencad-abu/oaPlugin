#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: ${0##*/} [--debug] [--no-build] [--keep-tmp]

Build and smoke-test the AIVI oaLibDef plugin with the local OpenAccess SOs.

Environment overrides:
  CXX         C++ compiler, default: g++
  SYSNAME     OA platform dir, default: linux_rhel90_64
  OPTMODE     OA build mode dir, default: opt
  OACPP_ROOT  OA runtime root, default: ../oacpp
  OA_INC      OA include dir, default: ../oa22.61-devel/include

Options:
  --debug     Export AIVI_LIBDEF_DEBUG=1 while running the smoke test
  --no-build  Reuse the existing plugin .so and test binary
  --keep-tmp  Do not remove the temporary plugin/work directories
  -h, --help  Show this help
EOF
}

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

log() {
    printf '\n[%s] %s\n' "$(date +%H:%M:%S)" "$*"
}

run() {
    printf '+'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

require_file() {
    [[ -f "$1" ]] || die "missing file: $1"
}

require_dir() {
    [[ -d "$1" ]] || die "missing directory: $1"
}

DEBUG=0
NO_BUILD=0
KEEP_TMP=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)
            DEBUG=1
            shift
            ;;
        --no-build)
            NO_BUILD=1
            shift
            ;;
        --keep-tmp)
            KEEP_TMP=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "unknown option: $1"
            ;;
    esac
done

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OAPLG_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)

CXX=${CXX:-g++}
SYSNAME=${SYSNAME:-linux_rhel90_64}
OPTMODE=${OPTMODE:-opt}
OACPP_ROOT=${OACPP_ROOT:-"$OAPLG_ROOT/../oacpp"}
OA_INC=${OA_INC:-"$OAPLG_ROOT/../oa22.61-devel/include"}

OA_LIB="$OACPP_ROOT/lib/$SYSNAME/$OPTMODE"
AIVI_LIB="$OAPLG_ROOT/lib/$SYSNAME/$OPTMODE"
PLUGIN_SO="$AIVI_LIB/liboaAiviLibDef.so"
PLUGIN_SRC="$OAPLG_ROOT/src/aivilibdef/oaAiviLibDef.cpp"
TEST_SRC="$OAPLG_ROOT/src/aivilibdef/oaAiviLibDefTest.cpp"
TEST_BIN="$OAPLG_ROOT/src/aivilibdef/test_aivilibdef"

command -v "$CXX" >/dev/null 2>&1 || die "compiler not found: $CXX"
command -v ldd >/dev/null 2>&1 || die "ldd not found"

require_file "$PLUGIN_SRC"
require_file "$TEST_SRC"
require_dir "$OA_INC"
require_dir "$OA_LIB"
require_file "$OA_LIB/liboaCommon.so"
require_file "$OA_LIB/liboaDM.so"
require_file "$OA_LIB/liboaBase.so"
require_file "$OA_LIB/liboaPlugIn.so"

mkdir -p "$AIVI_LIB"

if [[ "$NO_BUILD" -eq 0 ]]; then
    log "Building liboaAiviLibDef.so"
    run "$CXX" -shared -fPIC -std=c++17 -O2 \
        -I "$OA_INC" \
        -L "$OA_LIB" \
        -Wl,-rpath,'$ORIGIN' \
        "$PLUGIN_SRC" \
        -loaCommon -loaDM -loaBase -loaPlugIn -lpthread \
        -o "$PLUGIN_SO"

    log "Building test_aivilibdef"
    run "$CXX" -std=c++17 -O2 \
        -I "$OA_INC" \
        "$TEST_SRC" \
        -L "$OA_LIB" \
        -loaDM -loaBase -loaCommon -loaPlugIn -lpthread \
        -o "$TEST_BIN"
else
    log "Skipping build"
    require_file "$PLUGIN_SO"
    require_file "$TEST_BIN"
fi

TMP_ROOT=$(mktemp -d /tmp/aivilibdef-smoke.XXXXXX)
PLUGIN_DIR="$TMP_ROOT/plugins"
WORK_DIR="$TMP_ROOT/work"
mkdir -p "$PLUGIN_DIR" "$WORK_DIR"

cleanup() {
    if [[ "$KEEP_TMP" -eq 1 ]]; then
        printf '\nKept temporary directory: %s\n' "$TMP_ROOT"
    else
        rm -rf "$TMP_ROOT"
    fi
}
trap cleanup EXIT

cat > "$PLUGIN_DIR/oaLibDef.plg" <<'EOF'
<?xml version="1.0" encoding="utf-8" ?>
<plugIn lib="oaAiviLibDef" category="oaLibDefSystem"/>
EOF

cat > "$PLUGIN_DIR/oaLibDefSystem.plg" <<'EOF'
<?xml version="1.0" encoding="utf-8" ?>
<plugin treatAs="oaLibDef" />
EOF

export LD_LIBRARY_PATH="$AIVI_LIB:$OA_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export OA_PLUGIN_PATH="$PLUGIN_DIR${OA_PLUGIN_PATH:+:$OA_PLUGIN_PATH}"

if [[ "$DEBUG" -eq 1 ]]; then
    export AIVI_LIBDEF_DEBUG=1
fi

log "Runtime paths"
printf 'OAPLG_ROOT=%s\n' "$OAPLG_ROOT"
printf 'OA_LIB=%s\n' "$OA_LIB"
printf 'AIVI_LIB=%s\n' "$AIVI_LIB"
printf 'OA_PLUGIN_PATH=%s\n' "$OA_PLUGIN_PATH"
printf 'LD_LIBRARY_PATH=%s\n' "$LD_LIBRARY_PATH"

log "Checking OA linkage"
ldd "$PLUGIN_SO" | awk '/liboa(Common|DM|Base|PlugIn)\.so/ {print}'

log "Running smoke test"
(
    cd "$WORK_DIR"
    run "$TEST_BIN"
)

log "Smoke test passed"
