# distro-patterns: ol-* oracle-*
#
# Oracle Linux (RPM, dnf). EPEL comes from BaseOS
# (oracle-epel-release-el${major}), plus the CRB repository.

PKGMGR=dnf
PKGMGR_CHECK=rpm
COM_CHECK="-q"
COM_UPDATE="makecache"
COM_INSTALL="install -y"
COM_REMOVE="remove -y"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE="upgrade -y"
OPTS_UPGRADE=""
CHECK_CHAIN=("-q|")

_major="${DISTRO##*-}"; _major="${_major%%.*}"

DEPS="
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
"

REQUIRED_TOOLS=("rpmbuild:rpm-build" "rpm:rpm" "tar:tar" "readelf:binutils")

PACK_FORMAT=rpm
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-oracle"
RPM_RELEASE="1.el${_major}"
RPM_FILE_RELEASE="1.el${_major}"
RPM_OUTPUT_SUFFIX="el${_major}"
RPM_GROUP=""
RPM_CHANGELOG_TAG=""
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (Oracle Linux)"

distro_setup_repos() {
    local _epel="oracle-epel-release-el${_major}"
    if ! deps_is_installed "$_epel"; then
        warn "Build on $DISTRO needs $_epel"
        if ask "Enable $_epel? [Y/n]:" y; then
            log "Installing $_epel..."
            deps_install "$_epel" || error "Failed to enable $_epel"
        else
            error "Cannot build without $_epel"
        fi
    fi
    log "Enabling ol${_major}_codeready_builder repo..."
    $SUDO dnf config-manager --set-enabled "ol${_major}_codeready_builder" \
        || warn "Failed to enable ol${_major}_codeready_builder — check manually"
    deps_update || warn "deps_update failed, continuing"
}

distro_build() {
    prepare_staging && pack_rpm
}