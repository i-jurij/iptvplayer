#!/usr/bin/env bash
# setup-deps-deb.sh – apt: Debian/Ubuntu/Mint/Pop/Astra
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-deb.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat <<'EOF'
build-essential:build-essential
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
debsigs:debsigs
pkg-config:pkg-config
wget:wget
curl:curl
curl-dev:libcurl4-openssl-dev
mpv-dev:libmpv-dev
rapidjson-dev:rapidjson-dev
gtk3-dev:libgtk-3-dev
rsvg-common:librsvg2-common
x11-dev:libx11-dev
x11-xcb-dev:libx11-xcb-dev
gl-dev:libgl1-mesa-dev
egl-dev:libegl1-mesa-dev
png-dev:libpng-dev
jpeg-dev:libjpeg-dev
webp-dev:libwebp-dev
zlib-dev:zlib1g-dev
expat-dev:libexpat1-dev
freetype-dev:libfreetype-dev
EOF
}

deps_is_installed() {
    dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"
}
deps_update()  { $SUDO apt-get update -qq; }
deps_install() { $SUDO apt-get install -y --no-install-recommends "$@"; }
deps_remove()  { $SUDO apt-get remove -y --auto-remove "$@"; }