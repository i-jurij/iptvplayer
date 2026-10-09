#!/usr/bin/env bash
# setup-deps-alt.sh – ALT Linux: apt-rpm
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-alt.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat <<'EOF'
build-essential:gcc-c++ make pkgconfig rpm-build glibc-devel kernel-headers-common
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
mpv-dev:libmpv-devel
rapidjson-dev:rapidjson-devel
gtk3-dev:libgtk+3-devel
rsvg-common:librsvg
x11-dev:libX11-devel
x11-xcb-dev:libxcb-devel
gl-dev:libGL-devel
egl-dev:libEGL-devel
wayland-egl-dev:libwayland-egl-devel
png-dev:libpng-devel
jpeg-dev:libjpeg-devel
webp-dev:libwebp-devel
zlib-dev:zlib-devel
expat-dev:libexpat-devel
freetype-dev:libfreetype-devel
EOF
}

deps_is_installed() { rpm -q "$1" &>/dev/null; }
deps_update()  { $SUDO apt-get update -qq; }
deps_install() { $SUDO apt-get install -y "$@"; }
deps_remove()  { $SUDO apt-get remove -y --auto-remove "$@"; }