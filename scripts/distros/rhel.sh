# distro-patterns: rhel-* rocky-* almalinux-* centos-* fedora-*
#
# Fedora / RHEL / Rocky / AlmaLinux / CentOS.
# Data + distro_build + distro_setup_repos. Generic code does not know
# distribution names.

PKGMGR=dnf
PKGMGR_CHECK=rpm
COM_CHECK="-q"
COM_UPDATE="makecache -q"
COM_INSTALL="install -y"
COM_REMOVE="remove -y"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
CHECK_CHAIN=("-q|")

# EL10 (RHEL/Rocky/Alma/CentOS 10, Fedora 40+) uses mpv-devel (canonical).
# EL9 also uses mpv-devel (provides mpv-libs-devel). EL8 uses mpv-libs-devel.
_major="${DISTRO##*-}"; _major="${_major%%.*}"
case "$_major" in
    9|10) _mpv="mpv-devel" ;;
    *)    _mpv="mpv-libs-devel" ;;
esac

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
mpv-dev:${_mpv}
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
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm"
RPM_RELEASE="1"
RPM_FILE_RELEASE="1"
RPM_GROUP=""
RPM_CHANGELOG_TAG=""
RPM_EXTRA_SPEC=""
RPM_OUTPUT_SUFFIX=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (RHEL-compatible)"

distro_setup_repos() {
    local _id="${DISTRO%%-*}"
    case "$_id" in
        rhel|rocky|almalinux|centos) ;;
        *) return 0 ;;   # fedora: mpv-devel is in main repositories
    esac

    local _pkg _url
    case "$_major" in
        8)    _pkg="rpmfusion-free-release"
              _url="https://download1.rpmfusion.org/free/el/rpmfusion-free-release-8.noarch.rpm" ;;
        9|10) _pkg="epel-release"
              _url="https://dl.fedoraproject.org/pub/epel/epel-release-latest-${_major}.noarch.rpm" ;;
        *)    return 0 ;;
    esac

    if ! deps_is_installed "$_pkg"; then
        warn "Build on $DISTRO needs $_pkg"
        if ask "Enable $_pkg? [Y/n]:" y; then
            deps_install "$_url" || error "Failed to enable $_pkg"
        else
            error "Cannot build without $_pkg"
        fi
    fi
}

distro_build() {
    prepare_staging && pack_rpm
}