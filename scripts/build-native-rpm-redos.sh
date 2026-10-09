#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-redos.sh — библиотека." >&2; exit 1
fi

# RedOS — RHEL-совместимый, суффикс .redN.
build_rpm_redos() {
    local _major; _major="$(distro_major)"
    if [ -z "$_major" ]; then
        echo "[!] build_rpm_redos: не удалось определить версию из DISTRO=$DISTRO" >&2
        return 1
    fi
    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-redos"
    local RPM_RELEASE="1.red${_major}"
    local RPM_FILE_RELEASE="1.red${_major}"
    local RPM_OUTPUT_SUFFIX="red${_major}"
    build_rpm_common
}