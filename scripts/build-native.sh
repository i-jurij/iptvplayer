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
        --transform="flags=r;s,^,$PACKAGE_NAME-$VERSION/," . && cd - > /dev/null

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

# ---- Нативный .pkg.tar.zst ARCH ----
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

    # Артефакты CI (upload-artifact/download-artifact) теряют exec-бит.
    # Локально он есть, но chmod идемпотентен — просто подстрахуемся.
    chmod +x "$STAGING_DIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # -------------------------------------------------------------------------
    # Автоопределение runtime-зависимостей.
    #   ldd по бинарнику → список .so
    #   pacman -Fq       → пакет-владелец каждой .so
    #   минус glibc/gcc-libs (системные, не пакетные метаданные)
    #   плюс EXTRA_DEPS для того, что грузится через dlopen и ldd его не видит.
    # -------------------------------------------------------------------------
    local BIN="$STAGING_DIR/usr/bin/$PACKAGE_NAME"
    [ -x "$BIN" ] || { echo "[!] $BIN не исполняем" >&2; return 1; }

    # pacman -Fy требует root; если не root — БД файлов могла быть
    # синхронизирована раньше. Если её нет, автоопределение даст
    # пустой результат, и мы упадём на fallback-список.
    if [ "$(id -u)" -eq 0 ]; then
        pacman -Fy >/dev/null 2>&1 || true
    fi

    local libs_tmp pkgs_tmp
    libs_tmp=$(mktemp)
    pkgs_tmp=$(mktemp)

    ldd "$BIN" 2>/dev/null \
      | awk '
          /=>/ && $3 ~ /^\//  { print $3 }
          /^\t\//              { print $1 }
        ' \
      | sort -u > "$libs_tmp"

    : > "$pkgs_tmp"
    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        pacman -Fq "$lib" 2>/dev/null \
          | awk -F/ '{print $2}' >> "$pkgs_tmp" || true
    done < "$libs_tmp"

    local auto_deps
    auto_deps=$(sort -u "$pkgs_tmp" \
                | grep -vxE 'glibc|gcc-libs' \
                | grep -v '^$' \
                | tr '\n' ' ')
    rm -f "$libs_tmp" "$pkgs_tmp"

    case " $auto_deps " in
        *" jack2 "*|*" pipewire-jack "*)
            auto_deps=$(printf '%s' "$auto_deps" \
                | sed -e 's/\bjack2\b//g' -e 's/\bpipewire-jack\b//g' \
                | tr -s ' ')
            auto_deps="$auto_deps jack"
            ;;
    esac
    
    # dlopen-зависимости, которые ldd не видит.
    #   mesa        — OpenGL/Vulkan ICD (грузится через libGL/libvulkan)
    #   gdk-pixbuf2 — pixbuf-лоадеры для иконок (GdkPixbuf API)
    #   librsvg     — SVG-лоадер для gdk-pixbuf
    local extra_deps="mesa gdk-pixbuf2 librsvg"

    local all_deps="$auto_deps $extra_deps"

    # Fallback: если автоопределение дало <3 пакетов (например,
    # файловая БД pacman не синхронизирована и не root), используем
    # минимальный проверенный список.
    local auto_count
    auto_count=$(printf '%s\n' $auto_deps | grep -c .)
    if [ "$auto_count" -lt 3 ]; then
        echo "[i] автоопределение зависимостей дало $auto_count пакет(ов) — используем fallback-список"
        all_deps="mpv gtk3 gdk-pixbuf2 librsvg curl expat zlib mesa"
    fi

    echo "[i] auto-detected: $auto_deps"
    echo "[i] extras:        $extra_deps"
    echo "[i] final:         $all_deps"

    # -------------------------------------------------------------------------
    # PKGBUILD + makepkg.
    # Работаем в изолированном $workdir, чтобы makepkg не трогал
    # ни $STAGING_DIR, ни репозиторий.
    # -------------------------------------------------------------------------
    local workdir; workdir="$(mktemp -d)"

    # Копируем staging с сохранением прав. makepkg потом переустановит
    # права как надо при упаковке; нам важно только чтобы у builder
    # был доступ на чтение.
    cp -a "$STAGING_DIR/usr" "$workdir/staging_usr"

    local deps_array=""
    local d
    for d in $all_deps; do
        deps_array+="'$d' "
    done

    deps_array=$(printf '%s' "$deps_array" | sed \
        -e "s/'libjack\.so[^']*'/'jack'/g")

    cat > "$workdir/PKGBUILD" <<EOF
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=1
pkgdesc="IPTV Playlist Player"
arch=('$APPIMAGE_ARCH')
url="https://github.com/i-jurij/$PACKAGE_NAME"
license=('MIT')
# !debug: без отдельного -debug пакета с символами — он не нужен
# в релизе и удваивает размер артефактов.
options=('!debug')
depends=($deps_array)

package() {
    install -d "\$pkgdir/usr"
    cp -a "\$startdir/staging_usr/." "\$pkgdir/usr/"
}
EOF

    # makepkg отказывается работать от root. Если мы root — создаём
    # (идемпотентно) пользователя builder и отдаём ему workdir.
    local makepkg_cmd
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m builder 2>/dev/null || true
        chown -R builder:builder "$workdir"
        makepkg_cmd="runuser -u builder -- makepkg -f --nodeps --nocheck"
    else
        makepkg_cmd="makepkg -f --nodeps --nocheck"
    fi

    (
        cd "$workdir"
        # shellcheck disable=SC2086
        if ! $makepkg_cmd; then
            echo "[!] makepkg упал (см. вывод выше)" >&2
            exit 1
        fi
    ) || { cd "$PROJECT_ROOT"; rm -rf "$workdir"; return 1; }

    # Фильтруем debug-пакет — даже при options=('!debug') подстрахуемся.
    local built
    built=$(find "$workdir" -maxdepth 1 \
              -name '*.pkg.tar.zst' \
              ! -name '*-debug-*' \
              -print -quit)
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