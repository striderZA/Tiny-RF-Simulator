#!/bin/sh
# Install the newest Tiny RF Simulator release over an extracted copy.
#
# The app does not download or install anything itself. This script is always
# run by hand. It uses curl or wget, plus tar (Linux) or unzip (Windows) -- the
# same tools the release process already assumes.
set -eu

REPO="striderZA/Tiny-RF-Simulator"
API_URL="https://api.github.com/repos/$REPO/releases/latest"
DOWNLOAD_ROOT="https://github.com/$REPO/releases/download"

DIR="."
TAG=""
ASSET_URL=""
SHA256_URL=""
DRY_RUN=0
RUNNING_CHECK=1

usage() {
    cat <<'EOF'
Usage: update.sh [options]

Downloads the newest release for this platform and installs it over the copy in
--dir. The previous files are kept in a backup folder beside the app.

Options:
  --dir DIR             Folder holding the installed app (default: current directory).
  --tag TAG             Install a specific release tag instead of the newest one.
  --asset-url URL       Download this archive instead of the release asset.
  --sha256-url URL      Checksum file to verify (default: <asset-url>.sha256 when published).
  --dry-run             Download and verify, then print what would happen.
  --no-running-check    Skip the Windows check for a running tiny-rf-simulator.exe.
  -h, --help            Show this help.

Set http_proxy/https_proxy like curl or wget expects if your network needs them.
EOF
}

die() {
    echo "update.sh: $*" >&2
    exit 1
}

note() {
    echo "$*"
}

while [ $# -gt 0 ]; do
    case "$1" in
    --dir)
        [ $# -ge 2 ] || die "--dir needs a value"
        DIR=$2
        shift 2
        ;;
    --tag)
        [ $# -ge 2 ] || die "--tag needs a value"
        TAG=$2
        shift 2
        ;;
    --asset-url)
        [ $# -ge 2 ] || die "--asset-url needs a value"
        ASSET_URL=$2
        shift 2
        ;;
    --sha256-url)
        [ $# -ge 2 ] || die "--sha256-url needs a value"
        SHA256_URL=$2
        shift 2
        ;;
    --dry-run)
        DRY_RUN=1
        shift
        ;;
    --no-running-check)
        RUNNING_CHECK=0
        shift
        ;;
    -h | --help)
        usage
        exit 0
        ;;
    *)
        echo "update.sh: unknown option '$1'" >&2
        usage >&2
        exit 2
        ;;
    esac
done

SYSTEM=$(uname -s 2>/dev/null || echo unknown)
ARCH=$(uname -m 2>/dev/null || echo unknown)
case "$ARCH" in
x86_64 | amd64) ;;
*) die "Unsupported architecture '$ARCH'; releases are x86_64 only." ;;
esac

case "$SYSTEM" in
Linux*)
    PLATFORM=linux
    ASSET="rf-simulator-linux-x86_64.tar.gz"
    ;;
MINGW* | MSYS* | CYGWIN* | Windows_NT | Windows*)
    PLATFORM=windows
    ASSET="rf-simulator-windows-x86_64.zip"
    ;;
*)
    die "Unsupported platform '$SYSTEM'; releases cover Linux and Windows x86_64."
    ;;
esac

if command -v curl >/dev/null 2>&1; then
    DOWNLOADER=curl
elif command -v wget >/dev/null 2>&1; then
    DOWNLOADER=wget
else
    die "Neither curl nor wget is installed. Install one and run this update again."
fi

fetch() {
    url=$1
    output=$2
    if [ "$DOWNLOADER" = curl ]; then
        curl -fsSL --max-time 120 --connect-timeout 20 -o "$output" "$url"
    else
        wget -q -O "$output" --timeout=60 --tries=1 "$url"
    fi
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/rf-sim-update.XXXXXX") ||
    die "Could not create a temporary folder."
cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT HUP INT TERM

if [ -z "$TAG" ] && [ -z "$ASSET_URL" ]; then
    note "Looking up the newest release..."
    fetch "$API_URL" "$WORK/latest.json" || die "Could not read the latest release from GitHub."
    TAG=$(sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$WORK/latest.json" |
        head -n 1)
    [ -n "$TAG" ] || die "GitHub did not report a release tag."
fi

if [ -n "$TAG" ]; then
    echo "$TAG" | grep -Eq '^[vV]?[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.+-]+)?$' ||
        die "Release tag '$TAG' is not a version tag."
fi

if [ -z "$ASSET_URL" ]; then
    [ -n "$TAG" ] || die "No release tag or asset URL was given."
    ASSET_URL="$DOWNLOAD_ROOT/$TAG/$ASSET"
fi

ARCHIVE="$WORK/$ASSET"
note "Downloading $ASSET..."
fetch "$ASSET_URL" "$ARCHIVE" || die "Could not download $ASSET_URL."

listing=""
if [ "$PLATFORM" = linux ]; then
    command -v tar >/dev/null 2>&1 || die "tar is required to install on Linux."
    listing=$(tar -tzf "$ARCHIVE" 2>/dev/null | tr -d '\r') || die "The download is not a valid .tar.gz archive."
    printf '%s\n' "$listing" | grep -Eq '(^/|(^|/)\.\.(/|$))' &&
        die "The archive contains an unsafe path."
    printf '%s\n' "$listing" | grep -Fxq 'rf-simulator-linux' ||
        die "The archive has no rf-simulator-linux executable."
    printf '%s\n' "$listing" | grep -Fxq 'rf-sim-mcp' ||
        die "The archive has no rf-sim-mcp executable."
    printf '%s\n' "$listing" | grep -q '^component_data/library/' ||
        die "The archive has no component_data/library folder."
else
    command -v unzip >/dev/null 2>&1 || die "unzip is required to install on Windows."
    listing=$(unzip -Z1 "$ARCHIVE" 2>/dev/null | tr -d '\r') || die "The download is not a valid .zip archive."
    printf '%s\n' "$listing" | grep -Eq '(^/|(^|/)\.\.(/|$))' &&
        die "The archive contains an unsafe path."
    printf '%s\n' "$listing" | grep -Fxq 'rf-simulator-windows/tiny-rf-simulator.exe' ||
        die "The archive has no tiny-rf-simulator.exe."
    printf '%s\n' "$listing" | grep -Fxq 'rf-simulator-windows/rf-sim-mcp.exe' ||
        die "The archive has no rf-sim-mcp.exe."
    printf '%s\n' "$listing" | grep -q '^rf-simulator-windows/component_data/library/' ||
        die "The archive has no component_data/library folder."
    printf '%s\n' "$listing" | grep -Eiq '^rf-simulator-windows/[^/]+\.dll$' ||
        die "The archive has no MinGW runtime DLLs."
fi

if [ -z "$SHA256_URL" ]; then
    checksum_url="$ASSET_URL.sha256"
    if fetch "$checksum_url" "$WORK/published.sha256" 2>/dev/null; then
        SHA256_URL=$checksum_url
    fi
fi

if [ -n "$SHA256_URL" ]; then
    fetch "$SHA256_URL" "$WORK/checksum.txt" || die "Could not download the checksum file."
    expected=$(awk 'NF { print $1; exit }' "$WORK/checksum.txt")
    echo "$expected" | grep -Eq '^[0-9a-fA-F]{64}$' ||
        die "The checksum file does not contain a SHA-256 digest."
    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "$ARCHIVE" | awk '{ print $1 }')
    elif command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "$ARCHIVE" | awk '{ print $1 }')
    else
        die "A checksum is published, but neither sha256sum nor shasum is installed."
    fi
    [ "$actual" = "$expected" ] ||
        die "Checksum mismatch: the download is corrupt. Nothing was changed."
    note "Checksum verified."
else
    note "No published checksum found; verified the archive contents instead."
fi

if [ "$PLATFORM" = windows ] && [ "$RUNNING_CHECK" -eq 1 ]; then
    if command -v tasklist >/dev/null 2>&1 &&
        tasklist 2>/dev/null | grep -qi 'tiny-rf-simulator\.exe'; then
        die "Tiny RF Simulator is running. Close it and run this update again."
    fi
fi

[ -d "$DIR" ] || die "Install directory '$DIR' does not exist."
DIR=$(cd "$DIR" && pwd)

if [ "$PLATFORM" = linux ]; then
    ITEMS="rf-simulator-linux rf-sim-mcp component_data"
else
    ITEMS="tiny-rf-simulator.exe rf-sim-mcp.exe component_data"
fi

if [ "$DRY_RUN" -eq 1 ]; then
    note "Dry run: verified ${TAG:-$ASSET}, would install it into $DIR."
    note "Files that would be replaced: $ITEMS"
    exit 0
fi

EXTRACT="$WORK/extract"
mkdir -p "$EXTRACT"
if [ "$PLATFORM" = linux ]; then
    tar -xzf "$ARCHIVE" -C "$EXTRACT" || die "Could not extract the archive."
    SOURCE="$EXTRACT"
else
    unzip -q "$ARCHIVE" -d "$EXTRACT" || die "Could not extract the archive."
    SOURCE="$EXTRACT/rf-simulator-windows"
    for dll in "$SOURCE"/*.dll; do
        [ -e "$dll" ] || continue
        ITEMS="$ITEMS $(basename "$dll")"
    done
fi

STAGE=$(mktemp -d "$DIR/.rf-sim-update.XXXXXX") ||
    die "Could not write to $DIR."
cleanup_update() {
    rm -rf "$STAGE"
    cleanup
}
trap cleanup_update EXIT HUP INT TERM

for item in $ITEMS; do
    [ -e "$SOURCE/$item" ] || die "The archive is missing $item."
    cp -pR "$SOURCE/$item" "$STAGE/$item" || die "Could not stage $item."
done

BACKUP="$DIR/.rf-sim-backup-$(date +%Y%m%d-%H%M%S)-$$"
mkdir -p "$BACKUP" || die "Could not create the backup folder $BACKUP."

rollback() {
    for item in $INSTALLED; do
        [ -n "$item" ] || continue
        rm -rf -- "${DIR:?}/${item:?}"
    done
    for item in $MOVED; do
        [ -n "$item" ] || continue
        if [ -e "$BACKUP/$item" ]; then
            mv "$BACKUP/$item" "$DIR/$item"
        fi
    done
}

INSTALLED=""
MOVED=""
for item in $ITEMS; do
    if [ -e "$DIR/$item" ]; then
        if ! mv "$DIR/$item" "$BACKUP/$item"; then
            rollback
            die "Could not back up $item; the previous files were restored."
        fi
        MOVED="$MOVED $item"
    fi
done

for item in $ITEMS; do
    if ! mv "$STAGE/$item" "$DIR/$item"; then
        rollback
        die "Could not install $item; the previous files were restored."
    fi
    INSTALLED="$INSTALLED $item"
done

note "Updated Tiny RF Simulator to ${TAG:-$ASSET} in $DIR."
if [ -n "$MOVED" ]; then
    note "Previous files are in $BACKUP"
    note "To roll back, remove each new item and move its backup back:"
    note "  cd \"$DIR\" && for item in$MOVED; do rm -rf -- \"\$item\"; mv -- \"$BACKUP/\$item\" \"\$item\"; done"
else
    note "This was a fresh install; no previous files needed a backup."
fi
