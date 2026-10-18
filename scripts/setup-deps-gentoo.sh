#!/usr/bin/env bash
# setup-deps-gentoo.sh – portage: Gentoo Linux
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-gentoo.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat <<'EOF'
build-essential:sys-devel/gcc sys-devel/binutils
cmake:dev-build/cmake
git:dev-vcs/git
autoconf:sys-devel/autoconf
automake:sys-devel/automake
libtool:sys-devel/libtool
gnupg:app-crypt/gnupg
pkg-config:dev-util/pkgconf
wget:net-misc/wget
curl:net-misc/curl
mpv-dev:media-video/mpv
rapidjson-dev:dev-libs/rapidjson
gtk3-dev:x11-libs/gtk+:3
rsvg-common:gnome-base/librsvg
x11-dev:x11-libs/libX11
x11-xcb-dev:x11-libs/libxcb
gl-dev:media-libs/mesa
png-dev:media-libs/libpng
jpeg-dev:media-libs/libjpeg-turbo
webp-dev:media-libs/libwebp
zlib-dev:sys-libs/zlib
expat-dev:dev-libs/expat
freetype-dev:media-libs/freetype
EOF
}

deps_is_installed() {
    # Основной путь: portageq. Для обычных атомов работает надёжно.
    if portageq has_version / "$1" >/dev/null 2>&1; then
        return 0
    fi

    # Fallback для set-пакетов и краевых случаев portageq.
    # qlist -IC печатает category/package без версии; exit code 0,
    # если хотя бы один пакет найден. gentoolkit может отсутствовать —
    # тогда просто возвращаем 1 (пакет считаем неустановленным).
    if command -v qlist >/dev/null 2>&1; then
        qlist -IC "$1" >/dev/null 2>&1 && return 0
    fi

    return 1
}

deps_update()  { $SUDO emerge --sync >/dev/null 2>&1 || true; }
deps_install() { $SUDO emerge -1 --noreplace "$@"; }
deps_remove()  { $SUDO emerge -C "$@"; }