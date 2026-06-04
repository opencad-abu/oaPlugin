#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <workspace-root>" >&2
    exit 2
fi

WORKSPACE_ROOT="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DISPLAY_SRC="$REPO_ROOT/src/aivivc/gdm/displayPrefs"
DISPLAY_DST="$WORKSPACE_ROOT/.cadence/libManager/displayPrefs"
ICON_DST_DIR="$WORKSPACE_ROOT/.cadence/icons/16x16"
TAG_DST="$WORKSPACE_ROOT/cdsinfo.tag"
SOS_ICON_DIR="${AIVIVC_SOS_ICON_DIR:-/software/eda/cliosoft/installs/sos/sos_7.05.p3_linux64/adaptors/virtuoso/images}"

mkdir -p "$(dirname "$DISPLAY_DST")" "$ICON_DST_DIR"
install -m 644 "$DISPLAY_SRC" "$DISPLAY_DST"

copy_icon() {
    local name="$1"
    if [ -f "$ICON_DST_DIR/$name" ]; then
        return
    fi
    if [ -f "$SOS_ICON_DIR/$name" ]; then
        install -m 644 "$SOS_ICON_DIR/$name" "$ICON_DST_DIR/$name"
        return
    fi
    echo "AIVIVC warning: missing icon source for $name" >&2
}

copy_icon min-icon-big.png
copy_icon checked-in.png
copy_icon checked-out.png
copy_icon out-of-date.png
copy_icon not-latest.png
copy_icon unmanaged-library.png
copy_icon unmanaged-object.png
copy_icon locked.png

if [ "${AIVIVC_INSTALL_WORKAREA_TAG:-1}" != "0" ]; then
    existing_dmtype=""
    if [ -f "$TAG_DST" ]; then
        existing_dmtype="$(awk 'tolower($1) == "dmtype" { print tolower($2); exit }' "$TAG_DST")"
    fi
    if [ -z "$existing_dmtype" ]; then
        {
            echo ""
            echo "# AIVIVC workarea Design Management marker."
            echo "DMTYPE aivivc"
        } >> "$TAG_DST"
        chmod 644 "$TAG_DST"
    elif [ "$existing_dmtype" != "aivivc" ]; then
        echo "AIVIVC warning: $TAG_DST already has DMTYPE $existing_dmtype; leaving it unchanged" >&2
    fi
fi

echo "Installed AIVIVC Library Manager assets:"
echo "  displayPrefs -> $DISPLAY_DST"
echo "  icons        -> $ICON_DST_DIR"
if [ "${AIVIVC_INSTALL_WORKAREA_TAG:-1}" != "0" ]; then
    echo "  cdsinfo.tag  -> $TAG_DST"
fi
