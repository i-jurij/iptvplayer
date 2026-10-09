#!/usr/bin/env bash
# =============================================================================
# build-native-gentoo.sh – нативный .tbz2 для Gentoo Linux
# =============================================================================
# Библиотека. Сорсится из build-package.sh.
#
# Особенности Gentoo:
#   * ebuild — bash-скрипт в формате Portage.
#   * Сборка бинарного пакета: ebuild <файл>.ebuild package.
#     Команда создаёт .tbz2 (или .gpkg на новых версиях Portage)
#     в каталоге PKGDIR (по умолчанию /var/cache/binpkgs или
#     /usr/portage/packages).
#   * ebuild не работает от root — нужен непривилегированный пользователь
#     (обычно portage).
#   * Артефакт: dist/iptvplayer-<ver>.tbz2
#
# Переменные окружения:
#   GENTOO_CATEGORY  (опц.) категория пакета; default = media-video
#   GENTOO_PKGDIR    (опц.) переопределение PKGDIR; default = из make.conf
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-gentoo.sh — библиотека." >&2; exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

build_native_gentoo() {
    : "${PROJECT_ROOT:?}" "${OUTPUT_DIR:?}" "${PACKAGE_NAME:?}" "${VERSION:?}" "${DISTRO:?}"

    local category="${GENTOO_CATEGORY:-media-video}"
    local install_dir="$PROJECT_ROOT/install"
    local ebuild_dir="$PROJECT_ROOT/pkg-gentoo/$category/$PACKAGE_NAME"
    local ebuild_file="$ebuild_dir/${PACKAGE_NAME}-${VERSION}.ebuild"
    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}.tbz2"

    echo "[+] Сборка Gentoo .tbz2..."

    if ! command -v ebuild >/dev/null 2>&1; then
        echo "[!] ebuild не найден. Требуется sys-apps/portage (Gentoo)." >&2
        return 1
    fi
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] не найден $install_dir/bin/$PACKAGE_NAME — нужен build-release.sh" >&2
        return 1
    fi

    rm -rf "$ebuild_dir"
    mkdir -p "$ebuild_dir"

    # Копируем install/ в директорию ebuild — src_install будет
    # копировать файлы из FILESDIR (${FILESDIR} = ${PORTDIR}/files).
    mkdir -p "$ebuild_dir/files"
    cp -a "$install_dir/." "$ebuild_dir/files/"

    # EAPI=8 — актуальный на момент написания. SRC_URI пуст: исходников
    # нет, всё берётся из files/. src_install копирует готовые файлы
    # из ${FILESDIR} в ${D} (DESTDIR).
    #
    # insinto / устанавливает корень назначения. doins -r . рекурсивно
    # копирует всё содержимое ${FILESDIR} с сохранением структуры.
    # fperms 0755 возвращает +x бинарнику, т.к. doins ставит 0644.
    cat > "$ebuild_file" << EOF
# Copyright 1999-${VERSION%-*} Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="IPTV Playlist Player"
HOMEPAGE="https://github.com/i-jurij/${PACKAGE_NAME}"
SRC_URI=""

LICENSE="MIT"
SLOT="0"
KEYWORDS="~amd64 ~arm64"

RDEPEND="
    x11-libs/gtk+:3
    media-video/mpv
    x11-libs/wxGTK:3.2
"

src_install() {
    # doins ставит 0644 — теряет +x бинарника. cp -a сохраняет права,
    # fperms добивает +x на случай, если FILESDIR пришёл из upload-artifact.
    cp -a "\${FILESDIR}/." "\${D}/"
    fperms 0755 /usr/bin/${PACKAGE_NAME}
}
EOF

    # ebuild не работает от root. Если мы root — создаём (идемпотентно)
    # пользователя portage и работаем от него.
    local ebuild_cmd="cd '$ebuild_dir' && ebuild '$ebuild_file' package"
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m portage 2>/dev/null || true
        chown -R portage:portage "$ebuild_dir"
        if ! su -c "$ebuild_cmd" portage </dev/null; then
            echo "[!] ebuild package упал" >&2
            return 1
        fi
    else
        if ! bash -c "$ebuild_cmd"; then
            echo "[!] ebuild package упал" >&2
            return 1
        fi
    fi

    # Поиск .tbz2. Каталог PKGDIR по умолчанию /var/cache/binpkgs
    # (новые Portage) или /usr/portage/packages (старые).
    # Порядок поиска: GENTOO_PKGDIR → /var/cache/binpkgs → /usr/portage/packages.
    local built=""
    for _pkgdir in \
        "${GENTOO_PKGDIR:-}" \
        /var/cache/binpkgs \
        /usr/portage/packages \
        "$HOME/.cache/binpkgs"
    do
        [ -z "$_pkgdir" ] && continue
        [ -d "$_pkgdir" ] || continue
        built=$(find "$_pkgdir" -name "${PACKAGE_NAME}-${VERSION}*.tbz2" \
                  -type f -print -quit 2>/dev/null)
        [ -n "$built" ] && break
    done

    if [ -z "$built" ]; then
        echo "[!] ebuild не создал .tbz2. Проверьте PKGDIR в /etc/portage/make.conf" >&2
        return 1
    fi
    if ! cp "$built" "$out_file"; then
        echo "[!] не удалось скопировать $built → $out_file" >&2
        return 1
    fi

    echo "[✓] Gentoo .tbz2: $out_file"
    rm -rf "$ebuild_dir"
    return 0
}