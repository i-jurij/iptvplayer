#!/bin/bash
# =============================================
# build-package.sh – Сборка пакетов .deb, .rpm, .pkg.tar.zst, .AppImage
#
# Использование:
#   ./scripts/build-package.sh [ОПЦИИ]
#
# Варианты сборки:
#   --native-deb      Нативный .deb (системные библиотеки, Ubuntu/Debian)
#   --native-rpm      Нативный .rpm (Fedora/Rocky/RHEL/openSUSE)
#   --native-arch     Нативный .pkg.tar.zst (Arch/Manjaro)
#   --appimage        AppImage (linuxdeploy + appimagetool, bundled)
#   --sharun          AppImage (quick-sharun, максимальная переносимость)
#
# Bundled-варианты (обычно только для CI):
#   --bundle-deb      Bundled .deb (всё внутри /opt/iptvplayer)
#   --bundle-rpm      Bundled .rpm (всё внутри /opt/iptvplayer)
#
# Комбинированные:
#   --native          Все нативные пакеты, доступные здесь
#   --native-appimage Нативные + AppImage (linuxdeploy)
#   --bundle          Bundled .deb + bundled .rpm
#   --all             Всё возможное на этой системе
#
# Служебные:
#   --rebuild         Принудительно пересобрать бинарник
#   --clean           Очистить dist/ перед сборкой
#   --clean-only      Только очистить dist/
#   --no-menu         Не показывать меню (для скриптов)
#   --yes, -y         Неинтерактивный режим
#   -h, --help        Показать справку
#
# Environment для packagers (build-native.sh, build-bundle.sh, build-sharun.sh):
#   PROJECT_ROOT, SCRIPT_DIR
#   STAGING_DIR, APPDIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME, BUNDLE_PREFIX
#   APPIMAGE_ARCH, DEB_ARCH, RPM_ARCH, DISTRO
#   VERSION, VERSION_FILE
# Все эти переменные устанавливаются в main() до вызова packagers.
# =============================================

set -e

# ---- Каталог скриптов и корень проекта ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# ---- Общие утилиты и packagers ----
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/build-native.sh"
source "$SCRIPT_DIR/build-bundle.sh"
source "$SCRIPT_DIR/build-sharun.sh"

# === Настройки ===
PACKAGE_NAME="iptvplayer"
ICON_NAME="${PACKAGE_NAME}.svg"
BUNDLE_PREFIX="/opt/${PACKAGE_NAME}"

DEB_ARCH=""
RPM_ARCH=""
APPIMAGE_ARCH=""
DISTRO=""

# METAINFO_NAME
if [ ! -f "$PROJECT_ROOT/METAINFO_NAME" ]; then
    echo "[!] Файл $PROJECT_ROOT/METAINFO_NAME не найден."
    exit 1
fi
METAINFO_NAME="$(tr -d '\n\r' < "$PROJECT_ROOT/METAINFO_NAME" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
if [ -z "$METAINFO_NAME" ]; then
    echo "[!] Файл $PROJECT_ROOT/METAINFO_NAME пуст."
    exit 1
fi

BUILD_RELEASE_SCRIPT="$SCRIPT_DIR/build-release.sh"
OUTPUT_DIR="$PROJECT_ROOT/dist"
STAGING_DIR="$PROJECT_ROOT/pkg-staging"
APPDIR="$PROJECT_ROOT/dist/.AppDir"
FORCE_REBUILD=false
DO_CLEAN=false
CLEAN_ONLY=false

# === Очистка и подготовка ===
setup_dirs() {
    if [ "$DO_CLEAN" = true ]; then
        echo "[+] Очистка каталога $OUTPUT_DIR..."
        rm -rf "${OUTPUT_DIR:?}"/*
        mkdir -p "$OUTPUT_DIR"
    fi
    mkdir -p "$OUTPUT_DIR"
    rm -rf "$STAGING_DIR" "$APPDIR"
    mkdir -p "$STAGING_DIR"
}

# === Сборка бинарника ===
build_binary() {
    local BIN_PATH="$PROJECT_ROOT/install/bin/$PACKAGE_NAME"
    if [ -f "$BIN_PATH" ] && [ "$FORCE_REBUILD" = false ]; then
        echo "[+] Бинарник уже собран: $BIN_PATH"
        if ask "Использовать существующий? [Y/n]:" y; then
            echo "[+] Используем существующий бинарник."
            return 0
        fi
        FORCE_REBUILD=true
    fi
    echo "[+] Сборка через $BUILD_RELEASE_SCRIPT..."
    if [ ! -f "$BUILD_RELEASE_SCRIPT" ]; then
        echo "[!] $BUILD_RELEASE_SCRIPT не найден." >&2
        return 1
    fi
    if ! "$BUILD_RELEASE_SCRIPT" --type release --prefix "$PROJECT_ROOT/install" --yes; then
        echo "[!] $BUILD_RELEASE_SCRIPT завершился с ошибкой" >&2
        return 1
    fi
    echo "[+] Бинарник собран."
    return 0
}

# === Очистка ===
cleanup() {
    echo "[+] Очистка временных каталогов..."
    rm -rf "$STAGING_DIR" "$APPDIR" "$PROJECT_ROOT/pkg-rpm"
}
trap cleanup EXIT INT TERM

# =============================================================================
#                               ПОДПИСЬ
# =============================================================================
sign_files() {
    local dist_dir="$OUTPUT_DIR"
    local checksum_basename="checksums.txt"
    local signature_basename="${checksum_basename}.asc"
    local checksum_file="$dist_dir/$checksum_basename"
    local signature_file="$dist_dir/$signature_basename"

    local can_sign=false
    if ! command -v gpg >/dev/null 2>&1; then
        echo "[!] gpg не установлен."
    elif [ -z "${GPG_KEY_ID:-}" ]; then
        if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
            echo "::warning::GPG_KEY_ID не задан."
        else
            echo "[!] GPG_KEY_ID не задан."
        fi
    else
        can_sign=true
    fi

    if [ "$can_sign" = true ]; then
        if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -x "$GPG_WRAPPER_DIR/gpg" ]; then
            export PATH="$GPG_WRAPPER_DIR:$PATH"
        fi
        echo "[+] Подпись пакетов (ключ: $GPG_KEY_ID)..."

        # .deb через debsigs
        if command -v debsigs >/dev/null 2>&1; then
            for file in "$dist_dir"/*.deb; do
                [ -f "$file" ] || continue
                debsigs --sign=origin --default-key="$GPG_KEY_ID" "$file" \
                  || gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" || true
            done
        fi

        # .rpm через rpm --addsign (без правки ~/.rpmmacros)
        if command -v rpm >/dev/null 2>&1; then
            local real_gpg; real_gpg="$(command -v gpg)"
            local sign_cmd
            sign_cmd='%{__gpg} --batch --pinentry-mode loopback --passphrase "" -u "%{_gpg_name}" -sbo %{__signature_filename} %{__plaintext_filename}'

            for file in "$dist_dir"/*.rpm; do
                [ -f "$file" ] || continue
                rpm --addsign \
                    --define "_gpg_name $GPG_KEY_ID" \
                    --define "_signature gpg" \
                    --define "__gpg $real_gpg" \
                    --define "__gpg_check_password_cmd /bin/true" \
                    --define "__gpg_sign_cmd $sign_cmd" \
                    "$file" 2>/dev/null \
                  || gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" || true
            done
        fi

        # AppImage (detached)
        for file in "$dist_dir"/*.AppImage; do
            [ -f "$file" ] || continue
            gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" || true
        done
    fi

    # checksums
    echo "[+] Генерация checksums.txt..."
    rm -f "$checksum_file" "$signature_file"
    (
        cd "$dist_dir" || exit 1
        shopt -s nullglob
        files=()
        for f in *; do
            case "$f" in "$checksum_basename"|"$signature_basename") continue ;; esac
            [ -f "$f" ] || continue
            files+=("$f")
        done
        (( ${#files[@]} > 0 )) && sha256sum "${files[@]}"
    ) > "$checksum_file" || true

    if [ "$can_sign" = true ]; then
        gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" \
            --output "$signature_file" "$checksum_file" || true
    fi
}

# =============================================================================
#                              МЕНЮ / СПРАВКА
# =============================================================================
show_help() {
    cat << EOF
Использование: ./scripts/build-package.sh [ОПЦИИ]

Нативные:
  --native-deb      .deb из системных библиотек (Ubuntu/Debian)
  --native-rpm      .rpm из системных библиотек (Fedora/Rocky/RHEL/openSUSE)
  --native-arch     .pkg.tar.zst (Arch/Manjaro)
  --appimage        AppImage (bundled, работает везде)
  --sharun          AppImage через quick-sharun (максимальная переносимость:
                    старые glibc, musl-системы, NixOS)

Bundled (обычно только для CI):
  --bundle-deb      bundled .deb (всё в /opt/iptvplayer)
  --bundle-rpm      bundled .rpm

Комбинированные:
  --native          все нативные, доступные здесь
  --native-appimage нативные + AppImage
  --bundle          bundled .deb + bundled .rpm
  --all             всё возможное на этой системе

Служебные:
  --rebuild         пересобрать бинарник
  --clean           очистить dist/
  --clean-only      только очистить dist/
  --no-menu         не показывать меню
  --yes, -y         неинтерактивный
  -h, --help        эта справка
EOF
}

# === Интерактивное меню ===
show_menu() {
    local pkgmgr="$1"
    echo ""
    echo "Обнаружена система: $DISTRO ($(uname -m))"
    echo ""

    # Собираем список опций; для каждой храним action.
    local -a labels=()
    local -a actions=()

    local native_label=""
    case "$pkgmgr" in
        deb)  native_label="Нативный .deb (системные библиотеки)" ;;
        rpm)  native_label="Нативный .rpm (системные библиотеки)" ;;
        arch) native_label="Нативный .pkg.tar.zst (системные библиотеки)" ;;
    esac

    if [ -n "$native_label" ]; then
        labels+=("$native_label")
        actions+=("native")
    fi

    labels+=("AppImage (linuxdeploy — классический пайплайн)")
    actions+=("appimage")

    labels+=("AppImage (quick-sharun — максимальная переносимость)")
    actions+=("sharun")

    if [ -n "$native_label" ]; then
        labels+=("Родной пакет + AppImage (linuxdeploy)")
        actions+=("native+appimage")

        labels+=("Родной пакет + AppImage (quick-sharun)")
        actions+=("native+sharun")
    fi

    echo "Выберите, что собрать:"
    local i=1
    for opt in "${labels[@]}"; do
        echo "  $i) $opt"
        i=$((i+1))
    done
    echo "  0) Отмена"
    echo ""

    local choice=""
    read -p "Введите номер: " choice

    [ "$choice" = "0" ] && exit 0
    if ! [[ "$choice" =~ ^[0-9]+$ ]] || \
       [ "$choice" -lt 1 ] || \
       [ "$choice" -gt "${#actions[@]}" ]; then
        echo "Неверный выбор"; exit 1
    fi

    local action="${actions[$((choice-1))]}"

    case "$action" in
        native)
            case "$pkgmgr" in
                deb)  BUILD_NATIVE_DEB=true ;;
                rpm)  BUILD_NATIVE_RPM=true ;;
                arch) BUILD_NATIVE_ARCH=true ;;
            esac
            ;;
        appimage) BUILD_APPIMAGE=true ;;
        sharun)   BUILD_SHARUN=true ;;
        native+appimage)
            case "$pkgmgr" in
                deb)  BUILD_NATIVE_DEB=true ;;
                rpm)  BUILD_NATIVE_RPM=true ;;
                arch) BUILD_NATIVE_ARCH=true ;;
            esac
            BUILD_APPIMAGE=true
            ;;
        native+sharun)
            case "$pkgmgr" in
                deb)  BUILD_NATIVE_DEB=true ;;
                rpm)  BUILD_NATIVE_RPM=true ;;
                arch) BUILD_NATIVE_ARCH=true ;;
            esac
            BUILD_SHARUN=true
            ;;
    esac
}

# =============================================================================
#                              MAIN
# =============================================================================
main() {
    BUILD_NATIVE_DEB=false
    BUILD_NATIVE_RPM=false
    BUILD_NATIVE_ARCH=false
    BUILD_BUNDLE_DEB=false
    BUILD_BUNDLE_RPM=false
    BUILD_APPIMAGE=false
    BUILD_SHARUN=false
    SHOW_MENU=true

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --native-deb)    BUILD_NATIVE_DEB=true ;;
            --native-rpm)    BUILD_NATIVE_RPM=true ;;
            --native-arch)   BUILD_NATIVE_ARCH=true ;;
            --bundle-deb)    BUILD_BUNDLE_DEB=true ;;
            --bundle-rpm)    BUILD_BUNDLE_RPM=true ;;
            --appimage)      BUILD_APPIMAGE=true ;;
            --sharun)        BUILD_SHARUN=true ;;
            --native)        BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true ;;
            --native-appimage) BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true; BUILD_APPIMAGE=true ;;
            --bundle)        BUILD_BUNDLE_DEB=true; BUILD_BUNDLE_RPM=true ;;
            --all)           BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true; BUILD_BUNDLE_DEB=true; BUILD_BUNDLE_RPM=true; BUILD_APPIMAGE=true ; BUILD_SHARUN=true ;;
            --rebuild)       FORCE_REBUILD=true ;;
            --clean)         DO_CLEAN=true ;;
            --clean-only)    DO_CLEAN=true; CLEAN_ONLY=true ;;
            --no-menu)       SHOW_MENU=false ;;
            --yes|-y)        NON_INTERACTIVE=true; SHOW_MENU=false ;;
            -h|--help)       show_help; exit 0 ;;
            *) echo "Неизвестный аргумент: $1"; show_help; exit 1 ;;
        esac
        shift
    done

    if [[ "$CLEAN_ONLY" == true ]]; then
        echo "[+] Очистка $OUTPUT_DIR..."
        find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        mkdir -p "$OUTPUT_DIR"
        exit 0
    fi

    detect_arch
    detect_distro

    local pkgmgr; pkgmgr="$(detect_pkgmgr)"
    echo "[i] Пакетный менеджер: $pkgmgr"

    # Если ничего не выбрано и меню разрешено — показать меню
    if [[ "$BUILD_NATIVE_DEB" == false && "$BUILD_NATIVE_RPM" == false && \
          "$BUILD_NATIVE_ARCH" == false && "$BUILD_BUNDLE_DEB" == false && \
          "$BUILD_BUNDLE_RPM" == false && "$BUILD_APPIMAGE" == false && \
          "$BUILD_SHARUN" == false ]]; then
        if [[ "$SHOW_MENU" == true && "$NON_INTERACTIVE" == false ]]; then
            show_menu "$pkgmgr"
        else
            # В CI или --no-menu без флагов — по умолчанию bundle + appimage
            BUILD_BUNDLE_DEB=true
            BUILD_BUNDLE_RPM=true
            BUILD_APPIMAGE=true
        fi
    fi

    # Bundled требует AppDir — если запрошены bundle и AppImage, populate один раз
    check_deps "$pkgmgr" \
               "$BUILD_NATIVE_DEB" "$BUILD_NATIVE_RPM" "$BUILD_NATIVE_ARCH" \
               "$BUILD_BUNDLE_DEB" "$BUILD_BUNDLE_RPM" "$BUILD_APPIMAGE" "$BUILD_SHARUN"
    setup_dirs

    if ! build_binary; then
        echo "[!] Бинарник не собран — выходим." >&2
        exit 1
    fi

    { read -r VERSION_DISPLAY; read -r VERSION_FILE; read -r VERSION; } \
        < <(read_versions_from_install)
    if [ -z "$VERSION_DISPLAY" ] || [ -z "$VERSION_FILE" ] || [ -z "$VERSION" ]; then
        echo "[ERROR] Не удалось прочитать версии." >&2
        exit 1
    fi

    echo "=== $PACKAGE_NAME:$VERSION (файл: $VERSION_FILE, arch: $DEB_ARCH/$RPM_ARCH/$APPIMAGE_ARCH, distro: $DISTRO) ==="

    local FAILED=()

    if [[ "$BUILD_NATIVE_DEB" == true ]]; then
        if [[ "$pkgmgr" == "deb" ]]; then
            if ! prepare_staging; then
                FAILED+=("native-deb")
            elif ! build_deb_native; then
                FAILED+=("native-deb")
            fi
        else
            echo "[!] --native-deb недоступен на $DISTRO — пропускаем."
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM" == true ]]; then
        if [[ "$pkgmgr" == "rpm" ]]; then
            if ! prepare_staging; then
                FAILED+=("native-rpm")
            elif ! build_rpm_native; then
                FAILED+=("native-rpm")
            fi
        else
            echo "[!] --native-rpm недоступен на $DISTRO — пропускаем."
        fi
    fi
    if [[ "$BUILD_NATIVE_ARCH" == true ]]; then
        if [[ "$pkgmgr" == "arch" ]]; then
            if ! prepare_staging; then
                FAILED+=("native-arch")
            elif ! build_pkg_arch; then
                FAILED+=("native-arch")
            fi
        else
            echo "[!] --native-arch недоступен на $DISTRO — пропускаем."
        fi
    fi

    if [[ "$BUILD_BUNDLE_DEB" == true ]]; then
        if ! build_deb_bundled; then FAILED+=("bundle-deb"); fi
    fi
    if [[ "$BUILD_BUNDLE_RPM" == true ]]; then
        if ! build_rpm_bundled; then FAILED+=("bundle-rpm"); fi
    fi
    if [[ "$BUILD_APPIMAGE" == true ]]; then
        if ! build_appimage; then FAILED+=("appimage"); fi
    fi
    if [[ "$BUILD_SHARUN" == true ]]; then
        if ! build_sharun_appimage; then FAILED+=("sharun"); fi
    fi

    sign_files

    echo ""
    if (( ${#FAILED[@]} > 0 )); then
        echo "[!] Не собраны: ${FAILED[*]}" >&2
        echo "[i] Что удалось собрать в '$OUTPUT_DIR':"
        ls -la "$OUTPUT_DIR/"
        exit 1
    fi

    echo "🎉 Готово! Артефакты в '$OUTPUT_DIR':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"