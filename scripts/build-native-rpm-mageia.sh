#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-mageia.sh — библиотека." >&2; exit 1
fi

# Mageia: Release с суффиксом .mgaN (Mageia-конвенция %mkrel).
# Group исторически обязателен.
build_rpm_mageia() {
    local _ver; _ver="$(distro_major)"
    local _release="1"
    [ -n "$_ver" ] && _release="1.mga${_ver}"

    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-mageia"
    local RPM_RELEASE="$_release"
    local RPM_FILE_RELEASE="$_release"
    local RPM_GROUP="Video"
    local RPM_CHANGELOG_TAG="for Mageia"
    build_rpm_common
}