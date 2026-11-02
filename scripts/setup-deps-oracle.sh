#!/usr/bin/env bash
# setup-deps-oracle.sh – Oracle Linux: dnf, EPEL from BaseOS + CRB.
# Fedora's epel-release is not needed here: Oracle ships
# oracle-epel-release-el${major} (BaseOS), which configures EPEL.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-oracle.sh — library." >&2; exit 1; fi

deps_packages() {
    # Oracle 10 = EL10. EPEL 10 package is mpv-devel
    # (canonical name, provides mpv-libs-devel).
    cat <<'EOF'
build-essential:gcc gcc-c++ make
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gettext:gettext
gnupg:gnupg2
pkg-config:pkg-config
wget:wget
curl:curl
curl-dev:libcurl-devel
mpv-dev:mpv-devel
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
deps_remove()  { $SUDO dnf remove -y "$@"; }

# Oracle-specific repo setup. Called from setup-deps.sh after source,
# before package checks. Optional function: other deps files do not
# define it.
deps_setup_repos() {
    local _major; _major="$(distro_major)"
    if [ -z "$_major" ]; then
        warn "deps_setup_repos: DISTRO=$DISTRO — cannot determine major, skipping."
        return 0
    fi

    local _epel_pkg="oracle-epel-release-el${_major}"
    if ! rpm -q "$_epel_pkg" &>/dev/null; then
        echo
        warn "For build on $DISTRO need $_epel_pkg"
        if ask "Enable $_epel_pkg? [Y/n]:" y; then
            $SUDO dnf install -y "$_epel_pkg" \
                || error "Failed to enable $_epel_pkg"
        else
            error "Cannot build without $_epel_pkg"
        fi
    fi

    # CRB is required for some EPEL dependencies.
    $SUDO dnf config-manager --set-enabled "ol${_major}_codeready_builder" \
        || warn "Failed to enable ol${_major}_codeready_builder — check manually."
}