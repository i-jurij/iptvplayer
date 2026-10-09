#!/usr/bin/env bash
# setup-deps-arch.sh – pacman: Arch/Manjaro/EndeavourOS/CachyOS
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-arch.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat <<'EOF'
build-essential:base-devel
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
pkg-config:pkg-config
wget:wget
curl:curl
rapidjson-dev:rapidjson
gtk3-dev:gtk3
rsvg-common:librsvg
x11-dev:libx11
x11-xcb-dev:libxcb
gl-dev:mesa
png-dev:libpng
jpeg-dev:libjpeg-turbo
webp-dev:libwebp
zlib-dev:zlib
expat-dev:expat
freetype-dev:freetype2
EOF
}

# base-devel — группа, а не пакет. pacman -Q её не видит.
deps_is_installed() {
    if [[ "$1" == "base-devel" ]]; then
        [[ -n "$(pacman -Qg base-devel 2>/dev/null | head -1)" ]]
        return
    fi
    pacman -Q "$1" &>/dev/null
}
deps_update()  { $SUDO pacman -Sy --noconfirm >/dev/null; }
deps_install() { $SUDO pacman -S --noconfirm "$@"; }
deps_remove()  { $SUDO pacman -Rs --noconfirm "$@"; }