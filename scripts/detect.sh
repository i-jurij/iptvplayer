#!/usr/bin/env bash
# =============================================================================
# detect.sh - OS, architecture, sudo, colors, logging.
# =============================================================================
# Library. Sourced from make.sh before lib.sh.
#
# detect_distro reads DISTRO from env or /etc/os-release, then searches
# $SCRIPT_DIR/distros/*.sh by the "# distro-patterns:" header. The first
# matching file is sourced; DISTRO_ADAPTER = filename without .sh.
# If no file matches, DISTRO_ADAPTER stays empty and the caller decides
# whether that is an error (deps/package) or acceptable (clean).
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "detect.sh is a library, do not run directly." >&2
    exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;36m'
NC='\033[0m'

log()     { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1" >&2; }
error()   { echo -e "${RED}[ERROR]${NC} $1" >&2; exit 1; }
section() { echo -e "\n${BLUE}============================================================${NC}"; \
            echo -e "${BLUE}== $1${NC}"; \
            echo -e "${BLUE}============================================================${NC}\n"; }

# -----------------------------------------------------------------------------
# detect_arch - set DEB_ARCH, RPM_ARCH, APPIMAGE_ARCH from uname -m.
# Injects -march/-mtune into CFLAGS/CXXFLAGS for native builds.
# Honors pre-set values (cross-packaging).
# -----------------------------------------------------------------------------
detect_arch() {
    local machine; machine="$(uname -m)"
    local _inject_flags=1 _host_arch_mismatch=0
    local _march="" _mtune="-mtune=generic"
    local _deb="" _rpm="" _appimage=""

    case "$(uname -s)" in
        Darwin) _inject_flags=0 ;;
    esac

    case "$machine" in
        x86_64|amd64)              _deb=amd64;   _rpm=x86_64;   _appimage=x86_64;   _march=-march=x86-64 ;;
        aarch64|arm64)             _deb=arm64;   _rpm=aarch64;  _appimage=aarch64;  _march=-march=armv8-a ;;
        armv7l|armv7|armv8l|armhf) _deb=armhf;   _rpm=armv7hl;  _appimage=armhf;    _march=-march=armv7-a ;;
        i686|i386)                 _deb=i386;    _rpm=i686;     _appimage=i686;     _march=-march=i686 ;;
        ppc64le)                   _deb=ppc64el; _rpm=ppc64le;  _appimage=ppc64le ;;
        ppc64)                     _deb=ppc64;   _rpm=ppc64;    _appimage=ppc64 ;;
        s390x)                     _deb=s390x;   _rpm=s390x;    _appimage=s390x ;;
        riscv64)                   _deb=riscv64; _rpm=riscv64;  _appimage=riscv64;  _march=-march=rv64gc ;;
        mips64|mips64el)           _deb=$machine; _rpm=$machine; _appimage=$machine; _march=-march=mips64 ;;
        sparc64)                   _deb=sparc64; _rpm=sparc64;  _appimage=sparc64 ;;
        *) error "Unknown architecture: $machine" ;;
    esac

    [ -n "${DEB_ARCH:-}" ]      && [ "$DEB_ARCH"      != "$_deb"      ] && _host_arch_mismatch=1
    [ -n "${RPM_ARCH:-}" ]      && [ "$RPM_ARCH"      != "$_rpm"      ] && _host_arch_mismatch=1
    [ -n "${APPIMAGE_ARCH:-}" ] && [ "$APPIMAGE_ARCH" != "$_appimage" ] && _host_arch_mismatch=1

    local _had_cflags="${CFLAGS:-}" _had_cxxflags="${CXXFLAGS:-}"

    : "${DEB_ARCH:=$_deb}"
    : "${RPM_ARCH:=$_rpm}"
    : "${APPIMAGE_ARCH:=$_appimage}"

    ARCH_MARCH_FLAG="$_march"
    ARCH_MTUNE_FLAG="$_mtune"

    if [ "$_inject_flags" = 1 ] && [ "$_host_arch_mismatch" = 0 ]; then
        local _flags="${_march} ${_mtune}"
        _flags="${_flags# }"
        [ -z "$_had_cflags" ]   && [ -n "$_flags" ] && export CFLAGS="$_flags"
        [ -z "$_had_cxxflags" ] && [ -n "$_flags" ] && export CXXFLAGS="$_flags"
    fi

    if [ "$_host_arch_mismatch" = 1 ] && [ -z "$_had_cflags" ] && [ -z "$_had_cxxflags" ]; then
        warn "Cross-packaging: DEB/RPM/APPIMAGE_ARCH differ from host ($machine), but CFLAGS/CXXFLAGS are not set."
        warn "The binary will be built for $machine while the package is tagged for a different arch."
        warn "Set target-arch flags:"
        warn "  export CFLAGS='--target=<triplet> -march=<arch> -mtune=generic'"
        warn "  export CXXFLAGS=\"\$CFLAGS\""
        warn "  export CC=<triplet>-gcc CXX=<triplet>-g++"
    fi

    log "Architecture: $machine -> deb=$DEB_ARCH, rpm=$RPM_ARCH, appimage=$APPIMAGE_ARCH"
    [ -n "${CFLAGS:-}" ]   && log "CFLAGS='${CFLAGS}'"
    [ -n "${CXXFLAGS:-}" ] && log "CXXFLAGS='${CXXFLAGS}'"
    return 0
}

# -----------------------------------------------------------------------------
# detect_distro - set DISTRO, source the matching distros/*.sh, set DISTRO_ADAPTER.
# -----------------------------------------------------------------------------
detect_distro() {
    if [ -z "${DISTRO:-}" ]; then
        if [ -r /etc/os-release ]; then
            # shellcheck disable=SC1091
            . /etc/os-release
            local id="${ID:-unknown}" ver="${VERSION_ID:-}"
            if [ -n "$ver" ]; then DISTRO="${id}-${ver}"; else DISTRO="$id"; fi
        else
            DISTRO="unknown"
        fi
    fi

    local _d="$SCRIPT_DIR/distros"
    local f patterns p
    for f in "$_d"/*.sh; do
        [ -f "$f" ] || continue
        patterns="$(sed -n 's/^#[[:space:]]*distro-patterns:[[:space:]]*//p' "$f" | head -n1)"
        [ -n "$patterns" ] || continue
        for p in $patterns; do
            # shellcheck disable=SC2254
            case "$DISTRO" in $p)
                # shellcheck disable=SC1090
                . "$f"
                DISTRO_ADAPTER="${f##*/}"
                DISTRO_ADAPTER="${DISTRO_ADAPTER%.sh}"
                log "DISTRO: $DISTRO (adapter: $DISTRO_ADAPTER)"
                return 0
                ;;
            esac
        done
    done

    log "DISTRO: $DISTRO (no adapter in distros/)"
    DISTRO_ADAPTER=""
    return 0
}

# "ubuntu-24.04" -> "24"; "ubuntu" -> ""
distro_major() {
    local _v="${DISTRO#*-}"
    [ "$_v" = "$DISTRO" ] && { echo ""; return; }
    echo "${_v%%.*}"
}

# "ubuntu-24.04" -> "ubuntu"
distro_id() {
    echo "${DISTRO%%-*}"
}

# -----------------------------------------------------------------------------
# detect_sudo - set SUDO to "" (root) or "sudo" (non-root with sudo).
# -----------------------------------------------------------------------------
detect_sudo() {
    if [ "$(id -u)" = "0" ]; then
        SUDO=""
        return 0
    fi
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
        return 0
    fi
    error "sudo is not installed and the user is not root. Run as root, install sudo, or install packages manually."
}