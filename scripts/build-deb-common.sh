#!/usr/bin/env bash
# =============================================================================
# build-deb-common.sh – общий движок сборки .deb
# =============================================================================
# Библиотека. Сорсится из build-package.sh до адаптера build-native-deb.sh.
# Конфигурация адаптера:
#   DEB_OUTPUT_SUFFIX   (опц.) суффикс имени .deb; default = $DISTRO
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-deb-common.sh — библиотека." >&2; exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

build_deb_common() {
    : "${PROJECT_ROOT:?}" "${STAGING_DIR:?}" "${OUTPUT_DIR:?}"
    : "${PACKAGE_NAME:?}" "${VERSION:?}" "${DEB_ARCH:?}" "${DISTRO:?}"

    local suffix="${DEB_OUTPUT_SUFFIX:-$DISTRO}"
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${suffix}_${DEB_ARCH}.deb"

    echo "[+] Создание нативного .deb..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_deb_common: не удалось подготовить staging" >&2
            return 1
        fi
    fi
    mkdir -p "$STAGING_DIR/DEBIAN"

    local depends
    depends=$(detect_deb_depends "$STAGING_DIR/usr/bin/$PACKAGE_NAME")
    if [ -z "$depends" ]; then
        echo "[!] не удалось определить Depends" >&2
        return 1
    fi
    echo "[+] Depends: $depends"

    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: video
Priority: optional
Architecture: $DEB_ARCH
Depends: $depends
Maintainer: ijurij <mnisjil@duck.com>
Homepage: https://github.com/i-jurij/$PACKAGE_NAME
Description: IPTV Playlist Player
 A simple player for M3U playlists with GUI.
EOF

    cat > "$STAGING_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/postinst"

    cat > "$STAGING_DIR/DEBIAN/prerm" << EOF
#!/bin/bash
set -e
if [ \$1 = "remove" ] || [ \$1 = "purge" ]; then
    rm -f "/usr/share/applications/$PACKAGE_NAME.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/prerm"

    chmod -R 755 "$STAGING_DIR/usr"
    chmod 755 "$STAGING_DIR/DEBIAN"
    if ! dpkg-deb -Zxz --build --root-owner-group "$STAGING_DIR" "$deb_file"; then
        echo "[!] dpkg-deb упал" >&2
        return 1
    fi
    echo "[✓] Нативный .deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
    return 0
}