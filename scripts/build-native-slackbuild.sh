#!/usr/bin/env bash
# =============================================================================
# build-native-slackbuild.sh – нативный .txz для Slackware
# =============================================================================
# Библиотека. Сорсится из build-package.sh.
#
# Особенности Slackware:
#   * Нет пакетного менеджера с автоустановкой (аналога apt/dnf нет).
#     Пользователь сам ставит зависимости.
#   * Нет метаданных о зависимостях в пакете — только раскладка файлов
#     + slack-desc (обязательное описание).
#   * Сборка — через SlackBuild + makepkg (пакет pkgtools).
#   * Артефакт: dist/iptvplayer-<ver>-<arch>-<build>.txz
#
# Переменные окружения:
#   SLACKWARE_BUILD  (опц.) build-номер; default = 1
#   SLACKWARE_TAG    (опц.) тег сборки; default = пусто
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-slackbuild.sh — библиотека." >&2; exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

build_native_slackbuild() {
    : "${PROJECT_ROOT:?}" "${STAGING_DIR:?}" "${OUTPUT_DIR:?}"
    : "${PACKAGE_NAME:?}" "${VERSION:?}" "${DISTRO:?}"

    local ARCH; ARCH="$(uname -m)"
    local BUILD="${SLACKWARE_BUILD:-1}"
    local TAG="${SLACKWARE_TAG:-}"

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${ARCH}-${BUILD}${TAG}.txz"
    local build_dir="$PROJECT_ROOT/pkg-slackbuild"

    echo "[+] Сборка Slackware .txz (arch=$ARCH, build=$BUILD)..."

    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg не найден. Требуется Slackware с pkgtools." >&2
        return 1
    fi

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_native_slackbuild: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    rm -rf "$build_dir"
    mkdir -p "$build_dir/PKG/install"

    cp -a "$STAGING_DIR/usr" "$build_dir/PKG/"

    # slack-desc — обязателен для makepkg. Формат: 11 строк, первая —
    # "appname: описание", appname точно совпадает с именем пакета.
    cat > "$build_dir/PKG/install/slack-desc" << EOF
# HOW TO EDIT THIS FILE:
# The "handy ruler" below makes it easier to edit a package description.
# Line up the first '|' above the ':' following the base package name, and
# the '|' on the right side marks the last column you can put a character in.
# You must make exactly 11 lines for the formatting to be correct.  It's also
# customary to leave one space after the ':' except on otherwise blank lines.

         |-----handy-ruler------------------------------------------------------|
$PACKAGE_NAME: $PACKAGE_NAME (IPTV Playlist Player)
$PACKAGE_NAME:
$PACKAGE_NAME: A simple player for M3U playlists with GUI.
$PACKAGE_NAME:
$PACKAGE_NAME: Homepage: https://github.com/i-jurij/$PACKAGE_NAME
$PACKAGE_NAME:
$PACKAGE_NAME:
$PACKAGE_NAME:
$PACKAGE_NAME:
$PACKAGE_NAME:
EOF

    # makepkg запускается из каталога PKG, упаковывает его содержимое.
    if ! ( cd "$build_dir/PKG" \
           && makepkg -l y -c n "$pkg_file" ); then
        echo "[!] makepkg упал" >&2
        return 1
    fi

    if [ ! -f "$pkg_file" ]; then
        echo "[!] build_native_slackbuild: makepkg не создал $pkg_file" >&2
        return 1
    fi

    echo "[✓] Slackware .txz: $pkg_file"
    return 0
}