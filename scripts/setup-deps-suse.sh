#!/usr/bin/env bash
# setup-deps-suse.sh – zypper: openSUSE/SLES
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-suse.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat <<'EOF'
build-essential:gcc gcc-c++ make
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gpg2
pkg-config:pkg-config
wget:wget
curl:curl
curl-dev:libcurl-devel
mpv-dev:mpv-devel
rapidjson-dev:rapidjson-devel
gtk3-dev:gtk3-devel
rsvg-common:librsvg-2-2
x11-dev:libX11-devel
x11-xcb-dev:libxcb-devel
gl-dev:Mesa-libGL-devel
egl-dev:Mesa-libEGL-devel
png-dev:libpng16-devel
jpeg-dev:libjpeg8-devel
webp-dev:libwebp-devel
zlib-dev:zlib-devel
expat-dev:libexpat-devel
freetype-dev:freetype2-devel
EOF
}

deps_is_installed() { rpm -q "$1" &>/dev/null; }
deps_update()  { $SUDO zypper --non-interactive refresh; }
deps_install() { $SUDO zypper --non-interactive install -y "$@"; }
deps_remove()  { $SUDO zypper --non-interactive remove -y --clean-deps "$@"; }