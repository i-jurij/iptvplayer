#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-openmandriva.sh — библиотека." >&2; exit 1
fi

# OpenMandriva: использует собственные макросы и disttag omv<ver>.
# Release: 1, суффикс omv<ver> в имени файла.
build_rpm_openmandriva() {
    local _ver; _ver="$(distro_major)"
    local _suffix="${DISTRO}"
    [ -n "$_ver" ] && _suffix="omv${_ver}"

    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-omv"
    local RPM_RELEASE="1"
    local RPM_FILE_RELEASE="1"
    local RPM_OUTPUT_SUFFIX="$_suffix"
    local RPM_GROUP="Video"
    local RPM_CHANGELOG_TAG="for OpenMandriva"
    build_rpm_common
}