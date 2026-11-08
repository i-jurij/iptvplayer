# distro-patterns: alt* altlinux*
#
# ALT Linux - apt-rpm. Release: alt1. Group is required.
# %post is empty (no update-icon-caches on ALT).

PKGMGR=apt-get
PKGMGR_CHECK=rpm
COM_CHECK="-q"
COM_UPDATE="update"
COM_INSTALL="install -y"
COM_REMOVE="remove -y --auto-remove"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE="upgrade -y"
OPTS_UPGRADE=""
CHECK_CHAIN=("-q|")

DEPS="
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
gettext:gettext
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
"

REQUIRED_TOOLS=("rpmbuild:rpm-build" "rpm:rpm" "tar:tar" "readelf:binutils")

PACK_FORMAT=rpm
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-alt"
RPM_RELEASE="alt1"
RPM_FILE_RELEASE="alt1"
RPM_GROUP="Video"
RPM_CHANGELOG_TAG="for ALT Linux"
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (ALT Linux)"

distro_build() {
    prepare_staging && pack_rpm
}