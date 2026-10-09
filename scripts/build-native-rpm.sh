#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm.sh — библиотека, не запускается напрямую." >&2
    exit 1
fi

# Fedora/RHEL/Rocky/AlmaLinux/CentOS — стандартный %{?dist}.
build_rpm_native() {
    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm"
    local RPM_RELEASE="1"
    local RPM_FILE_RELEASE="1"
    build_rpm_common
}