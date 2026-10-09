#!/usr/bin/env bash
# setup-deps-apk.sh – apk: Alpine Linux
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-apk.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat <<'EOF'
build-essential:build-base
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
pkg-config:pkgconf
wget:wget
curl:curl
curl-dev:curl-dev
mpv-dev:mpv-dev
rapidjson-dev:rapidjson-dev
gtk3-dev:gtk+3.0-dev
rsvg-common:librsvg
x11-dev:libx11-dev
x11-xcb-dev:libxcb-dev
gl-dev:mesa-dev
egl-dev:mesa-dev
png-dev:libpng-dev
jpeg-dev:libjpeg-turbo-dev
webp-dev:libwebp-dev
zlib-dev:zlib-dev
expat-dev:expat-dev
freetype-dev:freetype-dev
EOF
}

deps_is_installed() { apk info -e "$1" >/dev/null 2>&1; }
deps_update()  { :; }
deps_install() { $SUDO apk add --no-cache "$@"; }
deps_remove()  { $SUDO apk del "$@"; }