#!/usr/bin/env bash
# setup-deps-rosa.sh – ROSA Linux: dnf, имена пакетов с lib64-префиксом
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-rosa.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    local _pfx="lib64"
    [[ "$(uname -m)" == i686 || "$(uname -m)" == i386 ]] && _pfx="lib"
    cat <<EOF
build-essential:gcc-c++ make glibc-devel
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
pkg-config:pkg-config
wget:wget
curl:curl
curl-dev:${_pfx}curl-devel
mpv-dev:${_pfx}mpv-devel
rapidjson-dev:rapidjson-devel
gtk3-dev:${_pfx}gtk+3.0-devel
rsvg-common:librsvg2
x11-dev:${_pfx}x11-devel
x11-xcb-dev:${_pfx}xcb-devel
gl-dev:${_pfx}gl-devel
egl-dev:${_pfx}egl-devel
png-dev:${_pfx}png-devel
jpeg-dev:${_pfx}jpeg-devel
webp-dev:${_pfx}webp-devel
zlib-dev:${_pfx}zlib-devel
expat-dev:${_pfx}expat-devel
freetype-dev:${_pfx}freetype-devel
EOF
}

deps_is_installed() { rpm -q "$1" &>/dev/null; }
deps_update()  { $SUDO dnf makecache -q; }
deps_install() { $SUDO dnf install -y "$@"; }
deps_remove()  { $SUDO dnf remove -y "$@" && $SUDO dnf autoremove -y; }