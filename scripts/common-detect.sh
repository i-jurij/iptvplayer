#!/usr/bin/env bash
# =============================================================================
# common-detect.sh – единое определение ОС, пакетного менеджера, архитектуры
# =============================================================================
# Библиотека. Сорсится из common.sh. Единственная точка правды для
# detect_distro / detect_pkgmgr / detect_arch. Заполняет DISTRO,
# DEB_ARCH, RPM_ARCH, APPIMAGE_ARCH.
#
# detect_pkgmgr печатает в stdout один из:
#   apt, dnf, zypper, apt-rpm, pacman, apk, xbps, portage, nix, unknown
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "common-detect.sh is a library, do not run directly." >&2
    exit 1
fi

# =============================================================================
# Цвета и логирование.
# =============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log()     { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1" >&2; }
error()   { echo -e "${RED}[ERROR]${NC} $1" >&2; exit 1; }
section() { echo -e "\n${BLUE}═══════════════════════════════════════${NC}\n${BLUE}$1${NC}\n${BLUE}═══════════════════════════════════════${NC}\n"; }

detect_arch() {
    local machine; machine="$(uname -m)"
    local _inject_flags=1
    local _host_arch_mismatch=0
    local _march="" _mtune="-mtune=generic"
    local _deb="" _rpm="" _appimage=""

    # macOS/Apple: Apple clang already targets the native arch.
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
        *) echo "[!] Unknown architecture: $machine" >&2; exit 1 ;;
    esac

    # Cross-packaging: the user set a package arch different from the
    # host arch. Respect it — but then CFLAGS/CXXFLAGS must also be set
    # by the user, otherwise the binary will be built for the host.
    [ -n "${DEB_ARCH:-}" ]       && [ "$DEB_ARCH"       != "$_deb"      ] && _host_arch_mismatch=1
    [ -n "${RPM_ARCH:-}" ]       && [ "$RPM_ARCH"       != "$_rpm"      ] && _host_arch_mismatch=1
    [ -n "${APPIMAGE_ARCH:-}" ]  && [ "$APPIMAGE_ARCH"  != "$_appimage" ] && _host_arch_mismatch=1

    local _had_cflags="${CFLAGS:-}"
    local _had_cxxflags="${CXXFLAGS:-}"

    : "${DEB_ARCH:=$_deb}"
    : "${RPM_ARCH:=$_rpm}"
    : "${APPIMAGE_ARCH:=$_appimage}"

    ARCH_MARCH_FLAG="$_march"
    ARCH_MTUNE_FLAG="$_mtune"

    # Don't inject host flags during cross-packaging — they would
    # contradict the explicitly set package arch. Flags must be
    # provided by the user.
    if [ "$_inject_flags" = 1 ] && [ "$_host_arch_mismatch" = 0 ]; then
        local _flags="${_march} ${_mtune}"
        _flags="${_flags# }"
        [ -z "$_had_cflags" ]   && [ -n "$_flags" ] && export CFLAGS="$_flags"
        [ -z "$_had_cxxflags" ] && [ -n "$_flags" ] && export CXXFLAGS="$_flags"
    fi

    # Diagnostics. Cross-arch variables are set, but compiler flags
    # and cross-CC are not — the binary will almost certainly be built
    # for the host inside a package marked for a different arch.
    if [ "$_host_arch_mismatch" = 1 ] && [ -z "$_had_cflags" ] && [ -z "$_had_cxxflags" ]; then
        warn "════════════════════════════════════════════════════════════════"
        warn "Cross-packaging detected: DEB/RPM/APPIMAGE_ARCH differ"
        warn "from the host ($machine), but CFLAGS/CXXFLAGS are not set."
        warn "The binary will be built for $machine while the package is"
        warn "tagged for the cross-architecture. Set target-arch flags:"
        warn "  export CFLAGS='--target=<triplet> -march=<arch> -mtune=generic'"
        warn "  export CXXFLAGS=\"\$CFLAGS\""
        warn "  export CC=<triplet>-gcc CXX=<triplet>-g++"
        warn "Host-flag injection is disabled to avoid masking the problem."
        warn "════════════════════════════════════════════════════════════════"
    fi

    echo "[i] Architecture: $machine → deb=$DEB_ARCH, rpm=$RPM_ARCH, appimage=$APPIMAGE_ARCH"
    [ -n "${CFLAGS:-}" ]   && echo "[i] CFLAGS='${CFLAGS}'"
    [ -n "${CXXFLAGS:-}" ] && echo "[i] CXXFLAGS='${CXXFLAGS}'"
}

detect_distro() {
    # Cross-packaging: if DISTRO is set in the environment, use it
    # without consulting /etc/os-release. This allows building packages
    # for another distribution (provided its tools are available on
    # the host or inside an appropriate chroot/container).
    if [ -n "${DISTRO:-}" ]; then
        echo "[i] DISTRO setup: $DISTRO"; return 0
    fi
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        local id="${ID:-unknown}" ver="${VERSION_ID:-}"
        if [ -n "$ver" ]; then DISTRO="${id}-${ver}"; else DISTRO="$id"; fi
    else
        DISTRO="unknown"
    fi
    echo "[i] DISTRO check local: $DISTRO"
}

detect_pkgmgr() {
    local id="${DISTRO%%-*}"
    case "$id" in
        ubuntu|debian|linuxmint|pop|astra)                    echo "apt"     ;;
        fedora|rocky|rhel|centos|almalinux|ol|oracle|redos|rosa|openmandriva|mageia) echo "dnf"  ;;
        opensuse|sles)                                        echo "zypper"  ;;
        alt|altlinux)                                         echo "apt-rpm" ;;
        arch|manjaro|endeavouros|cachyos)                     echo "pacman"  ;;
        alpine)                                               echo "apk"     ;;
        void)                                                 echo "xbps"    ;;
        gentoo)                                               echo "portage" ;;
        nixos)                                                echo "nix"     ;;
        *)                                                    echo "unknown" ;;
    esac
}

# Мажорная версия из DISTRO: "ubuntu-24.04" → "24"; "ubuntu" → ""
distro_major() {
    local _v="${DISTRO#*-}"
    [ "$_v" = "$DISTRO" ] && { echo ""; return; }
    echo "${_v%%.*}"
}

# ID без версии: "ubuntu-24.04" → "ubuntu"
distro_id() { echo "${DISTRO%%-*}"; }

# distro_is 'fedora-*' 'rocky-*' → 0 если DISTRO матчит любой шаблон
distro_is() {
    local _p
    for _p in "$@"; do
        # shellcheck disable=SC2254
        case "$DISTRO" in $_p) return 0 ;; esac
    done
    return 1
}

detect_sudo() {
    if [ "$(id -u)" = "0" ]; then
        SUDO=""
        return 0
    fi
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
        return 0
    fi
    error "sudo is not installed and the user is not root. Run as root, install sudo, or install packages manually and re-run."
}
