#!/usr/bin/env bash

# Building .apk is only possible inside an Alpine environment
# (musl, abuild). Cross-building .apk is not supported: a binary
# compiled against glibc will not run on musl. To build .apk for
# another arch, run this script inside an Alpine container of the
# corresponding architecture
# (e.g.: docker run --platform linux/arm64 -v "$PWD:/src" alpine:edge).

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-apk-alpine.sh — only library, run build-package.sh." >&2; exit 1
fi

# soname-зависимости abuild резолвит автоматически; явные gtk+3.0 и mpv
# нужны для runtime-файлов (схемы gsettings, кодеки mpv), не привязанных
# к конкретной .so.
build_apk_alpine() {
    local APK_PKGREL="0"
    # Alpine-native arch names. armv7l→armv7, i686→x86 — это
    # Alpine-специфичные переименования; остальные (x86_64, aarch64,
    # ppc64le, s390x, riscv64) совпадают с uname -m.
    local APK_ARCH
    case "$(uname -m)" in
        armv7l|armv8l) APK_ARCH="armv7" ;;
        i686|i386)     APK_ARCH="x86"   ;;
        *)             APK_ARCH="$(uname -m)" ;;
    esac
    local APK_DEPENDS="gtk+3.0 mpv"
    build_apk_common
}