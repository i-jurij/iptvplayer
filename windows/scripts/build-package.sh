#!/usr/bin/env bash
# =============================================================================
# build-package.sh — упаковка iptvplayer для Windows (.zip + NSIS).
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/bootstrap.sh"

INSTALL_DIR="$(win_install_dir)"
DIST_DIR="$(win_dist_dir)"
EXE="$INSTALL_DIR/bin/iptvplayer.exe"

[[ -f "$EXE" ]] || error "Сначала соберите: windows\\bats\\build-release.bat"

mkdir -p "$DIST_DIR"

if [[ -f "$INSTALL_DIR/VERSION_FILE" ]]; then
    VERSION_FILE="$(tr -d '\n\r' < "$INSTALL_DIR/VERSION_FILE")"
elif [[ -f "$INSTALL_DIR/VERSION" ]]; then
    VERSION_FILE="$(tr -d '\n\r' < "$INSTALL_DIR/VERSION")"
else
    VERSION_FILE="unknown"
fi

section "1/2. ZIP"

ZIP_NAME="iptvplayer-${VERSION_FILE}-win64.zip"
ZIP_PATH="$DIST_DIR/$ZIP_NAME"
rm -f "$ZIP_PATH"

TMP_DIR="$DIST_DIR/.pkg-tmp"
rm -rf "$TMP_DIR"
mkdir -p "$TMP_DIR/iptvplayer"
cp -r "$INSTALL_DIR/bin/." "$TMP_DIR/iptvplayer/"
[[ -f "$PROJECT_ROOT/LICENSE" ]]   && cp "$PROJECT_ROOT/LICENSE"   "$TMP_DIR/iptvplayer/"
[[ -f "$PROJECT_ROOT/README.md" ]] && cp "$PROJECT_ROOT/README.md" "$TMP_DIR/iptvplayer/"

if command -v 7z >/dev/null 2>&1; then
    7z a -tzip "$ZIP_PATH" "$TMP_DIR/iptvplayer/" >/dev/null
elif command -v zip >/dev/null 2>&1; then
    ( cd "$TMP_DIR" && zip -qr "$ZIP_PATH" iptvplayer )
else
    error "Не найден ни 7z, ни zip. Установите: pacman -S zip"
fi
rm -rf "$TMP_DIR"

log "ZIP создан: $ZIP_PATH ($(du -h "$ZIP_PATH" | cut -f1))"

section "2/2. NSIS-инсталлятор"

if ! command -v makensis >/dev/null 2>&1; then
    warn "makensis не найден. Пропускаю NSIS."
    warn "Установите: pacman -S mingw-w64-${MSYSTEM,,}-nsis"
    exit 0
fi

NSI_SCRIPT="$PROJECT_ROOT/windows/installer.nsi"
if [[ ! -f "$NSI_SCRIPT" ]]; then
    warn "Скрипт NSIS не найден: $NSI_SCRIPT"
    exit 0
fi

NSIS_NAME="iptvplayer-${VERSION_FILE}-win64-setup.exe"
NSIS_PATH="$DIST_DIR/$NSIS_NAME"
rm -f "$NSIS_PATH"

makensis -V2 \
    -DINSTALL_SRC="$(cygpath -w "$INSTALL_DIR/bin")" \
    -DVERSION="$VERSION_FILE" \
    -DOUTPUT="$(cygpath -w "$NSIS_PATH")" \
    "$(cygpath -w "$NSI_SCRIPT")"

log "Инсталлятор создан: $NSIS_PATH ($(du -h "$NSIS_PATH" | cut -f1))"