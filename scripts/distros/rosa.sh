# distro-patterns: rosa*
#
# ROSA Linux - dnf. Release: 1.<platform>. Group required.
# Spec release is long; file release is short (1).

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

_rosa_platform=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    _rosa_platform="${ROSA_OS_PLATFORM:-}"
    if [ -z "$_rosa_platform" ] && [ -n "${ID:-}" ] && [ -n "${VERSION_ID:-}" ]; then
        _rosa_platform="${ID}${VERSION_ID}"
    fi
fi
_spec_release="1"
[ -n "$_rosa_platform" ] && _spec_release="1.${_rosa_platform}"

_pfx="lib64"
case "$(uname -m)" in i686|i386) _pfx="lib" ;; esac

DEPS="
build-essential:gcc-c++ make glibc-devel
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
pkg-config:pkgconf
wget:wget
curl:curl
curl-dev:${_pfx}curl-devel
mpv-dev:${_pfx}mpv-devel
rapidjson-dev:rapidjson-devel
gtk3-dev:${_pfx}gtk+3.0-devel
rsvg-common:librsvg2
x11-dev:${_pfx}x11-devel
x11-xcb-dev:${_pfx}xcb-devel
gl-dev:${_pfx}gl-devel
egl-dev:${_pfx}egl-devel
png-dev:${_pfx}png-devel
jpeg-dev:${_pfx}jpeg-devel
webp-dev:${_pfx}webp-devel
zlib-dev:${_pfx}zlib-devel
expat-dev:${_pfx}expat-devel
freetype-dev:${_pfx}freetype6-devel
"

REQUIRED_TOOLS=("rpmbuild:rpm-build" "rpm:rpm" "tar:tar" "readelf:binutils")

PACK_FORMAT=rpm
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-rosa"
RPM_RELEASE="$_spec_release"
RPM_FILE_RELEASE="1"
RPM_GROUP="Video"
RPM_CHANGELOG_TAG="for ROSA Linux"
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (ROSA Linux)"

distro_build() {
    prepare_staging && pack_rpm
}