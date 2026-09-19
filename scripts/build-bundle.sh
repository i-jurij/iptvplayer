#!/bin/bash
# =============================================================================
# build-bundle.sh – AppImage (linuxdeploy) + bundled .deb / .rpm
# =============================================================================
#
# Библиотека, не запускается напрямую.
# Сорсится из build-package.sh.
#
# Зависит от scripts/common.sh (log/warn/error, prepare_staging,
# detect_deb_depends) и от следующих переменных окружения, устанавливаемых
# в build-package.sh:
#   PROJECT_ROOT, SCRIPT_DIR
#   STAGING_DIR, APPDIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME, BUNDLE_PREFIX
#   APPIMAGE_ARCH, DEB_ARCH, RPM_ARCH
#   VERSION, VERSION_FILE
#
# Функции:
#   populate_appdir       — наполняет $APPDIR через linuxdeploy + GTK-плагин
#   build_appimage        — упаковывает $APPDIR в .AppImage через appimagetool
#   build_bundled_stage   — готовит $STAGING_DIR из готового $APPDIR
#   build_deb_bundled     — bundled .deb
#   build_rpm_bundled     — bundled .rpm
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-bundle.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"

# =============================================================================
#                         НАПОЛНЕНИЕ APPDIR (linuxdeploy)
# =============================================================================
# Один раз наполняем $APPDIR. Результат переиспользуется для AppImage и
# для bundled-пакетов.
# =============================================================================
populate_appdir() {
    if [[ "$APPIMAGE_ARCH" != "x86_64" && "$APPIMAGE_ARCH" != "aarch64" ]]; then
        echo "[!] Bundled/AppImage не поддерживаются для '$APPIMAGE_ARCH'."
        return 1
    fi

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/scalable/apps"

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] populate_appdir: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    cp "$STAGING_DIR/usr/bin/$PACKAGE_NAME" "$APPDIR/usr/bin/"
    cp -r "$STAGING_DIR/usr/share/$PACKAGE_NAME" "$APPDIR/usr/share/"
    cp "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" "$APPDIR/usr/share/applications/"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" "$APPDIR/$ICON_NAME"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

    if [ -f "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$APPDIR/usr/share/metainfo"
        cp "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" "$APPDIR/usr/share/metainfo/"
    fi

    local LINUXDEPLOY="$SCRIPT_DIR/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    local GTK_PLUGIN="$SCRIPT_DIR/linuxdeploy-plugin-gtk.sh"

    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "[+] Скачивание linuxdeploy ($APPIMAGE_ARCH)..."
        if ! download \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" \
            "$LINUXDEPLOY"; then
            echo "[!] не удалось скачать linuxdeploy" >&2
            return 1
        fi
        chmod +x "$LINUXDEPLOY"
    fi
    if [ ! -f "$GTK_PLUGIN" ]; then
        echo "[+] Скачивание GTK-плагина (скрипт)..."
        if ! download \
            "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
            "$GTK_PLUGIN"; then
            echo "[!] не удалось скачать GTK-плагин" >&2
            return 1
        fi
        chmod +x "$GTK_PLUGIN"
    fi

    echo "[+] Наполнение AppDir через linuxdeploy..."
    if ! APPIMAGE_EXTRACT_AND_RUN=1 DEPLOY_GTK_VERSION=3 ARCH="$APPIMAGE_ARCH" "$LINUXDEPLOY" \
        --appdir="$APPDIR" \
        --plugin gtk \
        --desktop-file="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"; then
        echo "[!] Ошибка linuxdeploy." >&2
        return 1
    fi

    chmod +x "$APPDIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # Fallback-библиотеки: все NEEDED, которых нет в системе, кладём в
    # usr/lib/fallback. AppRun подключит их к LD_LIBRARY_PATH, только
    # если в системе их нет. FORBIDDEN_RE — то, что бандлить нельзя
    # (GPU-драйверы, DRM, X11/Wayland-сервер, libc).
    local FORBIDDEN_RE='^(libEGL|libGLX|libGLdispatch|libOpenGL|libGLES|libGL\.|libGLU|libglapi|libvulkan|libdrm|libgbm|libva|libvdpau|libdisplay-info|libX11|libxcb|libwayland|libc\.so|ld-linux|libm\.so|libpthread|libdl\.so|librt\.so|libgcc_s|libstdc\+\+|libz\.so)'

    echo "[+] Сбор NEEDED-списка из бинарника и библиотек в APPDIR..."
    local needed
    needed=$(
        {
            readelf -d "$APPDIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true
            find "$APPDIR/usr/lib" -maxdepth 2 \( -name '*.so' -o -name '*.so.*' \) -type f 2>/dev/null \
                -exec readelf -d {} \; 2>/dev/null || true
        } \
        | awk '/NEEDED/ {gsub(/[][]/,""); print $NF}' \
        | sort -u
    )

    mkdir -p "$APPDIR/usr/lib/fallback"
    local fb_count=0
    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        echo "$lib" | grep -qE "$FORBIDDEN_RE" && continue
        [ -e "$APPDIR/usr/lib/$lib" ] && continue
        local src
        src=$(ldconfig -p 2>/dev/null | awk -v L="$lib" '$1==L {print $NF; exit}')
        if [ -z "$src" ] || [ ! -f "$src" ]; then
            continue
        fi
        local real
        real=$(readlink -f "$src")
        cp -a "$real" "$APPDIR/usr/lib/fallback/$(basename "$real")"
        ln -sf "$(basename "$real")" "$APPDIR/usr/lib/fallback/$lib"
        fb_count=$((fb_count + 1))
    done <<< "$needed"
    echo "[i] Fallback-библиотек скопировано: $fb_count"

    rm -f "$APPDIR/AppRun" "$APPDIR/AppRun.wrapped" "$APPDIR/AppRun.linuxdeploy"

    cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="${APPDIR:-$HERE}"

export LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/fallback:${LD_LIBRARY_PATH:-}"

# Хук linuxdeploy-plugin-gtk.sh жёстко ставит GDK_BACKEND=x11 и
# GTK_THEME=Adwaita:light|dark. Запоминаем пользовательские значения,
# чтобы восстановить их после source и/или доуточнить.
_iptv_saved_gdk_backend="${GDK_BACKEND:-}"
_iptv_saved_gtk_theme="${GTK_THEME:-}"

if [ -f "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh" ]; then
    source "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh"
fi

# GDK_BACKEND: пользовательский выбор перебивает дефолт хука.
# Пример: headless без Xvfb (Rocky 10) поднимает только weston
# и передаёт GDK_BACKEND=wayland.
if [ -n "$_iptv_saved_gdk_backend" ]; then
    export GDK_BACKEND="$_iptv_saved_gdk_backend"
else
    unset GDK_BACKEND
fi
unset _iptv_saved_gdk_backend

# GTK_THEME: 1) явный GTK_THEME пользователя; 2) системная тема, если у неё
# есть gtk-3.0/gtk.css (иначе GTK молча откатится на встроенную Adwaita
# и потеряется dark/light от хука); 3) baseline Adwaita:light|dark от хука.
if [ -n "$_iptv_saved_gtk_theme" ]; then
    export GTK_THEME="$_iptv_saved_gtk_theme"
else
    _iptv_sys_theme=""

    if [ -r "$HOME/.config/gtk-3.0/settings.ini" ]; then
        _iptv_sys_theme=$(sed -n \
            's/^[[:space:]]*gtk-theme-name[[:space:]]*=[[:space:]]*//p' \
            "$HOME/.config/gtk-3.0/settings.ini" | head -n1 | tr -d '\r')
    fi

    if [ -z "$_iptv_sys_theme" ]; then
        _iptv_sys_theme=$(GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas" \
                          gsettings get org.gnome.desktop.interface gtk-theme \
                          2>/dev/null | tr -d "'" | tr -d '\r')
    fi

    if [ -n "$_iptv_sys_theme" ] && [ "$_iptv_sys_theme" != "Adwaita" ]; then
        for _iptv_dir in /usr/share /usr/local/share \
                         "$HOME/.local/share" "$HOME/.themes"; do
            if [ -f "$_iptv_dir/themes/$_iptv_sys_theme/gtk-3.0/gtk.css" ]; then
                export GTK_THEME="$_iptv_sys_theme"
                break
            fi
        done
    fi
fi
unset _iptv_saved_gtk_theme _iptv_sys_theme _iptv_dir

# Fallback: в LD_LIBRARY_PATH идут только те либы, которых нет в системе.
if [ -d "$APPDIR/usr/lib/fallback" ]; then
    FB_TMP=$(mktemp -d -t iptvplayer-fb.XXXXXX) || FB_TMP=""
    if [ -n "$FB_TMP" ]; then
        fb_needed=0
        for lib in "$APPDIR/usr/lib/fallback"/*.so*; do
            [ -e "$lib" ] || continue
            libname=$(basename "$lib")
            if ! ldconfig -p 2>/dev/null | grep -q "$libname"; then
                ln -sf "$lib" "$FB_TMP/$libname"
                fb_needed=1
            fi
        done
        if [ "$fb_needed" = 1 ]; then
            export LD_LIBRARY_PATH="$FB_TMP:${LD_LIBRARY_PATH:-}"
            trap 'rm -rf "$FB_TMP"' EXIT
        else
            rm -rf "$FB_TMP"
        fi
    fi
fi

BIN="$APPDIR/usr/bin/iptvplayer"
if [ -x "$BIN" ]; then
    missing=$(LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/fallback:${LD_LIBRARY_PATH:-}" \
              ldd "$BIN" 2>&1 | awk '/not found/ {print $1}' | sort -u)
    if [ -n "$missing" ]; then
        echo "iptvplayer: не удалось запустить — отсутствуют системные библиотеки:" >&2
        echo "$missing" | while read -r lib; do
            echo "  - $lib" >&2
        done
        echo "" >&2
        echo "Установите их средствами вашего дистрибутива." >&2
        exit 1
    fi
fi

exec "$APPDIR/usr/bin/iptvplayer" "$@"
APPRUN
    chmod +x "$APPDIR/AppRun"

    echo "[✓] AppDir наполнен."
    return 0
}

# =============================================================================
#                              APPIMAGE
# =============================================================================
build_appimage() {
    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}.AppImage"

    echo "[+] Создание AppImage ($APPIMAGE_ARCH)..."

    # Наполнить AppDir, если ещё не наполнен
    if [ ! -d "$APPDIR/usr/lib" ]; then
        if ! populate_appdir; then
            return 1
        fi
    fi

    # Проверка до упаковки: AppRun должен быть наш, а не linuxdeploy-овский
    if [ ! -f "$APPDIR/AppRun" ]; then
        echo "[!] $APPDIR/AppRun отсутствует — populate_appdir не отработал" >&2
        return 1
    fi
    if ! grep -q 'iptvplayer-fb' "$APPDIR/AppRun"; then
        echo "[!] $APPDIR/AppRun не содержит нашей fallback-логики — отказ" >&2
        return 1
    fi

    # .DirIcon — appimagetool требует, linuxdeploy обычно создаёт сам
    if [ ! -e "$APPDIR/.DirIcon" ] && [ -f "$APPDIR/$ICON_NAME" ]; then
        ln -sf "$ICON_NAME" "$APPDIR/.DirIcon"
    fi

    # Скачиваем appimagetool (один раз)
    local APPIMAGETOOL="$SCRIPT_DIR/appimagetool-${APPIMAGE_ARCH}.AppImage"
    if [ ! -f "$APPIMAGETOOL" ]; then
        echo "[+] Скачивание appimagetool ($APPIMAGE_ARCH)..."
        if ! download \
            "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${APPIMAGE_ARCH}.AppImage" \
            "$APPIMAGETOOL"; then
            echo "[!] не удалось скачать appimagetool" >&2
            return 1
        fi
        chmod +x "$APPIMAGETOOL"
    fi

    echo "[+] Упаковка AppDir в AppImage через appimagetool..."
    rm -f "$appimage_file"
    if ! APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$APPIMAGE_ARCH" "$APPIMAGETOOL" \
        --no-appstream \
        "$APPDIR" "$appimage_file"; then
        echo "[!] appimagetool завершился с ошибкой" >&2
        return 1
    fi

    if [ ! -f "$appimage_file" ]; then
        echo "[!] appimagetool не создал $appimage_file" >&2
        return 1
    fi

    echo "[✓] AppImage: $appimage_file"

    if command -v zsyncmake >/dev/null; then
        if ! zsyncmake "$appimage_file" -o "$OUTPUT_DIR/$(basename "$appimage_file" .AppImage).zsync"; then
            echo "[!] zsyncmake упал — .zsync не сгенерирован" >&2
        fi
    fi
    
    rm -f "${OUTPUT_DIR:?}/appinfo"
    
    return 0
}

# =============================================================================
#                       BUNDLED: staging + .deb + .rpm
# =============================================================================
build_bundled_stage() {
    # Наполнить AppDir, если ещё не наполнен
    if [ ! -d "$APPDIR/usr/lib" ]; then
        if ! populate_appdir; then
            return 1
        fi
    fi

    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR/opt" \
             "$STAGING_DIR/usr/bin" \
             "$STAGING_DIR/usr/share/applications" \
             "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"

    # Весь AppDir целиком — в /opt/iptvplayer
    cp -a "$APPDIR" "$STAGING_DIR${BUNDLE_PREFIX}"
    chmod -R u+rwX,go+rX "$STAGING_DIR${BUNDLE_PREFIX}"
    chmod +x "$STAGING_DIR${BUNDLE_PREFIX}/AppRun" 2>/dev/null || true
    chmod +x "$STAGING_DIR${BUNDLE_PREFIX}/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # Wrapper в /usr/bin
    cat > "$STAGING_DIR/usr/bin/$PACKAGE_NAME" <<EOF
#!/bin/bash
exec ${BUNDLE_PREFIX}/AppRun "\$@"
EOF
    chmod 755 "$STAGING_DIR/usr/bin/$PACKAGE_NAME"

    write_desktop_file "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop"

    cp "$APPDIR/$ICON_NAME" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" 2>/dev/null || true

    # metainfo
    if [ -f "$APPDIR/usr/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/metainfo"
        cp "$APPDIR/usr/share/metainfo/$METAINFO_NAME" "$STAGING_DIR/usr/share/metainfo/"
    fi
    return 0
}

# ---- Bundled .deb ----
build_deb_bundled() {
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${DEB_ARCH}.deb"
    echo "[+] Создание bundled .deb..."
    if ! build_bundled_stage; then
        return 1
    fi

    mkdir -p "$STAGING_DIR/DEBIAN"

    local bundled_bin="$STAGING_DIR${BUNDLE_PREFIX}/usr/bin/$PACKAGE_NAME"
    local bundled_lib="$STAGING_DIR${BUNDLE_PREFIX}/usr/lib"
    local bundled_fb="$STAGING_DIR${BUNDLE_PREFIX}/usr/lib/fallback"
    local depends
    depends=$(detect_deb_depends "$bundled_bin" "$bundled_lib" "$bundled_fb")
    if [ -z "$depends" ]; then
        echo "[!] bundled .deb: не удалось определить Depends" >&2
        return 1
    fi
    echo "[+] bundled .deb Depends: $depends"

    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: network
Priority: optional
Architecture: $DEB_ARCH
Depends: $depends
Maintainer: ijurij <mnisjil@duck.com>
Homepage: https://github.com/i-jurij/$PACKAGE_NAME
Description: IPTV Playlist Player (bundled)
 Self-contained build with all libraries in $BUNDLE_PREFIX.
EOF

    cat > "$STAGING_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
if [ -x /usr/bin/update-desktop-database ]; then
    /usr/bin/update-desktop-database -q || true
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/postinst"

    cat > "$STAGING_DIR/DEBIAN/prerm" << EOF
#!/bin/bash
set -e
if [ \$1 = "remove" ] || [ \$1 = "purge" ]; then
    rm -f "/usr/share/applications/$PACKAGE_NAME.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/prerm"

    chmod -R 755 "$STAGING_DIR/usr" 2>/dev/null || true
    chmod 755 "$STAGING_DIR/DEBIAN"
    if ! dpkg-deb -Zxz --build --root-owner-group "$STAGING_DIR" "$deb_file"; then
        echo "[!] bundled .deb: dpkg-deb упал" >&2
        return 1
    fi
    echo "[✓] Bundled .deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
    return 0
}

# ---- Bundled .rpm ----
build_rpm_bundled() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${release}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$PROJECT_ROOT/pkg-rpm"
    echo "[+] Создание bundled .rpm..."

    if ! build_bundled_stage; then
        echo "[!] bundled .rpm: не удалось подготовить staging" >&2
        return 1
    fi

    mkdir -p "$SPEC_DIR/SOURCES"
    if ! tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
        --transform="flags=r;s,^,$PACKAGE_NAME-$VERSION/," \
        -C "$STAGING_DIR" .; then
        echo "[!] bundled .rpm: не удалось создать архив" >&2
        return 1
    fi

    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir $SPEC_DIR
%define _binary_payload w2.xzdio
%global __requires_exclude_from ^/opt/iptvplayer/usr/lib/.*$
Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $release
Summary:        IPTV Playlist Player (bundled)
License:        MIT
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      $RPM_ARCH

%description
Self-contained build with all libraries in $BUNDLE_PREFIX.

%prep
%setup -q

%build
# already built

%install
rm -rf \$RPM_BUILD_ROOT
mkdir -p \$RPM_BUILD_ROOT
tar -xzf %{SOURCE0} -C \$RPM_BUILD_ROOT --strip-components=1

%files
${BUNDLE_PREFIX}/
/usr/bin/$PACKAGE_NAME
/usr/share/applications/$PACKAGE_NAME.desktop
/usr/share/icons/hicolor/scalable/apps/$ICON_NAME
/usr/share/metainfo/$METAINFO_NAME

%post
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
if [ -x /usr/bin/update-desktop-database ]; then
    /usr/bin/update-desktop-database -q || true
fi

%preun
if [ \$1 = 0 ]; then
    rm -f "/usr/share/applications/$PACKAGE_NAME.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
    rm -f "/usr/bin/$PACKAGE_NAME"
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$release
- Initial build
EOF

    if ! rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"; then
        echo "[!] bundled .rpm: rpmbuild упал (см. вывод выше)" >&2
        return 1
    fi

    if ! { mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"; }; then
        echo "[!] bundled .rpm: не найден собранный .rpm" >&2
        return 1
    fi

    echo "[✓] Bundled .rpm: $rpm_file"
    return 0
}