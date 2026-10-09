#!/usr/bin/env bash
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-apk-alpine.sh — библиотека." >&2; exit 1
fi

# Alpine Linux: musl, abuild, APKBUILD.
# soname-зависимости abuild резолвит автоматически; явные gtk+3.0 и mpv
# нужны для runtime-файлов (схемы gsettings, кодеки mpv), не привязанных
# к конкретной .so.
build_apk_alpine() {
    local APK_PKGREL="0"
    local APK_ARCH="x86_64 aarch64"
    local APK_DEPENDS="gtk+3.0 mpv"
    build_apk_common
}