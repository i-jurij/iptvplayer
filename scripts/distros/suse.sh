# distro-patterns: opensuse* sles*
#
# openSUSE / SLES. Release 0.suseN. %post uses gtk-update-icon-cache
# (update-icon-caches is not available here).

PKGMGR="zypper --non-interactive"
PKGMGR_CHECK=rpm
COM_CHECK="-q"
COM_UPDATE="refresh"
COM_INSTALL="install -y"
COM_REMOVE="remove -y --clean-deps"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE="update -y"
OPTS_UPGRADE=""
CHECK_CHAIN=("-q|")

# Explicit parse: distro_major() would give garbage for tumbleweed.
_ver=""
case "$DISTRO" in
    opensuse-leap-*)       _ver="${DISTRO#opensuse-leap-}";       _ver="${_ver%%.*}" ;;
    opensuse-tumbleweed-*) _ver="${DISTRO#opensuse-tumbleweed-}" ;;
    sles-*)                _ver="${DISTRO#sles-}";                _ver="${_ver%%.*}" ;;
    opensuse-*)            _ver="${DISTRO#opensuse-}";            _ver="${_ver%%.*}" ;;
esac

_release="0"
_suffix=""
if [ -n "$_ver" ]; then
    _release="0.suse${_ver}"
    _suffix="suse${_ver}"
fi

DEPS="
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
gettext-runtime:gettext-runtime
"

REQUIRED_TOOLS=("rpmbuild:rpm-build" "rpm:rpm" "tar:tar" "readelf:binutils")

PACK_FORMAT=rpm
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-suse"
RPM_RELEASE="$_release"
RPM_FILE_RELEASE="$_release"
RPM_OUTPUT_SUFFIX="${_suffix:-$DISTRO}"
RPM_GROUP="Productivity/Multimedia/Video"
RPM_CHANGELOG_TAG=""
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (openSUSE/SLES)"

distro_build() {
    prepare_staging && pack_rpm
}