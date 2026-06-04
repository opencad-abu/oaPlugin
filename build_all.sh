#!/bin/bash
# oaplg/build_all.sh - Build local OA plugin modules with an isolated build tree.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

MODE="${1:-opt}"

case "$MODE" in
    opt|clean) ;;
    *)
        echo "Usage: $0 [opt|clean]" >&2
        exit 1
        ;;
esac

if [ -z "${SYSNAME:-}" ]; then
    export SYSNAME="$("$SCRIPT_DIR/build/bin/sysname")_64"
else
    export SYSNAME
fi

export CXX="${CXX:-g++}"
export CC="${CC:-gcc}"

SRC_DIR="src"
export OA_INC="${OA_INC:-$SCRIPT_DIR/../oa22.61-devel/include}"
export OACPP="${OACPP:-$SCRIPT_DIR/../oacpp}"
export OA_EXTERNAL_LIB_DIR="${OA_EXTERNAL_LIB_DIR:-${OACPP}/lib/${SYSNAME}/${MODE}}"

if [ -z "${PYTHON_INCLUDE_DIR:-}" ]; then
    PYTHON_INCLUDE_DIR="$(python3 -c 'import sysconfig; print(sysconfig.get_path("include") or "")' 2>/dev/null || true)"
    export PYTHON_INCLUDE_DIR
fi
if [ -z "${PYTHON_LIBDIR:-}" ]; then
    PYTHON_LIBDIR="$(python3 -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR") or sysconfig.get_config_var("LIBPL") or "")' 2>/dev/null || true)"
    export PYTHON_LIBDIR
fi
if [ -n "${PYTHON_LIBDIR:-}" ]; then
    export LD_LIBRARY_PATH="${PYTHON_LIBDIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi

if [ "$MODE" = "clean" ]; then
    rm -rf lib "$SRC_DIR/lib"
    exit 0
fi

export OPTMODE="$MODE"
UNIFIED_LIB="lib/${SYSNAME}/${OPTMODE}"
mkdir -p "$UNIFIED_LIB"

if [ ! -d "$OA_EXTERNAL_LIB_DIR" ]; then
    echo "FATAL: missing OA core libraries: $OA_EXTERNAL_LIB_DIR" >&2
    echo "Build oacpp for SYSNAME=$SYSNAME OPTMODE=$OPTMODE first, or set OA_EXTERNAL_LIB_DIR." >&2
    exit 1
fi
if [ ! -d "$OA_INC" ]; then
    echo "FATAL: missing OA headers: $OA_INC" >&2
    echo "Set OA_INC to the OpenAccess include directory." >&2
    exit 1
fi

echo "=== [src/GNUmakefile] ==="
make -C "$SRC_DIR" OPTMODE="$OPTMODE" SYSNAME="$SYSNAME"

if [ -d "$SRC_DIR/lib" ]; then
    find "$SRC_DIR/lib" -type f \
        \( -name "*.so" -o -name "oaAiviDMTurboServer" -o -name "aivivc-gdm" -o -name "aivivcgdmconfig" \) \
        -path "*/${SYSNAME}/${OPTMODE}/*" | sort | while read -r f; do
            cp -f "$f" "$UNIFIED_LIB/"
            case "$(basename "$f")" in
                aivivc-gdm|aivivcgdmconfig)
                    chmod 755 "$UNIFIED_LIB/$(basename "$f")"
                    ;;
            esac
        done
fi

if [ -f "$SRC_DIR/aivilibdef/oaAiviLibDef.cpp" ]; then
    echo "=== [aivilibdef] ==="
    "$CXX" -shared -fPIC -std=c++17 -O2 \
        -I "$OA_INC" \
        -L "$OA_EXTERNAL_LIB_DIR" -Wl,-rpath,'$ORIGIN' \
        "$SRC_DIR/aivilibdef/oaAiviLibDef.cpp" \
        -loaCommon -loaDM -loaBase -loaPlugIn -lpthread \
        -o "$UNIFIED_LIB/liboaAiviLibDef.so"
fi
