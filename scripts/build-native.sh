#!/bin/bash
# =============================================================================
# build-native.sh – нативные .deb / .rpm / .pkg.tar.zst из системных библиотек
# =============================================================================
#
# Библиотека, не запускается напрямую.
# Сорсится из build-package.sh (см. scripts/common.sh для общего окружения).
#
# Требует установленных переменных окружения:
#   PROJECT_ROOT, STAGING_DIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME
#   VERSION, DEB_ARCH, RPM_ARCH, APPIMAGE_ARCH, DISTRO
# Все они выставляются в build-package.sh до вызова этих функций.
#
# Функции:
#   build_deb_native  — .deb из системных библиотек
#   build_rpm_native  — .rpm из системных библиотек
#   build_pkg_arch    — .pkg.tar.zst через makepkg (только на Arch)
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"

# ---- Нативный .deb ----
build_deb_native() {
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${DISTRO}_${DEB_ARCH}.deb"
    echo "[+] Создание нативного .deb..."
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_deb_native: не удалось подготовить staging" >&2
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
Section: network
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
        echo "[!] нативный .deb: dpkg-deb упал" >&2
        return 1
    fi
    echo "[✓] Нативный .deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
    return 0
}

# ---- Нативный .rpm ----
build_rpm_native() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${release}.${DISTRO}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$PROJECT_ROOT/pkg-rpm"
    echo "[+] Создание нативного .rpm..."
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_rpm_native: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    mkdir -p "$SPEC_DIR/SOURCES"
    cd "$STAGING_DIR" && tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
        --transform="s,^,$PACKAGE_NAME-$VERSION/," . && cd - > /dev/null

    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir $SPEC_DIR
Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $release
Summary:        IPTV Playlist Player
License:        MIT
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      $RPM_ARCH

%description
A simple player for M3U playlists with GUI.

%prep
%setup -q

%build
# already built

%install
rm -rf \$RPM_BUILD_ROOT
mkdir -p \$RPM_BUILD_ROOT
tar -xzf %{SOURCE0} -C \$RPM_BUILD_ROOT --strip-components=1

%files
%{_bindir}/$PACKAGE_NAME
%{_datadir}/$PACKAGE_NAME/
%{_datadir}/applications/$PACKAGE_NAME.desktop
%{_datadir}/icons/hicolor/scalable/apps/$ICON_NAME
%{_datadir}/metainfo/$METAINFO_NAME
%{_datadir}/doc/$PACKAGE_NAME/copyright
%{_datadir}/licenses/$PACKAGE_NAME/LICENSE

%post
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$release
- Initial build
EOF

    if ! rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"; then
        echo "[!] нативный .rpm: rpmbuild упал" >&2
        return 1
    fi
    if ! { mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"; }; then
        echo "[!] нативный .rpm: не найден собранный .rpm" >&2
        return 1
    fi
    echo "[✓] Нативный .rpm: $rpm_file"
    return 0
}

# ---- Нативный .pkg.tar.zst ----
build_pkg_arch() {
    # На не-Arch системах этот путь недоступен (меню его не предлагает).
    # Явный вызов --native-arch на Ubuntu/Debian — ошибка пользователя.
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg не найден. Сборка .pkg.tar.zst возможна только на Arch/Manjaro."
        return 1
    fi

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-1-${DISTRO}-${APPIMAGE_ARCH}.pkg.tar.zst"
    echo "[+] Создание нативного .pkg.tar.zst через makepkg..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_pkg_arch: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    local workdir; workdir="$(mktemp -d)"
    cd "$workdir"

    cat > PKGBUILD <<EOF
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=1
pkgdesc="IPTV Playlist Player"
arch=('$APPIMAGE_ARCH')
url="https://github.com/i-jurij/$PACKAGE_NAME"
license=('MIT')
depends=('mpv' 'gtk3' 'curl' 'expat' 'zlib')

package() {
    cp -a "$STAGING_DIR/usr" "\$pkgdir/"
}
EOF

    if ! makepkg -f --nodeps --nocheck; then
        echo "[!] makepkg упал (см. вывод выше)" >&2
        cd "$PROJECT_ROOT"; rm -rf "$workdir"; return 1
    fi

    local built
    built=$(find "$workdir" -maxdepth 1 -name "*.pkg.tar.zst" -print -quit)
    if [ -n "$built" ]; then
        mv "$built" "$pkg_file"
        echo "[✓] Нативный .pkg.tar.zst: $pkg_file"
    else
        echo "[!] makepkg не создал пакет" >&2
        cd "$PROJECT_ROOT"; rm -rf "$workdir"; return 1
    fi

    cd "$PROJECT_ROOT"
    rm -rf "$workdir"
    return 0
}