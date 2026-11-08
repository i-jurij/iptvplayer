# distro-patterns: mageia*
#
# Mageia - dnf. Release: 1.mgaN. Group required.

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

_ver="$(distro_major)"
_release="1"
[ -n "$_ver" ] && _release="1.mga${_ver}"

_pfx="lib64"
case "$(uname -m)" in i686|i386) _pfx="lib" ;; esac

DEPS="
build-essential:gcc gcc-c++ make glibc-devel
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gettext:gettext
gnupg:gnupg2
pkg-config:pkgconf
wget:wget
curl:curl
curl-dev:${_pfx}curl-devel
mpv-dev:${_pfx}mpv-devel
rapidjson-dev:rapidjson
gtk3-dev:${_pfx}gtk+3.0-devel
rsvg-common:librsvg2
x11-dev:${_pfx}x11-devel
x11-xcb-dev:${_pfx}xcb-devel
gl-dev:${_pfx}mesagl-devel
egl-dev:${_pfx}mesaegl-devel
png-dev:${_pfx}png-devel
jpeg-dev:${_pfx}jpeg-devel
webp-dev:${_pfx}webp-devel
zlib-dev:${_pfx}zlib-devel
expat-dev:${_pfx}expat-devel
freetype-dev:${_pfx}freetype2-devel
"

REQUIRED_TOOLS=("rpmbuild:rpm-build" "rpm:rpm" "tar:tar" "readelf:binutils")

PACK_FORMAT=rpm
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-mageia"
RPM_RELEASE="$_release"
RPM_FILE_RELEASE="$_release"
RPM_GROUP="Video"
RPM_CHANGELOG_TAG="for Mageia"
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (Mageia)"

distro_build() {
    prepare_staging && pack_rpm
}