#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rpm-rosa.sh — библиотека." >&2; exit 1
fi

# ROSA Linux: dnf, релиз 1.rosa<platform>. Группа обязательна исторически.
# В spec — расширенный Release, в имени файла — короткий.
build_rpm_rosa() {
    local rosa_platform=""
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        rosa_platform="${ROSA_OS_PLATFORM:-}"
        [ -z "$rosa_platform" ] && [ -n "${ID:-}" ] && [ -n "${VERSION_ID:-}" ] \
            && rosa_platform="${ID}${VERSION_ID}"
    fi
    local spec_release="1"
    [ -n "$rosa_platform" ] && spec_release="1.${rosa_platform}"

    local RPM_SPEC_DIR="$PROJECT_ROOT/pkg-rpm-rosa"
    local RPM_RELEASE="$spec_release"
    local RPM_FILE_RELEASE="1"
    local RPM_GROUP="Video"
    local RPM_CHANGELOG_TAG="for ROSA Linux"
    build_rpm_common
}