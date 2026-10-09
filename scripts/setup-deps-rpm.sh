#!/usr/bin/env bash
# setup-deps-rpm.sh – dnf: Fedora/RHEL/Rocky/Alma/CentOS
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-rpm.sh — библиотека." >&2; exit 1; fi

deps_packages() {
# EPEL 8: mpv-libs-devel (из RPM Fusion). EPEL 9/10: mpv-devel (canonical name,
    # provides mpv-libs-devel for compatibility).
    local _mpv="mpv-libs-devel"
    if distro_is 'rhel-*' 'rocky-*' 'almalinux-*' 'centos-*'; then
        case "$(distro_major)" in
            9|10) _mpv="mpv-devel" ;;
        esac
    fi
    cat <<EOF
build-essential:gcc gcc-c++ make
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg2
pkg-config:pkg-config
wget:wget
curl:curl
curl-dev:libcurl-devel
mpv-dev:$_mpv
rapidjson-dev:rapidjson-devel
gtk3-dev:gtk3-devel
rsvg-common:librsvg2
x11-dev:libX11-devel
x11-xcb-dev:libxcb-devel
gl-dev:mesa-libGL-devel
egl-dev:mesa-libEGL-devel
png-dev:libpng-devel
jpeg-dev:libjpeg-turbo-devel
webp-dev:libwebp-devel
zlib-dev:zlib-devel
expat-dev:expat-devel
freetype-dev:freetype-devel
EOF
}

deps_is_installed() { rpm -q "$1" &>/dev/null; }
deps_update()  { $SUDO dnf makecache -q; }
deps_install() { $SUDO dnf install -y "$@"; }
deps_remove()  { $SUDO dnf remove -y "$@" && $SUDO dnf autoremove -y; }