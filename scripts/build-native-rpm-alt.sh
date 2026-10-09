#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-alt.sh — библиотека." >&2; exit 1
fi

# ALT Linux: apt-rpm, но упаковка — rpmbuild + spec.
# Release: alt1, alt2, ...  Группы в spec не требуется.
build_rpm_alt() {
    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-alt"
    local RPM_RELEASE="alt1"
    local RPM_FILE_RELEASE="alt1"
    local RPM_CHANGELOG_TAG="for ALT Linux"
    build_rpm_common
}