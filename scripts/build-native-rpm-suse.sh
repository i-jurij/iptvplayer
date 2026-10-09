#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-suse.sh — библиотека." >&2; exit 1
fi

# openSUSE/SLES. Release 0.suseN — SUSE-конвенция, .suseN в имени файла.
# %post: gtk-update-icon-cache (Debian-имя update-icon-caches здесь нет).
build_rpm_suse() {
    # DISTRO бывает: opensuse-tumbleweed-YYYYMMDD, opensuse-leap-15.5, sles-15.5.
    # distro_major() здесь даёт мусор (leap-15, tumbleweed-2024…), поэтому
    # разбираем явно.
    local _ver=""
    case "$DISTRO" in
        opensuse-leap-*)       _ver="${DISTRO#opensuse-leap-}";       _ver="${_ver%%.*}" ;;
        opensuse-tumbleweed-*) _ver="${DISTRO#opensuse-tumbleweed-}" ;;
        sles-*)                _ver="${DISTRO#sles-}";                _ver="${_ver%%.*}" ;;
        opensuse-*)            _ver="${DISTRO#opensuse-}";            _ver="${_ver%%.*}" ;;
    esac

    local _release="0" _suffix=""
    if [ -n "$_ver" ]; then
        _release="0.suse${_ver}"
        _suffix="suse${_ver}"
    fi

    local RPM_POST_BODY
    RPM_POST_BODY=$(cat <<'EOF'
if [ -x /usr/bin/gtk-update-icon-cache ]; then
    /usr/bin/gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
EOF
)

    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-suse"
    local RPM_RELEASE="$_release"
    local RPM_FILE_RELEASE="$_release"
    local RPM_OUTPUT_SUFFIX="${_suffix:-$DISTRO}"
    local RPM_GROUP="Productivity/Multimedia/Video"
    build_rpm_common
}