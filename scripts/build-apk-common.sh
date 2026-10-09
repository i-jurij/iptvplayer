#!/usr/bin/env bash
# =============================================================================
# build-apk-common.sh – движок сборки .apk для Alpine Linux
# =============================================================================
# Библиотека. Сорсится из build-package.sh до адаптера build-native-apk-alpine.sh.
#
# Работает ТОЛЬКО на Alpine (musl). На glibc-хосте бинарник из install/
# не запустится на Alpine — сборка возможна только внутри alpine-окружения.
#
# Конфигурация адаптера:
#   APK_PKGREL       (опц.) pkgrel; default = 0
#   APK_ARCH         (опц.) arch=; default = "x86_64 aarch64"
#   APK_DEPENDS      (опц.) runtime-зависимости через пробел; default = "gtk+3.0 mpv"
#   APK_MAKEDEPENDS  (опц.) build-зависимости через пробел
#
# Артефакт: dist/iptvplayer-<ver>-r<pkgrel>.apk
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-apk-common.sh — библиотека." >&2; exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

build_apk_common() {
    : "${PROJECT_ROOT:?}" "${STAGING_DIR:?}" "${OUTPUT_DIR:?}"
    : "${PACKAGE_NAME:?}" "${VERSION:?}" "${DISTRO:?}"

    local pkgrel="${APK_PKGREL:-0}"
    local arch="${APK_ARCH:-x86_64 aarch64}"
    local depends="${APK_DEPENDS:-gtk+3.0 mpv}"
    local makedepends="${APK_MAKEDEPENDS:-}"

    local apk_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-r${pkgrel}.apk"
    local spec_dir="$PROJECT_ROOT/pkg-apk"

    echo "[+] Сборка Alpine .apk (pkgrel=$pkgrel)..."

    if ! command -v abuild >/dev/null 2>&1; then
        echo "[!] abuild не найден. Установите: apk add alpine-sdk" >&2
        return 1
    fi

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_apk_common: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    rm -rf "$spec_dir"
    mkdir -p "$spec_dir"

    # abuild требует, чтобы APKBUILD лежал в директории с именем пакета.
    local pkg_dir="$spec_dir/$PACKAGE_NAME"
    mkdir -p "$pkg_dir"
    cp -a "$STAGING_DIR/usr" "$pkg_dir/staging_usr"

    local depends_line=""
    [ -n "$depends" ] && depends_line="depends=\"$depends\""
    local makedepends_line=""
    [ -n "$makedepends" ] && makedepends_line="makedepends=\"$makedepends\""

    cat > "$pkg_dir/APKBUILD" << EOF
# Maintainer: ijurij <mnisjil@duck.com>
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=$pkgrel
pkgdesc="IPTV Playlist Player"
url="https://github.com/i-jurij/$PACKAGE_NAME"
arch="$arch"
license="MIT"
$depends_line
$makedepends_line
source=""
builddir="\$srcdir"

build() {
    # Бинарник уже собран — на этом шаге ничего не делаем.
    :
}

package() {
    install -d "\$pkgdir/usr"
    cp -a "\$startdir/staging_usr/." "\$pkgdir/usr/"
}
EOF

    # abuild отказывается работать от root. Если мы root — создаём
    # непривилегированного builder и работаем от него через su.
    local su_prefix=""
    if [ "$(id -u)" -eq 0 ]; then
        adduser -D builder 2>/dev/null \
          || useradd -m builder 2>/dev/null || true
        chown -R builder:builder "$pkg_dir"
        su_prefix="su -c"
    fi

    # Ключи abuild. Генерируются один раз, от того же пользователя,
    # что запускает abuild.
    local key_cmd='[ -f "$HOME/.abuild/$(hostname).rsa" ] || abuild-keygen -a -n'
    if [ -n "$su_prefix" ]; then
        $su_prefix "$key_cmd" builder </dev/null >/dev/null 2>&1 || true
    else
        bash -c "$key_cmd" >/dev/null 2>&1 || true
    fi

    # -F: force (перезаписать существующий пакет).
    # -P: не подписывать (приватный ключ не используется).
    # -K: keep going при ошибке (для диагностики; на успех не влияет).
    # -r (рекурсия) не нужен: исходников нет, всё уже в staging_usr.
    local abuild_cmd="cd '$pkg_dir' && abuild -F -K -P"
    if [ -n "$su_prefix" ]; then
        if ! $su_prefix "$abuild_cmd" builder </dev/null; then
            echo "[!] abuild упал" >&2
            return 1
        fi
    else
        if ! bash -c "$abuild_cmd"; then
            echo "[!] abuild упал" >&2
            return 1
        fi
    fi

    # Поиск собранного .apk. abuild кладёт его в $pkg_dir или ~/packages.
    local built
    built=$(find "$spec_dir" -name "${PACKAGE_NAME}-${VERSION}-r${pkgrel}.apk" \
              -type f -print -quit 2>/dev/null)
    if [ -z "$built" ]; then
        echo "[!] build_apk_common: abuild не создал .apk в $spec_dir" >&2
        return 1
    fi
    if ! mv "$built" "$apk_file"; then
        echo "[!] не удалось переместить $built → $apk_file" >&2
        return 1
    fi

    echo "[✓] Alpine .apk: $apk_file"
    return 0
}