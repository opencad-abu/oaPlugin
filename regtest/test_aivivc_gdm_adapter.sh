#!/bin/bash
# Regression for the AIVIVC GDM command adapter.
set -euo pipefail

AIVIVC_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ADAPTER="${AIVIVC_ADAPTER:-$AIVIVC_ROOT/src/aivivc/gdm/aivivc-gdm}"
CONFIG="${AIVIVC_GDMCONFIG:-$AIVIVC_ROOT/src/aivivc/gdm/aivivcgdmconfig}"
SVN_ROOT="${SVN_ROOT:-/software/pkgs/subversion/usr}"
TMPDIR="$(mktemp -d /tmp/aivivc-gdm-regtest.XXXXXX)"
trap 'rm -rf "$TMPDIR"' EXIT

if [ -x "$SVN_ROOT/bin/svn" ]; then
    export PATH="$SVN_ROOT/bin:$PATH"
    export LD_LIBRARY_PATH="$SVN_ROOT/lib64:${LD_LIBRARY_PATH:-}"
fi

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

cat > "$TMPDIR/svn-wrapper" <<'SH'
#!/bin/sh
exec svn "$@"
SH
chmod +x "$TMPDIR/svn-wrapper"
export AIVIVC_SVN="$TMPDIR/svn-wrapper"

run_capture()
{
    local output
    if ! output="$("$@" 2>&1)"; then
        printf '%s\n' "$output" >&2
        return 1
    fi
    printf '%s\n' "$output"
}

assert_contains()
{
    local haystack="$1"
    local needle="$2"
    case "$haystack" in
        *"$needle"*) ;;
        *) fail "expected output to contain: $needle";;
    esac
}

assert_not_contains_file()
{
    local file="$1"
    local needle="$2"
    if grep -F "$needle" "$file" >/dev/null; then
        fail "unexpected text in $file: $needle"
    fi
}

test_config()
{
    local output
    output="$(run_capture "$CONFIG")"
    assert_contains "$output" "nam aivivc"
    assert_contains "$output" "ci"
    output="$(run_capture "$CONFIG" -commands)"
    assert_contains "$output" "statusOptions updateNeeded"
    output="$(run_capture "$CONFIG" -version)"
    assert_contains "$output" 'ver "1"'
}

test_git_adapter()
{
    local root="$TMPDIR/gitroot"
    local cdslib="$root/cds.lib"
    mkdir -p "$root/libA/cell1/schematic"
    printf 'DEFINE libA libA\n' > "$cdslib"
    printf '%s\n%s\n' '-- Master.tag File, Rev:1.0' 'ViewType=schematic' \
        > "$root/libA/cell1/schematic/master.tag"
    printf 'oa-data\n' > "$root/libA/cell1/schematic/sch.oa"

    git init "$root" >/dev/null
    git -C "$root" config user.email aivivc@example.invalid
    git -C "$root" config user.name aivivc
    git -C "$root" config commit.gpgsign false
    git -C "$root" add .
    git -C "$root" commit -m initial >/dev/null

    local output
    output="$(run_capture "$ADAPTER" status -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa)"
    assert_contains "$output" "Check in"
    [ ! -w "$root/libA/cell1/schematic/sch.oa" ] ||
        fail "git tracked file should be protected after status"

    run_capture "$ADAPTER" co -file "$root/libA/cell1/schematic" >/dev/null
    output="$(run_capture "$ADAPTER" status-code -file "$root/libA/cell1/schematic/sch.oa")"
    assert_contains "$output" "display=CHECKED_OUT"
    assert_contains "$output" "perm=WRITE"
    output="$(run_capture "$ADAPTER" ci -message "git gdm noop release" \
        -file "$root/libA/cell1/schematic/sch.oa")"
    assert_contains "$output" "checkout released"
    output="$(run_capture "$ADAPTER" status-code -file "$root/libA/cell1/schematic/sch.oa")"
    assert_contains "$output" "display=CHECKED_IN"
    [ ! -w "$root/libA/cell1/schematic/master.tag" ] ||
        fail "git noop commit should release and protect parent checkout marker"

    run_capture "$ADAPTER" co -file "$root/libA/cell1/schematic" >/dev/null
    run_capture "$ADAPTER" protect -file "$root" >/dev/null
    [ -w "$root/libA/cell1/schematic/sch.oa" ] ||
        fail "git root protect should not protect descendant of checked out directory"
    printf 'dir-dirty\n' >> "$root/libA/cell1/schematic/sch.oa"
    run_capture "$ADAPTER" cancel -file "$root/libA/cell1/schematic" >/dev/null
    assert_not_contains_file "$root/libA/cell1/schematic/sch.oa" "dir-dirty"

    run_capture "$ADAPTER" co -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa >/dev/null
    [ -w "$root/libA/cell1/schematic/sch.oa" ] ||
        fail "git checked out file should be writable"

    output="$(run_capture "$ADAPTER" status-code -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa)"
    assert_contains "$output" "display=CHECKED_OUT"

    chmod u+w "$root/libA/cell1/schematic/master.tag"
    run_capture "$ADAPTER" protect -file "$root" >/dev/null
    [ -w "$root/libA/cell1/schematic/sch.oa" ] ||
        fail "git root protect should not protect checked out file"
    [ ! -w "$root/libA/cell1/schematic/master.tag" ] ||
        fail "git root protect should protect non-checked-out tracked file"

    printf 'changed\n' >> "$root/libA/cell1/schematic/sch.oa"

    run_capture "$ADAPTER" ci -message "git gdm update" -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa >/dev/null
    [ -z "$(git -C "$root" status --porcelain -- libA/cell1/schematic/sch.oa)" ] ||
        fail "git tracked file was not committed"
    [ ! -w "$root/libA/cell1/schematic/sch.oa" ] ||
        fail "git committed file should be protected"

    printf 'net-data\n' > "$root/libA/cell1/schematic/net.oa"
    run_capture "$ADAPTER" ci -message "git gdm add" -cdslib "$cdslib" \
        -lib libA.cell1:schematic/net.oa >/dev/null
    git -C "$root" ls-files --error-unmatch libA/cell1/schematic/net.oa >/dev/null

    run_capture "$ADAPTER" co -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa >/dev/null
    printf 'dirty\n' >> "$root/libA/cell1/schematic/sch.oa"
    run_capture "$ADAPTER" cancel -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa >/dev/null
    assert_not_contains_file "$root/libA/cell1/schematic/sch.oa" "dirty"
    [ ! -w "$root/libA/cell1/schematic/sch.oa" ] ||
        fail "git canceled file should be protected"

    output="$(run_capture "$ADAPTER" history -cdslib "$cdslib" \
        -lib libA.cell1:schematic/sch.oa)"
    assert_contains "$output" "git gdm update"
}

test_svn_adapter()
{
    command -v svn >/dev/null
    command -v svnadmin >/dev/null

    local repo="$TMPDIR/svnrepo"
    local wc="$TMPDIR/svnwc"
    local cdslib="$wc/cds.lib"
    svnadmin create "$repo"
    svn checkout "file://$repo" "$wc" >/dev/null
    mkdir -p "$wc/libS/cell1/schematic"
    printf 'DEFINE libS libS\n' > "$cdslib"
    printf '%s\n%s\n' '-- Master.tag File, Rev:1.0' 'ViewType=schematic' \
        > "$wc/libS/cell1/schematic/master.tag"
    printf 'oa-data\n' > "$wc/libS/cell1/schematic/sch.oa"
    svn add "$cdslib" "$wc/libS" >/dev/null
    svn commit -m initial "$wc" >/dev/null

    local output
    output="$(run_capture "$ADAPTER" status -cdslib "$cdslib" \
        -lib libS.cell1:schematic/sch.oa)"
    assert_contains "$output" "Check in"

    run_capture "$ADAPTER" co -file "$wc/libS/cell1/schematic" >/dev/null
    output="$(run_capture "$ADAPTER" status-code -file "$wc/libS/cell1/schematic/sch.oa")"
    assert_contains "$output" "display=CHECKED_OUT"
    assert_contains "$output" "perm=WRITE"
    run_capture "$ADAPTER" protect -file "$wc" >/dev/null
    [ -w "$wc/libS/cell1/schematic/sch.oa" ] ||
        fail "svn root protect should not protect descendant of checked out directory"
    printf 'dir-dirty\n' >> "$wc/libS/cell1/schematic/sch.oa"
    run_capture "$ADAPTER" cancel -file "$wc/libS/cell1/schematic" >/dev/null
    assert_not_contains_file "$wc/libS/cell1/schematic/sch.oa" "dir-dirty"

    run_capture "$ADAPTER" co -cdslib "$cdslib" \
        -lib libS.cell1:schematic/sch.oa >/dev/null
    [ -w "$wc/libS/cell1/schematic/sch.oa" ] ||
        fail "svn checked out file should be writable"

    chmod u+w "$wc/libS/cell1/schematic/master.tag"
    run_capture "$ADAPTER" protect -file "$wc" >/dev/null
    [ -w "$wc/libS/cell1/schematic/sch.oa" ] ||
        fail "svn root protect should not protect checked out file"
    [ ! -w "$wc/libS/cell1/schematic/master.tag" ] ||
        fail "svn root protect should protect non-checked-out file"

    printf 'changed\n' >> "$wc/libS/cell1/schematic/sch.oa"

    run_capture "$ADAPTER" ci -message "svn gdm update" -cdslib "$cdslib" \
        -lib libS.cell1:schematic/sch.oa >/dev/null
    [ -z "$(svn status "$wc/libS/cell1/schematic/sch.oa")" ] ||
        fail "svn tracked file was not committed"
    [ ! -w "$wc/libS/cell1/schematic/sch.oa" ] ||
        fail "svn committed file should be protected"

    printf 'new-data\n' > "$wc/libS/cell1/schematic/new.oa"
    run_capture "$ADAPTER" ci -message "svn gdm add" -cdslib "$cdslib" \
        -lib libS.cell1:schematic/new.oa >/dev/null
    svn info "$wc/libS/cell1/schematic/new.oa" >/dev/null

    run_capture "$ADAPTER" co -cdslib "$cdslib" \
        -lib libS.cell1:schematic/sch.oa >/dev/null
    printf 'dirty\n' >> "$wc/libS/cell1/schematic/sch.oa"
    run_capture "$ADAPTER" cancel -cdslib "$cdslib" \
        -lib libS.cell1:schematic/sch.oa >/dev/null
    assert_not_contains_file "$wc/libS/cell1/schematic/sch.oa" "dirty"
    [ ! -w "$wc/libS/cell1/schematic/sch.oa" ] ||
        fail "svn canceled file should be protected"

    output="$(run_capture "$ADAPTER" history -cdslib "$cdslib" \
        -lib libS.cell1:schematic/sch.oa)"
    assert_contains "$output" "svn gdm update"
}

test_config
test_git_adapter
test_svn_adapter
echo "aivivc gdm adapter regtest ok"
