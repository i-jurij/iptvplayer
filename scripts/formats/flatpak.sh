#!/usr/bin/env bash
# =============================================================================
# formats/flatpak.sh - Flatpak bundle from install/.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "formats/flatpak.sh is a library, do not run directly." >&2
    exit 1
fi

build_flatpak() {
    : "${PROJECT_ROOT:?}" "${OUTPUT_DIR:?}" "${PACKAGE_NAME:?}" "${VERSION:?}"

    local RUNTIME="${FLATPAK_RUNTIME:-org.gnome.Platform}"
    local RUNTIME_VERSION="${FLATPAK_RUNTIME_VERSION:-47}"
    local APP_ID="${FLATPAK_APP_ID:-io.github.i_jurij.iptvplayer}"
    local BRANCH="${FLATPAK_BRANCH:-stable}"

    local install_dir="$PROJECT_ROOT/install"
    local manifest="$PROJECT_ROOT/dist/.flatpak-manifest/${APP_ID}.yaml"
    local build_dir="$PROJECT_ROOT/dist/.flatpak-build"
    local repo_dir="$PROJECT_ROOT/dist/.flatpak-repo"
    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}.flatpak"

    log "Building Flatpak (runtime ${RUNTIME}//${RUNTIME_VERSION})..."

    ensure_tools "Flatpak packaging" \
        "flatpak:flatpak" \
        "flatpak-builder:flatpak-builder"

    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] Not found: $install_dir/bin/$PACKAGE_NAME" >&2
        return 1
    fi

    mkdir -p "$(dirname "$manifest")"

    cat > "$manifest" << YAML
app-id: $APP_ID
runtime: $RUNTIME
runtime-version: "$RUNTIME_VERSION"
sdk: ${RUNTIME/Platform/Sdk}
command: $PACKAGE_NAME

finish-args:
  - --socket=wayland
  - --socket=fallback-x11
  - --share=ipc
  - --device=dri
  - --share=network
  - --filesystem=xdg-videos

rename-desktop-file: $PACKAGE_NAME.desktop
rename-icon: $ICON_NAME

modules:
  - name: $PACKAGE_NAME
    buildsystem: simple
    build-commands:
      - install -d /app/bin
      - install -Dm755 $install_dir/bin/$PACKAGE_NAME /app/bin/$PACKAGE_NAME
      - install -d /app/share
      - cp -a $install_dir/share/$PACKAGE_NAME /app/share/
      - install -Dm644 $install_dir/share/applications/$PACKAGE_NAME.desktop /app/share/applications/$PACKAGE_NAME.desktop
      - install -Dm644 $install_dir/share/icons/hicolor/scalable/apps/$ICON_NAME /app/share/icons/hicolor/scalable/apps/$ICON_NAME
      - sh -c 'if [ -d $install_dir/share/locale ]; then mkdir -p /app/share/locale && cp -a $install_dir/share/locale/. /app/share/locale/; fi'
    sources:
      - type: dir
        path: $PROJECT_ROOT
YAML

    rm -rf "$build_dir" "$repo_dir"
    mkdir -p "$repo_dir"

    if ! flatpak-builder \
        --force-clean \
        --repo="$repo_dir" \
        --arch="$(uname -m)" \
        --install-deps-from=flathub \
        "$build_dir" "$manifest"; then
        echo "[!] flatpak-builder failed" >&2
        return 1
    fi

    if ! flatpak build-bundle \
        --arch="$(uname -m)" \
        "$repo_dir" "$out_file" "$APP_ID" "$BRANCH"; then
        echo "[!] flatpak build-bundle failed" >&2
        return 1
    fi

    if [ ! -f "$out_file" ]; then
        echo "[!] Flatpak not created: $out_file" >&2
        return 1
    fi

    log "Flatpak: $out_file"
    rm -rf "$build_dir" "$repo_dir"
    return 0
}