#!/usr/bin/env bash
# =============================================================================
# formats/appimage.sh - AppImage (linuxdeploy) via appimagetool.
# =============================================================================
# Uses system GPU stack (matches linuxdeploy excludelist). For a
# self-contained bundle use sharun variant (formats/sharun.sh).
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "formats/appimage.sh is a library, do not run directly." >&2
    exit 1
fi

populate_appdir() {
    if [[ "$APPIMAGE_ARCH" != "x86_64" && "$APPIMAGE_ARCH" != "aarch64" ]]; then
        echo "[!] AppImage is not supported for '$APPIMAGE_ARCH'" >&2
        return 1
    fi

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/scalable/apps"

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] populate_appdir: failed to prepare staging" >&2
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

    if [ -d "$STAGING_DIR/usr/share/locale" ]; then
        mkdir -p "$APPDIR/usr/share/locale"
        cp -r "$STAGING_DIR/usr/share/locale/"* "$APPDIR/usr/share/locale/"
    fi

    local LINUXDEPLOY="$SCRIPT_DIR/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    local GTK_PLUGIN="$SCRIPT_DIR/linuxdeploy-plugin-gtk.sh"

    if [ ! -f "$LINUXDEPLOY" ]; then
        log "Downloading linuxdeploy ($APPIMAGE_ARCH)..."
        if ! download \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" \
            "$LINUXDEPLOY"; then
            echo "[!] Failed to download linuxdeploy" >&2
            return 1
        fi
        chmod +x "$LINUXDEPLOY"
    fi
    if [ ! -f "$GTK_PLUGIN" ]; then
        log "Downloading GTK plugin (script)..."
        if ! download \
            "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
            "$GTK_PLUGIN"; then
            echo "[!] Failed to download GTK plugin" >&2
            return 1
        fi
        chmod +x "$GTK_PLUGIN"
    fi

    log "Populating AppDir via linuxdeploy..."
    if ! APPIMAGE_EXTRACT_AND_RUN=1 DEPLOY_GTK_VERSION=3 ARCH="$APPIMAGE_ARCH" "$LINUXDEPLOY" \
        --appdir="$APPDIR" \
        --plugin gtk \
        --desktop-file="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"; then
        echo "[!] linuxdeploy failed" >&2
        return 1
    fi

    chmod +x "$APPDIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # Fallback libraries: NEEDED entries not in system go into
    # usr/lib/fallback. AppRun prepends them to LD_LIBRARY_PATH only for
    # libraries the host does not have. FORBIDDEN_RE lists things that
    # must never be bundled (GPU drivers, DRM, X11/Wayland, libc).
    local FORBIDDEN_RE='^(libEGL|libGLX|libGLdispatch|libOpenGL|libGLES|libGL\.|libGLU|libglapi|libvulkan|libdrm|libgbm|libva|libvdpau|libdisplay-info|libX11|libxcb|libwayland|libc\.so|ld-linux|libm\.so|libpthread|libdl\.so|librt\.so|libutil\.so|libresolv|libnss_|libgcc_s|libstdc\+\+|libz\.so)'

    log "Collecting NEEDED list from binary and APPDIR libraries..."
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
    local _needed_tmp
    _needed_tmp="$(mktemp)"
    printf '%s\n' "$needed" > "$_needed_tmp"

    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        echo "$lib" | grep -qE "$FORBIDDEN_RE" && continue
        [ -e "$APPDIR/usr/lib/$lib" ] && continue
        local src
        src=$(ldconfig -p 2>/dev/null | awk -v L="$lib" '$1==L {print $NF; exit}')
        if [ -z "$src" ] || [ ! -f "$src" ]; then
            continue
        fi
        local real realbase
        real=$(readlink -f "$src")
        realbase=$(basename "$real")

        if [ ! -e "$APPDIR/usr/lib/fallback/$realbase" ]; then
            cp -a "$real" "$APPDIR/usr/lib/fallback/$realbase"
        fi

        if [ "$lib" != "$realbase" ]; then
            ln -sf "$realbase" "$APPDIR/usr/lib/fallback/$lib"
        fi

        fb_count=$((fb_count + 1))
    done < "$_needed_tmp"
    rm -f "$_needed_tmp"

    log "Fallback libraries copied: $fb_count"

    rm -f "$APPDIR/AppRun" "$APPDIR/AppRun.wrapped" "$APPDIR/AppRun.linuxdeploy"

    cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="${APPDIR:-$HERE}"

# Main LD_LIBRARY_PATH - linuxdeploy deploy only. Fallback directory is
# attached via FB_TMP below, only for libraries absent on the host.
export LD_LIBRARY_PATH="$APPDIR/usr/lib:${LD_LIBRARY_PATH:-}"

# linuxdeploy-plugin-gtk.sh forces GDK_BACKEND=x11 and GTK_THEME.
# Remember user values to restore them after source.
_iptv_saved_gdk_backend="${GDK_BACKEND:-}"
_iptv_saved_gtk_theme="${GTK_THEME:-}"

if [ -f "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh" ]; then
    source "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh"
fi

# GDK_BACKEND: user choice overrides the hook default.
if [ -n "$_iptv_saved_gdk_backend" ]; then
    export GDK_BACKEND="$_iptv_saved_gdk_backend"
else
    unset GDK_BACKEND
fi
unset _iptv_saved_gdk_backend

# GTK_THEME: 1) user GTK_THEME; 2) system theme if it has gtk-3.0/gtk.css;
# 3) baseline Adwaita:light|dark from the hook.
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

# Fallback: prepend to LD_LIBRARY_PATH only libraries absent on host.
if [ -d "$APPDIR/usr/lib/fallback" ]; then
    FB_TMP=$(mktemp -d -t iptvplayer-fb.XXXXXX) || FB_TMP=""
    if [ -n "$FB_TMP" ]; then
        fb_needed=0
        for lib in "$APPDIR/usr/lib/fallback"/*.so*; do
            [ -e "$lib" ] || continue
            libname=$(basename "$lib")
            if ! ldconfig -p 2>/dev/null | grep -qE "[[:space:]]${libname}([[:space:]]|$)"; then
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
        echo "iptvplayer: cannot launch - missing system libraries:" >&2
        echo "$missing" | while read -r lib; do
            echo "  - $lib" >&2
        done
        echo "" >&2
        echo "Install them using your distribution's package manager." >&2
        exit 1
    fi
fi

exec "$APPDIR/usr/bin/iptvplayer" "$@"
APPRUN
    chmod +x "$APPDIR/AppRun"

    log "AppDir populated."
    return 0
}

build_appimage() {
    section "AppImage (linuxdeploy)"
    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}-linuxdeploy.AppImage"

    log "Creating AppImage ($APPIMAGE_ARCH)..."

    if [ ! -d "$APPDIR/usr/lib" ]; then
        if ! populate_appdir; then
            return 1
        fi
    fi

    if [ ! -f "$APPDIR/AppRun" ]; then
        echo "[!] $APPDIR/AppRun missing - populate_appdir did not run" >&2
        return 1
    fi
    if ! grep -q 'iptvplayer-fb' "$APPDIR/AppRun"; then
        echo "[!] $APPDIR/AppRun does not contain our fallback logic - refusing" >&2
        return 1
    fi

    if [ ! -f "$PROJECT_ROOT/icons/${PACKAGE_NAME}-256.png" ]; then
        error "Missing icons/${PACKAGE_NAME}-256.png (required for .DirIcon by appimagetool)"
    fi
    rm -f "$APPDIR/.DirIcon"
    cp "$PROJECT_ROOT/icons/${PACKAGE_NAME}-256.png" "$APPDIR/.DirIcon"

    local APPIMAGETOOL="$SCRIPT_DIR/appimagetool-${APPIMAGE_ARCH}.AppImage"
    if [ ! -f "$APPIMAGETOOL" ]; then
        log "Downloading appimagetool ($APPIMAGE_ARCH)..."
        if ! download \
            "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${APPIMAGE_ARCH}.AppImage" \
            "$APPIMAGETOOL"; then
            echo "[!] Failed to download appimagetool" >&2
            return 1
        fi
        chmod +x "$APPIMAGETOOL"
    fi

    log "Packing AppDir into AppImage via appimagetool..."
    rm -f "$appimage_file"

    # appimagetool invokes zsyncmake which writes the .zsync relative to
    # the current working directory, not to the AppImage path. Run from
    # $OUTPUT_DIR so the .zsync lands next to the .AppImage.
    cd "$OUTPUT_DIR" || return 1

    local upd_info="gh-releases-zsync|i-jurij|iptvplayer|latest|iptvplayer-linux-*-linuxdeploy.AppImage.zsync"
    if ! APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$APPIMAGE_ARCH" "$APPIMAGETOOL" \
        --no-appstream \
        -u "$upd_info" \
        "$APPDIR" "${appimage_file##*/}"; then
        cd "$PROJECT_ROOT"
        echo "[!] appimagetool failed" >&2
        return 1
    fi

    cd "$PROJECT_ROOT"

    if [ ! -f "$appimage_file" ]; then
        echo "[!] appimagetool did not produce $appimage_file" >&2
        return 1
    fi

    log "AppImage: $appimage_file"

    rm -f "${OUTPUT_DIR:?}/appinfo"

    return 0
}