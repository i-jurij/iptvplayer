# distro-patterns: redos*
#
# RedOS - RHEL-compatible, .redN release suffix.

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
mpv-dev:mpv-libs-devel
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
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-redos"
RPM_RELEASE="1.red${_major}"
RPM_FILE_RELEASE="1.red${_major}"
RPM_OUTPUT_SUFFIX="red${_major}"
RPM_GROUP=""
RPM_CHANGELOG_TAG=""
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (RedOS)"

distro_setup_repos() {
    return 0
}

distro_build() {
    prepare_staging && pack_rpm
}