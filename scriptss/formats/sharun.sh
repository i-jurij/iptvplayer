#!/usr/bin/env bash
# =============================================================================
# formats/sharun.sh - AppImage via quick-sharun.
# =============================================================================
# Canonical AppDir layout: root = effective /usr (bin/, lib/, share/).
# install/ already has the right shape; no separate staging needed.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "formats/sharun.sh is a library, do not run directly." >&2
    exit 1
fi

_resolve_dejavu_font() {
    local cached="${SCRIPT_DIR}/DejaVuSans.ttf"
    [ -f "$cached" ] && { printf '%s\n' "$cached"; return 0; }

    local p
    for p in \
        /usr/share/fonts/TTF/DejaVuSans.ttf \
        /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
        /usr/share/fonts/dejavu/DejaVuSans.ttf \
        /usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf \
        /usr/share/fonts/truetype/DejaVuSans.ttf \
        /usr/local/share/fonts/DejaVuSans.ttf \
        /usr/local/share/fonts/TTF/DejaVuSans.ttf
    do
        [ -f "$p" ] && { printf '%s\n' "$p"; return 0; }
    done

    local url="https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2"
    local archive="${SCRIPT_DIR}/.dejavu-ttf-2.37.tar.bz2"
    log "Font not found locally, downloading: $url"
    download "$url" "$archive" || { echo "[!] Failed to download DejaVu" >&2; return 1; }

    local tmp; tmp="$(mktemp -d)"
    tar -xjf "$archive" -C "$tmp" 2>/dev/null || { rm -rf "$tmp" "$archive"; return 1; }
    local src; src="$(find "$tmp" -name 'DejaVuSans.ttf' -type f -print -quit)"
    [ -n "$src" ] || { rm -rf "$tmp" "$archive"; return 1; }

    local magic
    magic="$(dd if="$src" bs=4 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    case "$magic" in
        00010000|74727565|74746366|4f54544f) ;;
        *) rm -rf "$tmp" "$archive"; return 1 ;;
    esac

    cp "$src" "$cached"
    rm -rf "$tmp" "$archive"
    log "DejaVu Sans cached: $cached"
    printf '%s\n' "$cached"
}

build_sharun_appimage() {
    local appimage_file="${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}-sharun.AppImage"
    local bin_src="$PROJECT_ROOT/install/bin/$PACKAGE_NAME"
    local share_src="$PROJECT_ROOT/install/share/$PACKAGE_NAME"
    local desktop_file="$PROJECT_ROOT/install/share/applications/$PACKAGE_NAME.desktop"
    local icon_file="$PROJECT_ROOT/install/share/$PACKAGE_NAME/icons/$ICON_NAME"
    local QUICK_SHARUN="$SCRIPT_DIR/quick-sharun.sh"

    log "Building AppImage via quick-sharun..."

    : "${APPDIR:?APPDIR not set}"
    : "${OUTPUT_DIR:?OUTPUT_DIR not set}"
    : "${PROJECT_ROOT:?PROJECT_ROOT not set}"
    : "${PACKAGE_NAME:?PACKAGE_NAME not set}"
    : "${APPIMAGE_ARCH:?APPIMAGE_ARCH not set}"
    : "${VERSION_FILE:?VERSION_FILE not set}"

    case "$APPDIR" in
        "$PROJECT_ROOT"/*) ;;
        *)
            echo "[!] APPDIR outside PROJECT_ROOT: $APPDIR - refusing" >&2
            return 1
            ;;
    esac

    ensure_tools "sharun AppImage" "wget:wget" "patchelf:patchelf"

    if [ ! -f "$bin_src" ]; then
        echo "[!] Binary not found: $bin_src" >&2
        return 1
    fi
    if [ ! -d "$share_src" ]; then
        echo "[!] App data directory not found: $share_src" >&2
        return 1
    fi
    chmod +x "$bin_src" 2>/dev/null || true

    local QUICK_SHARUN_REF="${QUICK_SHARUN_REF:-main}"
    local QUICK_SHARUN_URL="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/${QUICK_SHARUN_REF}/useful-tools/quick-sharun.sh"

    rm -f "$QUICK_SHARUN"
    log "Downloading quick-sharun (ref=${QUICK_SHARUN_REF})..."
    if ! download "$QUICK_SHARUN_URL" "$QUICK_SHARUN"; then
        echo "[!] Failed to download quick-sharun" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    chmod +x "$QUICK_SHARUN"
    if ! head -c 2 "$QUICK_SHARUN" | grep -q '#!'; then
        echo "[!] quick-sharun is not a shell script (no shebang)" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    write_desktop_file "$desktop_file"
    if [ ! -f "$desktop_file" ] || ! grep -q '^\[Desktop Entry\]' "$desktop_file"; then
        echo "[!] write_desktop_file did not create a valid .desktop: $desktop_file" >&2
        return 1
    fi

    rm -rf "$APPDIR"

    export APPDIR
    export ARCH="$APPIMAGE_ARCH"
    export VERSION="$VERSION_FILE"
    export OUTPATH="$OUTPUT_DIR"
    export OUTNAME="$appimage_file"
    export DESKTOP="$desktop_file"
    export ICON="$icon_file"
    export UPINFO="gh-releases-zsync|i-jurij|iptvplayer|latest|iptvplayer-linux-*-sharun.AppImage.zsync"
    export GTK_CLASS_FIX=1
    export DEPLOY_GDK=1
    export DEPLOY_OPENGL=1
    export DEPLOY_VULKAN=1
    unset ANYLINUX_DO_NOT_LOAD_LIBS 2>/dev/null || true

    if [ "$DEPLOY_GDK" = 1 ]; then
        local _gdk_loaders_dir=""
        local _d
        for _d in /usr/lib/gdk-pixbuf-*/*/loaders \
                  /usr/lib64/gdk-pixbuf-*/*/loaders \
                  /usr/lib/*-linux-gnu/gdk-pixbuf-*/*/loaders; do
            [ -d "$_d" ] || continue
            if ls "$_d"/*pixbufloader*svg*.so* >/dev/null 2>&1; then
                _gdk_loaders_dir="$_d"
                break
            fi
        done

        if [ -n "$_gdk_loaders_dir" ]; then
            log "gdk-pixbuf loaders dir: $_gdk_loaders_dir"
            set -- "$@" "$_gdk_loaders_dir"
        else
            log "gdk-pixbuf loaders dir not found - SVG via glycin or librsvg missing"
        fi
    fi

    log "Deploying dependencies via quick-sharun..."
    if ! "$QUICK_SHARUN" "$bin_src" "$@"; then
        echo "[!] quick-sharun (deploy) failed" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    log "Copying app data to $APPDIR/share/$PACKAGE_NAME..."
    mkdir -p "$APPDIR/share/$PACKAGE_NAME"
    if ! cp -a "$share_src/." "$APPDIR/share/$PACKAGE_NAME/"; then
        echo "[!] Failed to copy app data to APPDIR" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    if [ -z "$(ls -A "$APPDIR/share/$PACKAGE_NAME" 2>/dev/null)" ]; then
        echo "[!] $APPDIR/share/$PACKAGE_NAME is empty after copy" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    if [ -d "$PROJECT_ROOT/install/share/locale" ]; then
        mkdir -p "$APPDIR/share/locale"
        cp -r "$PROJECT_ROOT/install/share/locale/"* "$APPDIR/share/locale/"
        log "Locales: $APPDIR/share/locale"
    fi

    local _bundle_loader _bundle_glycin
    _bundle_loader="$(find "$APPDIR" \( -type f -o -type l \) \
                      -name '*pixbufloader*svg*.so*' -print -quit 2>/dev/null || true)"
    _bundle_glycin="$(find "$APPDIR" -type f -name 'glycin-svg' -print -quit 2>/dev/null || true)"

    if [ -n "$_bundle_loader" ]; then
        log "gdk-pixbuf SVG loader: ${_bundle_loader#"$APPDIR"}"
    elif [ -n "$_bundle_glycin" ]; then
        log "gdk-pixbuf SVG via glycin: ${_bundle_glycin#"$APPDIR"}"
    else
        echo "[!] SVG loader not found in bundle - app will not render any icon and will fail at startup." >&2
        echo "[!] Install one of the following on the build host:" >&2
        echo "[!]   Debian/Ubuntu: librsvg2-common" >&2
        echo "[!]   Fedora/RHEL:   librsvg2" >&2
        echo "[!]   Arch:          librsvg (uses glycin)" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    local _bundle_cache
    _bundle_cache="$(find "$APPDIR" \( -type f -o -type l \) \
                     -name 'loaders.cache' -print -quit 2>/dev/null || true)"

    if [ -z "$_bundle_cache" ] && [ -n "$_bundle_loader" ]; then
        if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
            local _loader_dir _generated
            _loader_dir="$(dirname "$_bundle_loader")"
            _generated="${_loader_dir}/loaders.cache"
            log "loaders.cache missing, generating from $_loader_dir..."
            if gdk-pixbuf-query-loaders "$_loader_dir"/*.so* > "$_generated" 2>/dev/null \
               && [ -s "$_generated" ] \
               && grep -q '\.so' "$_generated"; then
                sed -i \
                    -e 's|/usr/lib/.*/loaders/||g' \
                    -e "s|$_loader_dir/||g" \
                    "$_generated"
                _bundle_cache="$_generated"
                log "Generated loaders.cache: ${_bundle_cache#"$APPDIR"}"
            else
                rm -f "$_generated"
                echo "[!] Failed to generate loaders.cache" >&2
            fi
        fi
    fi

    if [ -n "$_bundle_cache" ]; then
        log "gdk-pixbuf loaders.cache: ${_bundle_cache#"$APPDIR"}"
    elif [ -n "$_bundle_glycin" ]; then
        log "loaders.cache not found (normal for Arch, glycin is used)"
    else
        echo "[!] loaders.cache missing in bundle and was not generated." >&2
        echo "[!] gdk-pixbuf will not see any loader - app will crash." >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    if [ -n "$_bundle_cache" ] \
       && ! grep -q '^GDK_PIXBUF_MODULE_FILE=' "$APPDIR/.env" 2>/dev/null; then
        local _cache_rel="${_bundle_cache#"$APPDIR"}"
        [ -f "$APPDIR/.env" ] || : > "$APPDIR/.env"
        echo "GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}" >> "$APPDIR/.env"
        log ".env += GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}"
    elif [ -n "$_bundle_cache" ]; then
        log "GDK_PIXBUF_MODULE_FILE already set in .env"
    else
        log "GDK_PIXBUF_MODULE_FILE not set (no loaders.cache)"
    fi

    log "Creating dbus-fallback.hook..."
    cat > "$APPDIR/bin/96-dbus-fallback.hook" << 'HOOK'
#!/bin/sh
# GLib/GTK/dconf require a session bus. Without one, fall back to
# GSettings memory backend and disable the a11y bridge.
_dbus_available=0

if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    _dbus_available=1
fi

if [ "$_dbus_available" = 0 ] && [ -n "${XDG_RUNTIME_DIR:-}" ] \
   && [ -S "$XDG_RUNTIME_DIR/bus" ]; then
    _dbus_available=1
fi

if [ "$_dbus_available" = 0 ]; then
    export GSETTINGS_BACKEND=memory
    export G_DBUS_SESSION_BUS_ADDRESS=disabled:
    export NO_AT_BRIDGE=1
fi
HOOK
    chmod +x "$APPDIR/bin/96-dbus-fallback.hook"
    log "dbus-fallback.hook installed (triggers only when bus is unavailable)"

    log "Bundling CA certificates (fallback)..."
    mkdir -p "$APPDIR/share/ca"
    local _ca_dst="$APPDIR/share/ca/cacert.pem"
    local _ca_ok=0
    local _ca_src

    for _ca_src in \
        /etc/ssl/certs/ca-certificates.crt \
        /usr/local/etc/ssl/certs/ca-certificates.crt \
        /usr/local/etc/ssl/cert.pem \
        /usr/local/share/ca-certificates/ca-certificates.crt \
        /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem \
        /etc/pki/tls/cert.pem \
        /etc/pki/tls/cacert.pem \
        /etc/pki/tls/certs/ca-bundle.crt \
        /etc/pki/tls/certs/ca-bundle.trust.crt \
        /etc/ssl/cert.pem \
        /etc/ssl/ca-bundle.pem \
        /var/lib/ca-certificates/ca-bundle.pem \
        /etc/ca-certificates/extracted/tls-ca-bundle.pem
    do
        if [ -f "$_ca_src" ]; then
            if cp "$_ca_src" "$_ca_dst" 2>/dev/null \
               && grep -q 'BEGIN CERTIFICATE' "$_ca_dst"; then
                log "CA bundle from build system: $_ca_src"
                _ca_ok=1
                break
            fi
        fi
    done

    if [ "$_ca_ok" = 0 ]; then
        log "System CA bundle not found, downloading..."
        DOWNLOAD_UA="Mozilla/5.0"
        if download_multi "$_ca_dst" \
            "https://curl.se/ca/cacert.pem" \
            "https://raw.githubusercontent.com/bagder/ca-bundle/master/ca-bundle.crt"
        then
            _ca_ok=1
        fi
        unset DOWNLOAD_UA
    fi

    if [ "$_ca_ok" = 1 ]; then
        local _ca_size=0
        _ca_size=$(stat -c %s "$_ca_dst" 2>/dev/null || echo 0)
        if [ "$_ca_size" -lt 51200 ] \
           || ! grep -q 'BEGIN CERTIFICATE' "$_ca_dst"; then
            warn "CA bundle invalid (size=${_ca_size}B) - removing"
            rm -f "$_ca_dst"
            rmdir "$APPDIR/share/ca" 2>/dev/null || true
            _ca_ok=0
        fi
    fi

    if [ "$_ca_ok" = 1 ]; then
        log "cacert.pem bundled ($(du -h "$_ca_dst" | cut -f1), $(grep -c 'BEGIN CERTIFICATE' "$_ca_dst") certificates)"
    else
        warn "CA bundle not bundled - fallback unavailable, system paths only"
    fi

    log "Creating ca-bundle.hook..."
    cat > "$APPDIR/bin/99-ca_bundle.hook" << 'EOF'
#!/bin/sh
# Priority: system CA bundle -> bundled fallback.
# Covers Debian/Ubuntu, Fedora/RHEL, Arch, openSUSE, Alpine, NixOS,
# Gentoo, Tiny Core (/usr/local/...).
for c in \
    /etc/ssl/certs/ca-certificates.crt \
    /usr/local/etc/ssl/certs/ca-certificates.crt \
    /usr/local/etc/ssl/cert.pem \
    /usr/local/share/ca-certificates/ca-certificates.crt \
    /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem \
    /etc/pki/tls/cert.pem \
    /etc/pki/tls/cacert.pem \
    /etc/pki/tls/certs/ca-bundle.crt \
    /etc/pki/tls/certs/ca-bundle.trust.crt \
    /etc/ssl/cert.pem \
    /etc/ssl/ca-bundle.pem \
    /var/lib/ca-certificates/ca-bundle.pem \
    /etc/ca-certificates/extracted/tls-ca-bundle.pem
do
    if [ -f "$c" ]; then
        export IPTVPLAYER_CA_BUNDLE="$c"
        export SSL_CERT_FILE="$c"
        break
    fi
done

if [ -z "$IPTVPLAYER_CA_BUNDLE" ] \
   && [ -f "${SHARUN_DIR}/share/ca/cacert.pem" ]; then
    export IPTVPLAYER_CA_BUNDLE="${SHARUN_DIR}/share/ca/cacert.pem"
    export SSL_CERT_FILE="$IPTVPLAYER_CA_BUNDLE"
fi
EOF
    chmod +x "$APPDIR/bin/99-ca_bundle.hook"

    log "CA bundle: system preferred, bundled as fallback."
    log "Override: IPTVPLAYER_CA_BUNDLE (see README)."

    log "Bundling fallback font..."
    local font_src
    if ! font_src="$(_resolve_dejavu_font)"; then
        warn "Font not bundled - systems without fontconfig will show tofu"
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    mkdir -p "$APPDIR/share/fonts"
    cp "$font_src" "$APPDIR/share/fonts/DejaVuSans.ttf"
    log "Bundled $(basename "$font_src") ($(du -h "$font_src" | cut -f1))"

    log "Creating fonts.conf..."
    mkdir -p "$APPDIR/etc/fonts"
    cat > "$APPDIR/etc/fonts/fonts.conf" << 'FONTCONF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir prefix="relative">../../share/fonts</dir>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <cachedir>/tmp/fontconfig-cache</cachedir>
  <include ignore_missing="yes">/etc/fonts/conf.d</include>
  <include ignore_missing="yes">/usr/local/etc/fonts/conf.d</include>
</fontconfig>
FONTCONF

    log "Creating fonts.hook..."
    cat > "$APPDIR/bin/98-fonts.hook" << 'HOOK'
#!/bin/sh
# FONTCONFIG_FILE is set by sharun to $SHARUN_DIR/etc/fonts/fonts.conf.
# FONTCONFIG_PATH is not set by sharun - set it here so <include> with
# relative paths works.
export FONTCONFIG_PATH="${SHARUN_DIR}/etc/fonts"

mkdir -p /tmp/fontconfig-cache 2>/dev/null || true

# XDG_DATA_HOME on a temporary empty dir: cuts off recursive scan of
# ~/.fonts and ~/.local/share/fonts (where fontconfig hangs on Tiny Core
# with symlinks into /tmp/tcloop/...).
export XDG_DATA_HOME="${TMPDIR:-/tmp}/sharun-xdg-$$"
mkdir -p "$XDG_DATA_HOME/fonts" 2>/dev/null || true
HOOK
    chmod +x "$APPDIR/bin/98-fonts.hook"

    log "Packing AppDir into AppImage..."
    # appimagetool (invoked by quick-sharun) writes the .zsync via
    # zsyncmake relative to the current working directory. Run from
    # $OUTPUT_DIR so the .zsync lands next to the .AppImage.
    cd "$OUTPUT_DIR" || return 1
    if ! "$QUICK_SHARUN" --make-appimage; then
        cd "$PROJECT_ROOT"
        echo "[!] quick-sharun --make-appimage failed" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    cd "$PROJECT_ROOT"

    unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
          UPINFO GTK_CLASS_FIX DEPLOY_GDK \
          DEPLOY_OPENGL DEPLOY_VULKAN

    if [ ! -f "$OUTPUT_DIR/$appimage_file" ]; then
        echo "[!] quick-sharun did not produce $appimage_file" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    log "AppImage (sharun): $OUTPUT_DIR/$appimage_file"

    rm -f "${OUTPUT_DIR:?}/appinfo"
    rm -f "$QUICK_SHARUN"

    return 0
}