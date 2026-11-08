#!/usr/bin/env bash
# =============================================================================
# cmd/package.sh - --package command.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "cmd/package.sh is a library, do not run directly." >&2
    exit 1
fi

package_run() {
    # detect_distro / detect_sudo / system_prepare already ran once in make.sh.
    section "Package format"
    # --- Format selection FIRST (matches old build-package.sh) --------------
    if [ "$OPT_NATIVE" = false ] && [ "$OPT_APPIMAGE" = false ] \
       && [ "$OPT_SHARUN" = false ] && [ "$OPT_FLATPAK" = false ]; then
        if [[ "${NON_INTERACTIVE:-false}" == true ]] || [ "$OPT_NO_MENU" = true ]; then
            warn "No format flag specified - defaulting to native package"
            OPT_NATIVE=true
        else
            package_show_menu
        fi
    fi

    # --- --rebuild cleanup (after menu: cancel does not nuke artifacts) -----
    if [ "$OPT_REBUILD" = true ]; then
        rm -rf "$PROJECT_ROOT"/build-*
        rm -rf "$PROJECT_ROOT/install"
        rm -rf "$PROJECT_ROOT/dist"
        rm -rf "$PROJECT_ROOT"/pkg-*
        rm -rf "$PROJECT_ROOT/pkg-staging"
        rm -rf "$PROJECT_ROOT/AppDir"
        log "--rebuild: removed build/install/dist/pkg-*/staging/AppDir"
    fi

    # --- Binary -------------------------------------------------------------
    section "Binary"
    # Interactive: ask before rebuilding, so a user with a working binary can
    # repackage as-is. wx/wxSQLite3 questions appear only on "yes" (inside
    # deps_ensure_for_build). Noninteractive: never ask, use what exists.
    OPT_BUILD_TYPE="${OPT_BUILD_TYPE:-release}"
    bin_resolve_paths

    local build_binary=false
    if [ ! -x "$PROJECT_ROOT/install/bin/$PACKAGE_NAME" ]; then
        log "Binary not found - building"
        build_binary=true
    elif [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        log "Noninteractive: binary exists, using it"
    elif ask "Rebuild binary ($PACKAGE_NAME)? [y/N]:" n; then
        log "Rebuilding binary"
        build_binary=true
    else
        log "Using existing binary"
    fi

    if [ "$build_binary" = true ]; then
        deps_ensure_for_build
        bin_ensure_binary || error "Failed to build binary"
        if [ ! -x "$PROJECT_ROOT/install/bin/$PACKAGE_NAME" ]; then
            error "Binary not found after build: $PROJECT_ROOT/install/bin/$PACKAGE_NAME"
        fi
    fi

    # --- Versions ------------------------------------------------------------
    local versions_tmp
    versions_tmp="$(mktemp)"
    if ! read_versions_from_install > "$versions_tmp"; then
        rm -f "$versions_tmp"
        error "Cannot read version files from install/"
    fi
    local VERSION_DISPLAY VERSION_FILE VERSION
    if ! { read -r VERSION_DISPLAY; read -r VERSION_FILE; read -r VERSION; } < "$versions_tmp"; then
        rm -f "$versions_tmp"
        error "Cannot read version files from install/"
    fi
    rm -f "$versions_tmp"
    if [ -z "$VERSION_DISPLAY" ] || [ -z "$VERSION_FILE" ] || [ -z "$VERSION" ]; then
        error "Cannot read version files from install/"
    fi

    echo "=== $PACKAGE_NAME:$VERSION (file: $VERSION_FILE, arch: $DEB_ARCH/$RPM_ARCH/$APPIMAGE_ARCH, distro: $DISTRO) ==="

    check_deps "$OPT_NATIVE" "$OPT_APPIMAGE" "$OPT_SHARUN" "$OPT_FLATPAK"
    ensure_tools "base packaging tools" "tar:tar" "readelf:binutils"
    setup_dirs

    local -a FAILED=()

    if [ "$OPT_NATIVE" = true ]; then
        source "$SCRIPT_DIR/formats/native.sh"
        build_native || FAILED+=("native")
    fi
    if [ "$OPT_APPIMAGE" = true ]; then
        source "$SCRIPT_DIR/formats/appimage.sh"
        build_appimage || FAILED+=("appimage")
    fi
    if [ "$OPT_SHARUN" = true ]; then
        source "$SCRIPT_DIR/formats/sharun.sh"
        build_sharun_appimage || FAILED+=("sharun")
    fi
    if [ "$OPT_FLATPAK" = true ]; then
        source "$SCRIPT_DIR/formats/flatpak.sh"
        build_flatpak || FAILED+=("flatpak")
    fi

    sign_files

    if [ ${#FAILED[@]} -ne 0 ]; then
        echo "[!] Failed: ${FAILED[*]}" >&2
        ls -la "$OUTPUT_DIR/"
        return 1
    fi

    echo ""
    ls -la "$OUTPUT_DIR/"
    echo ""
    log "Done. Artifacts in $OUTPUT_DIR"
    return 0
}

# -----------------------------------------------------------------------------
# package_show_menu - private
# -----------------------------------------------------------------------------
package_show_menu() {
    local -a labels=() actions=()

    if declare -F distro_build >/dev/null 2>&1; then
        labels+=("${NATIVE_LABEL:-Native package} (native package)")
        actions+=("native")
    fi
    labels+=("AppImage (linuxdeploy)"); actions+=("appimage")
    labels+=("AppImage (quick-sharun)"); actions+=("sharun")
    labels+=("Flatpak"); actions+=("flatpak")
    echo ""
    echo "Detected: $DISTRO ($(uname -m))"
    echo ""
    echo "Choose what to build:"
    local i=1
    local label
    for label in "${labels[@]}"; do
        echo "  $i) $label"
        i=$((i+1))
    done
    echo "  0) Cancel"
    echo ""

    local choice=""
    read -p "Enter number: " -r choice || true

    if [ "$choice" = "0" ]; then
        log "Cancelled"
        exit 0
    fi

    if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt "${#actions[@]}" ]; then
        error "Invalid choice"
    fi

    local action="${actions[$((choice-1))]}"
    case "$action" in
        native)   OPT_NATIVE=true ;;
        appimage) OPT_APPIMAGE=true ;;
        sharun)   OPT_SHARUN=true ;;
        flatpak)  OPT_FLATPAK=true ;;
    esac
}