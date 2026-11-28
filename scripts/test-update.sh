#!/bin/sh
# Offline tests for scripts/update.sh. Builds local fixture archives and points
# the script at them through file:// URLs, so no network is used.
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
UPDATE="$SCRIPT_DIR/update.sh"
SH=$(command -v sh)
ROOT=$(mktemp -d "${TMPDIR:-/tmp}/rf-sim-update-test.XXXXXX") ||
    { echo "could not create a temporary folder" >&2; exit 1; }
cleanup() {
    rm -rf "$ROOT"
}
trap cleanup EXIT HUP INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

file_url() {
    path=$1
    if command -v cygpath >/dev/null 2>&1; then
        path=$(cygpath -m "$path")
    fi
    case "$path" in
    /*) printf 'file://%s\n' "$path" ;;
    *) printf 'file:///%s\n' "$path" ;;
    esac
}

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{ print $1 }'
    else
        shasum -a 256 "$1" | awk '{ print $1 }'
    fi
}

SYSTEM=$(uname -s 2>/dev/null || echo unknown)
case "$SYSTEM" in
MINGW* | MSYS* | CYGWIN* | Windows_NT | Windows*) PLATFORM=windows ;;
*) PLATFORM=linux ;;
esac

# ---------------------------------------------------------------- fixtures
FIXTURE="$ROOT/fixture"
ASSET="rf-simulator-linux-x86_64.tar.gz"

build_linux_archive() {
    archive=$1
    rm -rf "$FIXTURE"
    mkdir -p "$FIXTURE/component_data/library"
    printf '#!/bin/sh\necho new-app\n' >"$FIXTURE/rf-simulator-linux"
    chmod +x "$FIXTURE/rf-simulator-linux"
    printf '#!/bin/sh\necho new-mcp\n' >"$FIXTURE/rf-sim-mcp"
    chmod +x "$FIXTURE/rf-sim-mcp"
    printf '{"part": "fixture"}\n' >"$FIXTURE/component_data/library/part.json"
    tar -czf "$archive" -C "$FIXTURE" rf-simulator-linux rf-sim-mcp component_data
}

build_windows_archive() {
    archive=$1
    command -v zip >/dev/null 2>&1 || return 1
    rm -rf "$FIXTURE"
    mkdir -p "$FIXTURE/rf-simulator-windows/component_data/library"
    printf 'new app\n' >"$FIXTURE/rf-simulator-windows/tiny-rf-simulator.exe"
    printf 'new mcp\n' >"$FIXTURE/rf-simulator-windows/rf-sim-mcp.exe"
    for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
        printf 'new dll\n' >"$FIXTURE/rf-simulator-windows/$dll"
    done
    printf '{"part": "fixture"}\n' >"$FIXTURE/rf-simulator-windows/component_data/library/part.json"
    (cd "$FIXTURE" && zip -qr "$archive" rf-simulator-windows)
}

seed_install() {
    install=$1
    rm -rf "$install"
    mkdir -p "$install/component_data/library"
    if [ "$PLATFORM" = linux ]; then
        printf '#!/bin/sh\necho old-app\n' >"$install/rf-simulator-linux"
        chmod +x "$install/rf-simulator-linux"
        printf '#!/bin/sh\necho old-mcp\n' >"$install/rf-sim-mcp"
        chmod +x "$install/rf-sim-mcp"
    else
        printf 'old app\n' >"$install/tiny-rf-simulator.exe"
        printf 'old mcp\n' >"$install/rf-sim-mcp.exe"
        for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
            printf 'old dll\n' >"$install/$dll"
        done
    fi
    printf '{"part": "old"}\n' >"$install/component_data/library/part.json"
}

app_file() {
    if [ "$PLATFORM" = linux ]; then
        echo "$1/rf-simulator-linux"
    else
        echo "$1/tiny-rf-simulator.exe"
    fi
}

if [ "$PLATFORM" = linux ]; then
    ASSET="rf-simulator-linux-x86_64.tar.gz"
    build_linux_archive "$ROOT/$ASSET"
else
    ASSET="rf-simulator-windows-x86_64.zip"
    build_windows_archive "$ROOT/$ASSET" || fail "zip is required for the Windows fixture"
fi

GOOD="$ROOT/$ASSET"
GOOD_URL=$(file_url "$GOOD")
CHECKSUM="$ROOT/good.sha256"
hash_file "$GOOD" >"$CHECKSUM"
CHECKSUM_URL=$(file_url "$CHECKSUM")

# -------------------------------------------------- happy path with checksum
INSTALL="$ROOT/install"
seed_install "$INSTALL"
OUT="$ROOT/happy.out"
sh "$UPDATE" --dir "$INSTALL" --asset-url "$GOOD_URL" --sha256-url "$CHECKSUM_URL" \
    --tag v9.9.9 --no-running-check >"$OUT" 2>&1 || fail "happy path failed: $(cat "$OUT")"

grep -q 'new-app' "$(app_file "$INSTALL")" ||
    grep -q 'new app' "$(app_file "$INSTALL")" || fail "the new app was not installed"
grep -q 'new mcp' "$INSTALL/rf-sim-mcp$([ "$PLATFORM" = windows ] && echo .exe)" ||
    fail "rf-sim-mcp was not replaced"
grep -q 'fixture' "$INSTALL/component_data/library/part.json" ||
    fail "component_data was not replaced"
grep -q 'Updated Tiny RF Simulator to v9.9.9' "$OUT" || fail "the script did not print the version"
grep -q 'Previous files are in' "$OUT" || fail "the script did not name the backup"
BACKUP=$(find "$INSTALL" -maxdepth 1 -type d -name '.rf-sim-backup-*' 2>/dev/null | head -n 1)
[ -n "$BACKUP" ] || fail "no backup folder was created"
grep -q 'old app' "$(app_file "$BACKUP")" ||
    grep -q 'old-app' "$(app_file "$BACKUP")" || fail "the previous app was not backed up"
grep -q 'old' "$BACKUP/component_data/library/part.json" ||
    fail "the previous component_data was not backed up"
echo "ok - happy path installs, keeps a backup, and prints the result"

# ------------------------------------------------------- bad archive shape
BAD="$ROOT/bad-$ASSET"
printf 'this is not an archive\n' >"$BAD"
INSTALL_BAD="$ROOT/install-bad"
seed_install "$INSTALL_BAD"
if sh "$UPDATE" --dir "$INSTALL_BAD" --asset-url "$(file_url "$BAD")" --tag v9.9.9 \
    --no-running-check >"$ROOT/bad.out" 2>&1; then
    fail "a file that is not an archive was accepted"
fi
grep -q 'old' "$INSTALL_BAD/component_data/library/part.json" ||
    fail "a refused archive changed the install"
grep -q 'old-app' "$(app_file "$INSTALL_BAD")" ||
    grep -q 'old app' "$(app_file "$INSTALL_BAD")" ||
    fail "a refused archive replaced the app"
echo "ok - malformed archive is refused and changes nothing"

# ------------------------------------------------------- checksum mismatch
WRONG_CHECKSUM="$ROOT/wrong.sha256"
printf '%064d  %s\n' 0 "$ASSET" >"$WRONG_CHECKSUM"
INSTALL_SUM="$ROOT/install-sum"
seed_install "$INSTALL_SUM"
if sh "$UPDATE" --dir "$INSTALL_SUM" --asset-url "$GOOD_URL" \
    --sha256-url "$(file_url "$WRONG_CHECKSUM")" --tag v9.9.9 --no-running-check \
    >"$ROOT/sum.out" 2>&1; then
    fail "a checksum mismatch was accepted"
fi
grep -q 'old' "$INSTALL_SUM/component_data/library/part.json" ||
    fail "a checksum mismatch changed the install"
echo "ok - checksum mismatch is refused"

# ------------------------------------------------------------- dry run
INSTALL_DRY="$ROOT/install-dry"
seed_install "$INSTALL_DRY"
sh "$UPDATE" --dir "$INSTALL_DRY" --asset-url "$GOOD_URL" --sha256-url "$CHECKSUM_URL" \
    --tag v9.9.9 --dry-run --no-running-check >"$ROOT/dry.out" 2>&1 ||
    fail "dry run failed: $(cat "$ROOT/dry.out")"
grep -q 'Dry run:' "$ROOT/dry.out" || fail "dry run did not report its plan"
grep -q 'old' "$INSTALL_DRY/component_data/library/part.json" ||
    fail "dry run changed the install"
grep -q 'old-app' "$(app_file "$INSTALL_DRY")" ||
    grep -q 'old app' "$(app_file "$INSTALL_DRY")" ||
    fail "dry run replaced the app"
echo "ok - dry run verifies without changing anything"

# ------------------------------------------------- missing downloader
FAKEBIN="$ROOT/fake-bin"
mkdir -p "$FAKEBIN"
# shellcheck disable=SC2016
printf '#!/bin/sh\ncase "$1" in\n-m) echo x86_64 ;;\n*) echo Linux ;;\nesac\n' >"$FAKEBIN/uname"
chmod +x "$FAKEBIN/uname"
if PATH="$FAKEBIN" "$SH" -c 'command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1'; then
    echo "skip - this shell does not honor a restricted PATH, so a missing downloader cannot be simulated"
else
    if PATH="$FAKEBIN" "$SH" "$UPDATE" --dir "$INSTALL_DRY" --tag v9.9.9 \
        >"$ROOT/notool.out" 2>&1; then
        fail "update.sh ran without curl or wget"
    fi
    grep -q 'Neither curl nor wget' "$ROOT/notool.out" ||
        fail "the missing downloader was not named: $(cat "$ROOT/notool.out")"
    echo "ok - a missing curl/wget is reported clearly"
fi

# ------------------------------------------ Windows running-app refusal
if [ "$PLATFORM" = windows ]; then
    FAKE_TASKS="$ROOT/fake-tasks"
    mkdir -p "$FAKE_TASKS"
    printf '#!/bin/sh\necho "tiny-rf-simulator.exe  1234 Console  1  20,000 K"\n' >"$FAKE_TASKS/tasklist"
    chmod +x "$FAKE_TASKS/tasklist"
    if ! PATH="$FAKE_TASKS:$PATH" "$SH" -c 'tasklist' 2>/dev/null | grep -q 'tiny-rf-simulator.exe'; then
        echo "skip - this shell does not honor a PATH prefix, so a running app cannot be simulated"
    else
        INSTALL_RUN="$ROOT/install-run"
        seed_install "$INSTALL_RUN"
        if PATH="$FAKE_TASKS:$PATH" "$SH" "$UPDATE" --dir "$INSTALL_RUN" --asset-url "$GOOD_URL" \
            --sha256-url "$CHECKSUM_URL" --tag v9.9.9 >"$ROOT/running.out" 2>&1; then
            fail "update.sh ran while tiny-rf-simulator.exe was reported running"
        fi
        grep -q 'is running' "$ROOT/running.out" || fail "the running-app refusal was not named"
        grep -q 'old app' "$(app_file "$INSTALL_RUN")" || fail "the running-app refusal changed the app"
        PATH="$FAKE_TASKS:$PATH" "$SH" "$UPDATE" --dir "$INSTALL_RUN" --asset-url "$GOOD_URL" \
            --sha256-url "$CHECKSUM_URL" --tag v9.9.9 --no-running-check >"$ROOT/running-skip.out" 2>&1 ||
            fail "--no-running-check still refused: $(cat "$ROOT/running-skip.out")"
        echo "ok - Windows running-app refusal works and --no-running-check bypasses it"
    fi
fi

echo "update.sh tests passed"
