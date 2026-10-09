#!/usr/bin/env bash
# =============================================================================
# build-flatpak.sh – Flatpak из готового install/
# =============================================================================
# Библиотека. Сорсится из build-package.sh.
#
# Не использует STAGING_DIR: Flatpak работает с install/ напрямую.
# Требует: flatpak-builder, flatpak в PATH, remote flathub настроен
# (flatpak remote-add --if-not-exists flathub \
#    https://flathub.org/repo/flathub.flatpakrepo).
# Runtime по умолчанию: org.gnome.Platform//47. При реализации проверить
# актуальную стабильную ветку на flathub (48/49).
#
# Артефакт: dist/iptvplayer-<ver>.flatpak
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-flatpak.sh — библиотека, не запускается напрямую." >&2
    exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
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

    echo "[+] Сборка Flatpak (runtime ${RUNTIME}//${RUNTIME_VERSION})..."

    if ! command -v flatpak-builder >/dev/null 2>&1; then
        echo "[!] flatpak-builder не найден. Установите: flatpak-builder" >&2
        return 1
    fi
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] не найден $install_dir/bin/$PACKAGE_NAME" >&2
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
  - --filesystem=xdg-download

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
        echo "[!] flatpak-builder упал" >&2
        return 1
    fi

    if ! flatpak build-bundle \
        --arch="$(uname -m)" \
        "$repo_dir" "$out_file" "$APP_ID" "$BRANCH"; then
        echo "[!] flatpak build-bundle упал" >&2
        return 1
    fi

    if [ ! -f "$out_file" ]; then
        echo "[!] Flatpak не создан: $out_file" >&2
        return 1
    fi

    echo "[✓] Flatpak: $out_file"
    rm -rf "$build_dir" "$repo_dir"
    return 0
}