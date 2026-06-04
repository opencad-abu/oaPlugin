#!/usr/bin/env bash
# Source this file before launching Virtuoso for AIVIVC/GDM testing.
#
# Usage:
#   source /workarea/ai/openclaw/oaplg/scripts/aivivc_virtuoso_env.sh
#   source /workarea/ai/openclaw/oaplg/scripts/aivivc_virtuoso_env.sh /workarea/xh/smic28

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "Source this file instead of executing it:" >&2
    echo "  source $0 [AIVIVC_ROOT]" >&2
    exit 1
fi

aivivc_prepend_path() {
    local var_name="$1"
    local dir="$2"
    local current value

    [[ -n "$dir" && -d "$dir" ]] || return 0

    current="${!var_name-}"
    case ":$current:" in
        *":$dir:"*) return 0 ;;
    esac

    if [[ -n "$current" ]]; then
        value="$dir:$current"
    else
        value="$dir"
    fi
    printf -v "$var_name" '%s' "$value"
    export "$var_name"
}

export OAPLG="${OAPLG:-/workarea/ai/openclaw/oaplg}"
export OACPP="${OACPP:-/workarea/ai/openclaw/oacpp}"
export SYSNAME="${SYSNAME:-linux_rhel90_64}"
export OPTMODE="${OPTMODE:-opt}"
export AIVIVC_PYTHON_ROOT="${AIVIVC_PYTHON_ROOT:-/software/pkgs/python/3.12.9}"

if [[ $# -ge 1 && -n "$1" ]]; then
    export AIVIVC_ROOT="$1"
fi

if [[ -n "${AIVIVC_ROOT:-}" && "${AIVIVC_SET_CDS_WORKAREA:-1}" != "0" && -z "${CDS_WORKAREA:-}" ]]; then
    export CDS_WORKAREA="$AIVIVC_ROOT"
fi

export AIVIVC_BACKEND="${AIVIVC_BACKEND:-auto}"
export AIVIVC_GIT_COMMAND="${AIVIVC_GIT_COMMAND:-/usr/bin/git}"
export AIVIVC_SVN_COMMAND="${AIVIVC_SVN_COMMAND:-/software/pkgs/subversion/usr/bin/svn}"
export AIVIVC_GDM_ADAPTER="${AIVIVC_GDM_ADAPTER:-$OAPLG/lib/$SYSNAME/$OPTMODE/aivivc-gdm}"
export AIVIVC_SKILL_DIR="${AIVIVC_SKILL_DIR:-$OAPLG/src/aivivc/skill}"
export GDM_USE_SHLIB_ENVVAR="${GDM_USE_SHLIB_ENVVAR:-yes}"
export CDS_GDM_SHLIB_LOCATION="${CDS_GDM_SHLIB_LOCATION:-$OAPLG/lib/$SYSNAME/$OPTMODE}"
unset GDMNOTLOADLIB

aivivc_prepend_path PATH "$OAPLG/lib/$SYSNAME/$OPTMODE"
aivivc_prepend_path PATH "$OAPLG/src/aivivc/gdm"
aivivc_prepend_path PATH "$AIVIVC_PYTHON_ROOT/bin"
aivivc_prepend_path PATH "/software/pkgs/subversion/usr/bin"

aivivc_prepend_path LD_LIBRARY_PATH "$OAPLG/lib/$SYSNAME/$OPTMODE"
aivivc_prepend_path LD_LIBRARY_PATH "$OACPP/lib/$SYSNAME/$OPTMODE"
aivivc_prepend_path LD_LIBRARY_PATH "$AIVIVC_PYTHON_ROOT/lib"
aivivc_prepend_path LD_LIBRARY_PATH "/software/pkgs/subversion/usr/lib64"

aivivc_prepend_path OA_PLUGIN_PATH "$OAPLG/data/plugins"
aivivc_prepend_path OA_PLUGIN_PATH "$OACPP/data/plugins"

if [[ -n "${AIVIVC_ROOT:-}" && "${AIVIVC_INSTALL_LIBMGR_ASSETS:-1}" != "0" ]]; then
    if [[ -x "$OAPLG/scripts/install_aivivc_libmanager_assets.sh" ]]; then
        if ! "$OAPLG/scripts/install_aivivc_libmanager_assets.sh" "$AIVIVC_ROOT" >/dev/null; then
            echo "AIVIVC warning: failed to install Library Manager assets under $AIVIVC_ROOT/.cadence" >&2
        fi
    else
        echo "AIVIVC warning: missing $OAPLG/scripts/install_aivivc_libmanager_assets.sh" >&2
    fi
fi

if [[ -n "${AIVIVC_ROOT:-}" && "${AIVIVC_PROTECT_ON_STARTUP:-1}" != "0" ]]; then
    if [[ -x "$AIVIVC_GDM_ADAPTER" ]]; then
        if ! "$AIVIVC_GDM_ADAPTER" protect -file "$AIVIVC_ROOT" >/dev/null; then
            echo "AIVIVC warning: failed to protect managed files under $AIVIVC_ROOT" >&2
        fi
    else
        echo "AIVIVC warning: missing executable AIVIVC_GDM_ADAPTER=$AIVIVC_GDM_ADAPTER" >&2
    fi
fi

if [[ ! -x "$OAPLG/lib/$SYSNAME/$OPTMODE/aivivcgdmconfig" ]]; then
    echo "AIVIVC warning: missing $OAPLG/lib/$SYSNAME/$OPTMODE/aivivcgdmconfig" >&2
fi
if [[ ! -f "$OAPLG/lib/$SYSNAME/$OPTMODE/libgdmaivivc_sh.so" ]]; then
    echo "AIVIVC warning: missing $OAPLG/lib/$SYSNAME/$OPTMODE/libgdmaivivc_sh.so" >&2
fi
if command -v python3 >/dev/null 2>&1 && ! python3 -c 'import sys' >/dev/null 2>&1; then
    echo "AIVIVC warning: python3 is not runnable; check LD_LIBRARY_PATH and AIVIVC_PYTHON_ROOT=$AIVIVC_PYTHON_ROOT" >&2
fi
