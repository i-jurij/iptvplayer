#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-deb.sh — библиотека." >&2; exit 1
fi

# Debian/Ubuntu/Mint/Pop — один адаптер.
build_deb_native() {
    build_deb_common
}