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
    echo "common-detect.sh — библиотека, не запускается напрямую." >&2
    exit 1
fi

detect_arch() {
    local machine; machine="$(uname -m)"
    case "$machine" in
        x86_64|amd64)          DEB_ARCH="amd64"; RPM_ARCH="x86_64";  APPIMAGE_ARCH="x86_64"  ;;
        aarch64|arm64)         DEB_ARCH="arm64"; RPM_ARCH="aarch64"; APPIMAGE_ARCH="aarch64" ;;
        armv7l|armv8l|armhf)   DEB_ARCH="armhf"; RPM_ARCH="armv7hl"; APPIMAGE_ARCH="armhf"   ;;
        i686|i386)             DEB_ARCH="i386";  RPM_ARCH="i686";    APPIMAGE_ARCH="i686"    ;;
        *)
            echo "[!] Неизвестная архитектура: $machine" >&2
            exit 1 ;;
    esac
    echo "[i] Архитектура: $machine → deb=$DEB_ARCH, rpm=$RPM_ARCH, appimage=$APPIMAGE_ARCH"
}

detect_distro() {
    if [ -n "${DISTRO:-}" ]; then
        echo "[i] DISTRO задан извне: $DISTRO"; return 0
    fi
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        local id="${ID:-unknown}" ver="${VERSION_ID:-}"
        if [ -n "$ver" ]; then DISTRO="${id}-${ver}"; else DISTRO="$id"; fi
    else
        DISTRO="unknown"
    fi
    echo "[i] DISTRO определён локально: $DISTRO"
}

detect_pkgmgr() {
    local id="${DISTRO%%-*}"
    case "$id" in
        ubuntu|debian|linuxmint|pop|astra)                    echo "apt"     ;;
        fedora|rocky|rhel|centos|almalinux|ol|oracle|redos|rosa) echo "dnf"  ;;
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