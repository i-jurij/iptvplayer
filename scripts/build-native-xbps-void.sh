#!/usr/bin/env bash
# =============================================================================
# build-native-xbps-void.sh – нативный .xbps для Void Linux
# =============================================================================
# Библиотека. Сорсится из build-package.sh.
#
# Особенности Void:
#   * Сборка через xbps-src (клон void-packages).
#   * xbps-src не работает от root — нужен пользователь в группе xbuilder
#     или chroot-метод (proot, bwrap, ethereal).
#   * Шаблон — shell-скрипт, размещается в srcpkgs/<pkgname>/template.
#   * Артефакт: dist/iptvplayer-<ver>_<rev>.<arch>.xbps
#
# Переменные окружения:
#   VOID_PACKAGES_DIR   путь к клону void-packages (default = ~/void-packages)
#   XBPS_REVISION       revision в шаблоне (default = 1)
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-xbps-void.sh — библиотека." >&2; exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

build_native_xbps_void() {
    : "${PROJECT_ROOT:?}" "${OUTPUT_DIR:?}" "${PACKAGE_NAME:?}" "${VERSION:?}" "${DISTRO:?}"

    local VOID_PACKAGES_DIR="${VOID_PACKAGES_DIR:-$HOME/void-packages}"
    local revision="${XBPS_REVISION:-1}"
    local arch; arch="$(uname -m)"

    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}_${revision}.${arch}.xbps"
    local template_dir="$VOID_PACKAGES_DIR/srcpkgs/$PACKAGE_NAME"

    echo "[+] Сборка Void .xbps (revision=$revision, arch=$arch)..."

    if [ ! -d "$VOID_PACKAGES_DIR" ]; then
        echo "[!] VOID_PACKAGES_DIR не существует: $VOID_PACKAGES_DIR" >&2
        echo "    Клонируйте: git clone https://github.com/void-linux/void-packages.git" >&2
        return 1
    fi
    if [ ! -x "$VOID_PACKAGES_DIR/xbps-src" ]; then
        echo "[!] xbps-src не найден в $VOID_PACKAGES_DIR" >&2
        return 1
    fi

    local install_dir="$PROJECT_ROOT/install"
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] не найден $install_dir/bin/$PACKAGE_NAME — нужен build-release.sh" >&2
        return 1
    fi

    rm -rf "$template_dir"
    mkdir -p "$template_dir"

    # Копируем готовый install/ в files/ — xbps-src увидит через FILESDIR.
    mkdir -p "$template_dir/files"
    cp -a "$install_dir/." "$template_dir/files/"

    cat > "$template_dir/template" << EOF
# Template file for '${PACKAGE_NAME}'
pkgname=${PACKAGE_NAME}
version=${VERSION}
revision=${revision}
build_style=custom
short_desc="IPTV Playlist Player"
maintainer="ijurij <mnisjil@duck.com>"
license="MIT"
homepage="https://github.com/i-jurij/${PACKAGE_NAME}"
distfiles=""
checksum=""

do_install() {
    # Копируем готовый install/ из files/ в DESTDIR.
    mkdir -p "\${DESTDIR}/usr"
    cp -a "\${FILESDIR}/." "\${DESTDIR}/usr/"
}
EOF

    # xbps-src отказывается работать от root. Если мы root — создаём
    # (идемпотентно) пользователя xbuilder и работаем от него.
    local su_prefix=""
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m xbuilder 2>/dev/null || true
        chown -R xbuilder:xbuilder "$VOID_PACKAGES_DIR"
        su_prefix="su -c"
    fi

    local xbps_cmd="cd '$VOID_PACKAGES_DIR' && ./xbps-src pkg '$PACKAGE_NAME'"
    if [ -n "$su_prefix" ]; then
        if ! $su_prefix "$xbps_cmd" xbuilder </dev/null; then
            echo "[!] xbps-src pkg упал" >&2
            return 1
        fi
    else
        if ! bash -c "$xbps_cmd"; then
            echo "[!] xbps-src pkg упал" >&2
            return 1
        fi
    fi

    # Поиск собранного .xbps в hostdir/binpkgs.
    local built
    built=$(find "$VOID_PACKAGES_DIR/hostdir/binpkgs" \
              -name "${PACKAGE_NAME}-${VERSION}_${revision}.${arch}.xbps" \
              -type f -print -quit 2>/dev/null)
    if [ -z "$built" ]; then
        built=$(find "$VOID_PACKAGES_DIR/hostdir/binpkgs" \
                  -name "${PACKAGE_NAME}-*.xbps" -type f -print -quit 2>/dev/null)
    fi
    if [ -z "$built" ]; then
        echo "[!] xbps-src не создал .xbps в $VOID_PACKAGES_DIR/hostdir/binpkgs" >&2
        return 1
    fi
    if ! cp "$built" "$out_file"; then
        echo "[!] не удалось скопировать $built → $out_file" >&2
        return 1
    fi

    echo "[✓] Void .xbps: $out_file"
    rm -rf "$template_dir"
    return 0
}