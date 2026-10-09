#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-oracle.sh — библиотека." >&2; exit 1
fi

# Oracle Linux — RHEL-совместимый, но со своим суффиксом .elN.
build_rpm_oracle() {
    local _major; _major="$(distro_major)"
    if [ -z "$_major" ]; then
        echo "[!] build_rpm_oracle: не удалось определить версию из DISTRO=$DISTRO" >&2
        return 1
    fi
    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-oracle"
    local RPM_RELEASE="1.el${_major}"
    local RPM_FILE_RELEASE="1.el${_major}"
    local RPM_OUTPUT_SUFFIX="el${_major}"
    build_rpm_common
}