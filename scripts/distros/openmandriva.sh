# distro-patterns: openmandriva*
#
# OpenMandriva Lx - dnf. Release: 1. Suffix: omv<ver>. Group required.
# lib64mpv-devel is in the "restricted" repo; it must be enabled first.

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

_ver="$(distro_major)"
_suffix="$DISTRO"
[ -n "$_ver" ] && _suffix="omv${_ver}"

_pfx="lib64"
case "$(uname -m)" in i686|i386) _pfx="lib" ;; esac

DEPS="
build-essential:gcc gcc-c++ make glibc-devel llvm gettext
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
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
gl-dev:${_pfx}gl-devel
egl-dev:${_pfx}egl-devel
png-dev:${_pfx}png-devel
jpeg-dev:${_pfx}jpeg-devel
webp-dev:${_pfx}webp-devel
zlib-dev:${_pfx}z-devel
expat-dev:${_pfx}expat-devel
freetype-dev:${_pfx}freetype6-devel
"

REQUIRED_TOOLS=("rpmbuild:rpm-build" "rpm:rpm" "tar:tar" "readelf:binutils")

PACK_FORMAT=rpm
RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-omv"
RPM_RELEASE="1"
RPM_FILE_RELEASE="1"
RPM_OUTPUT_SUFFIX="$_suffix"
RPM_GROUP="Video"
RPM_CHANGELOG_TAG="for OpenMandriva"
RPM_EXTRA_SPEC=""
RPM_DESC_EXTRA=""

NATIVE_LABEL="Native .rpm (OpenMandriva)"

_ensure_restricted_repo() {
    if dnf repolist --enabled 2>/dev/null | grep -q 'restricted'; then
        return 0
    fi
    local _ids
    _ids=$(dnf repolist --disabled 2>/dev/null \
           | awk '/restricted/ {print $1}' \
           | tr '\n' ' ')
    [ -z "$_ids" ] && return 0

    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        log "CI: enabling restricted repo: ${_ids% }"
    else
        warn "The 'restricted' repository is needed for lib64mpv-devel."
        warn "It contains packages with license/patent restrictions;"
        warn "ensure their use is legal in your jurisdiction."
        if ! ask "Enable restricted (${_ids% }) now? [Y/n]:" y; then
            error "Without 'restricted' lib64mpv-devel cannot be installed. Use om-repo-picker."
        fi
    fi

    # shellcheck disable=SC2086
    $SUDO dnf config-manager --enable $_ids \
        || error "Failed to enable restricted repo (${_ids% }). Use om-repo-picker."
    log "restricted enabled: ${_ids% }"
}

distro_setup_repos() {
    _ensure_restricted_repo
}

distro_build() {
    prepare_staging && pack_rpm
}