#!/usr/bin/env bash
# setup-deps-xbps.sh – xbps: Void Linux
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-xbps.sh — библиотека." >&2; exit 1; fi

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
curl-dev:libcurl-devel
mpv-dev:mpv-devel
rapidjson-dev:rapidjson
gtk3-dev:gtk+3-devel
rsvg-common:librsvg
x11-dev:libX11-devel
x11-xcb-dev:libxcb-devel
gl-dev:MesaLib-devel
egl-dev:MesaLib-devel
png-dev:libpng-devel
jpeg-dev:libjpeg-turbo-devel
webp-dev:libwebp-devel
zlib-dev:zlib-devel
expat-dev:expat-devel
freetype-dev:freetype-devel
EOF
}

deps_is_installed() { xbps-query -l "$1" >/dev/null 2>&1; }
deps_update()  { $SUDO xbps-install -S >/dev/null 2>&1 || true; }
deps_install() { $SUDO xbps-install -y "$@"; }
deps_remove()  { $SUDO xbps-remove -y -R "$@"; }