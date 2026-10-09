#!/bin/sh
# =============================================================================
# build-package.sh – упаковка .deb, .rpm, .pkg.tar.zst, .AppImage
# =============================================================================

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/bootstrap-bash.sh" || exit 1

if [ -z "${BASH_VERSION:-}" ]; then
    echo "[!] bootstrap-bash.sh не передал управление bash." >&2
    exit 1
fi

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/build-rpm-common.sh"
source "$SCRIPT_DIR/build-deb-common.sh"
source "$SCRIPT_DIR/build-native-rpm.sh"
source "$SCRIPT_DIR/build-native-rpm-oracle.sh"
source "$SCRIPT_DIR/build-native-rpm-redos.sh"
source "$SCRIPT_DIR/build-native-rpm-suse.sh"
source "$SCRIPT_DIR/build-native-rpm-alt.sh"
source "$SCRIPT_DIR/build-native-rpm-rosa.sh"
source "$SCRIPT_DIR/build-native-rpm-mageia.sh"
source "$SCRIPT_DIR/build-native-rpm-openmandriva.sh"
source "$SCRIPT_DIR/build-native-deb.sh"
source "$SCRIPT_DIR/build-native-arch.sh"
source "$SCRIPT_DIR/build-appimage.sh"
source "$SCRIPT_DIR/build-sharun.sh"
source "$SCRIPT_DIR/build-flatpak.sh"
source "$SCRIPT_DIR/build-apk-common.sh"
source "$SCRIPT_DIR/build-native-apk-alpine.sh"
source "$SCRIPT_DIR/build-native-slackbuild.sh"
source "$SCRIPT_DIR/build-native-xbps-void.sh"
source "$SCRIPT_DIR/build-native-gentoo.sh"
source "$SCRIPT_DIR/build-native-nix.sh"

PACKAGE_NAME="iptvplayer"
ICON_NAME="${PACKAGE_NAME}.svg"

DEB_ARCH=""; RPM_ARCH=""; APPIMAGE_ARCH=""; DISTRO=""

if [ ! -f "$PROJECT_ROOT/METAINFO_NAME" ]; then
    echo "[!] Файл $PROJECT_ROOT/METAINFO_NAME не найден."; exit 1
fi
METAINFO_NAME="$(tr -d '\n\r' < "$PROJECT_ROOT/METAINFO_NAME" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
[ -z "$METAINFO_NAME" ] && { echo "[!] METAINFO_NAME пуст."; exit 1; }

BUILD_RELEASE_SCRIPT="$SCRIPT_DIR/build-release.sh"
OUTPUT_DIR="$PROJECT_ROOT/dist"
STAGING_DIR="$PROJECT_ROOT/pkg-staging"
APPDIR="$PROJECT_ROOT/dist/.AppDir"
FORCE_REBUILD=false
DO_CLEAN=false
CLEAN_ONLY=false
KEEP_DEPS=false

setup_dirs() {
    if [ "$DO_CLEAN" = true ]; then
        echo "[+] Очистка $OUTPUT_DIR..."
        rm -rf "${OUTPUT_DIR:?}"/*
        mkdir -p "$OUTPUT_DIR"
    fi
    mkdir -p "$OUTPUT_DIR"
    rm -rf "$STAGING_DIR" "$APPDIR"
    mkdir -p "$STAGING_DIR"
}

build_binary() {
    echo "[+] Сборка через $BUILD_RELEASE_SCRIPT..."
    [ -f "$BUILD_RELEASE_SCRIPT" ] || { echo "[!] $BUILD_RELEASE_SCRIPT не найден." >&2; return 1; }
    local args=(--type release --prefix "$PROJECT_ROOT/install")
    [[ "$FORCE_REBUILD" == true ]] && args+=(--clean)
    [[ "$NON_INTERACTIVE" == true ]] && args+=(--yes)
    [[ "$KEEP_DEPS" == true ]] && args+=(--keep-deps)
    "$BUILD_RELEASE_SCRIPT" "${args[@]}" || { echo "[!] $BUILD_RELEASE_SCRIPT упал" >&2; return 1; }
    echo "[+] Бинарник собран."
}

cleanup() {
    echo "[+] Очистка временных каталогов..."
    rm -rf "${STAGING_DIR:?}" "${APPDIR:?}"
    rm -rf "$PROJECT_ROOT"/pkg-rpm*
    rm -rf "$PROJECT_ROOT/pkg-apk"
    rm -rf "$PROJECT_ROOT/pkg-slackbuild"
    rm -rf "$PROJECT_ROOT/pkg-xbps"
    rm -rf "$PROJECT_ROOT/pkg-gentoo"
    rm -rf "$PROJECT_ROOT/pkg-nix"
    rm -rf "$PROJECT_ROOT/AppDir"
    rm -f  "${OUTPUT_DIR:?}/appinfo"
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
        # В CI release.yml/native-packages.yml кладут в $GPG_WRAPPER_DIR
        # обёртку gpg, которая сама добавляет --batch --no-tty
        # --pinentry-mode loopback --passphrase-file <файл>. Используем её
        # ТОЛЬКО для detached-подписей, которые вызываем сами (.asc,
        # checksums.txt.asc).
        #
        # RPM получает passphrase-file внутри __gpg_sign_cmd и запускается
        # через реальный /usr/bin/gpg. Если дать RPM обёртку из PATH,
        # опции наложатся дважды (RPM + обёртка), и rpm --checksig потом
        # скажет SIGNATURES NOT OK
        local gpg_sign="gpg"
        if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -x "$GPG_WRAPPER_DIR/gpg" ]; then
            gpg_sign="$GPG_WRAPPER_DIR/gpg"
        fi

        local gpg_real="/usr/bin/gpg"
        [ -x "$gpg_real" ] || gpg_real="$(command -v gpg)"

        echo "[+] Подпись пакетов (ключ: $GPG_KEY_ID)..."

        # --- .deb: встроенная подпись через debsigs + detached .asc ---
        for file in "$dist_dir"/*.deb; do
            [ -f "$file" ] || continue
            if command -v debsigs >/dev/null 2>&1; then
                if debsigs --sign=origin --default-key="$GPG_KEY_ID" "$file"; then
                    if ! debsigs --verify "$file" >/dev/null 2>&1; then
                        warn "debsigs не подтвердил подпись $(basename "$file") — .deb уедет без встроенной подписи"
                    fi
                else
                    warn "debsigs упал на $(basename "$file") — .deb уедет без встроенной подписи"
                fi
            else
                warn "debsigs не установлен — .deb будет только с detached .asc"
            fi

            "$gpg_sign" --yes --detach-sign --armor \
                --local-user "$GPG_KEY_ID" \
                --output "$file.asc" "$file" \
              || warn "detached .asc для $(basename "$file") не создан"
        done

        # --- .rpm: встроенная подпись через rpm --addsign + detached .asc ---
        # Локально: passphrase-file нет, работает системный gpg-agent+pinentry.
        # В CI: passphrase передаётся явно в __gpg_sign_cmd, gpg — реальный.
        if command -v rpm >/dev/null 2>&1; then
            local passphrase_file=""
            if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -f "$GPG_WRAPPER_DIR/pass" ]; then
                passphrase_file="$GPG_WRAPPER_DIR/pass"
            fi

            local sign_cmd='%{__gpg} -u "%{_gpg_name}" -sbo %{__signature_filename} %{__plaintext_filename}'
            if [ -n "$passphrase_file" ]; then
                sign_cmd="%{__gpg} --batch --no-tty --pinentry-mode loopback --passphrase-file $passphrase_file -u \"%{_gpg_name}\" -sbo %{__signature_filename} %{__plaintext_filename}"
            fi

            for file in "$dist_dir"/*.rpm; do
                [ -f "$file" ] || continue
                if rpm --addsign \
                    --define "_gpg_name $GPG_KEY_ID" \
                    --define "_signature gpg" \
                    --define "__gpg $gpg_real" \
                    --define "__gpg_check_password_cmd /bin/true" \
                    --define "__gpg_sign_cmd $sign_cmd" \
                    "$file"; then
                    if rpm --checksig "$file" 2>&1 | grep -q 'signatures OK'; then
                        log "rpm --checksig OK: $(basename "$file")"
                    else
                        warn "rpm --checksig НЕ подтвердил $(basename "$file") — .rpm уедет без встроенной подписи"
                    fi
                else
                    warn "rpm --addsign упал на $(basename "$file") — .rpm уедет без встроенной подписи"
                fi

                "$gpg_sign" --yes --detach-sign --armor \
                    --local-user "$GPG_KEY_ID" \
                    --output "$file.asc" "$file" \
                  || warn "detached .asc для $(basename "$file") не создан"
            done
        fi

        # --- AppImage: только detached .asc ---
        for file in "$dist_dir"/*.AppImage; do
            [ -f "$file" ] || continue
            "$gpg_sign" --yes --detach-sign --armor \
                --local-user "$GPG_KEY_ID" \
                --output "$file.asc" "$file" \
              || warn "detached .asc для $(basename "$file") не создан"
        done
    fi

    # checksums — генерируются и локально, и в CI (per-job).
    # В release.yml полный checksums.txt пересчитывается поверх всех
    # скачанных артефактов и перезаписывает этот файл.
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
        "$gpg_sign" --yes --detach-sign --armor \
            --local-user "$GPG_KEY_ID" \
            --output "$signature_file" "$checksum_file" || true
    fi
}

show_help() {
    cat << EOF
Использование: ./scripts/build-package.sh [ОПЦИИ]

Нативные:
  --native          автоопределение: соберёт пакет(ы) для текущей системы
  --native-deb      .deb (Debian/Ubuntu/Mint/Pop/Astra)
  --native-rpm      .rpm (Fedora/RHEL/Rocky/Alma/CentOS)
  --native-rpm-oracle       .rpm (Oracle Linux, .elN)
  --native-rpm-redos        .rpm (RedOS, .redN)
  --native-rpm-suse         .rpm (openSUSE/SLES, 0.suseN)
  --native-rpm-alt          .rpm (ALT Linux, alt1)
  --native-rpm-rosa         .rpm (ROSA Linux, 1.rosaN)
  --native-rpm-mageia       .rpm (Mageia, 1.mgaN)
  --native-rpm-openmandriva .rpm (OpenMandriva, omvN)
  --native-arch     .pkg.tar.zst (Arch/Manjaro)
  --appimage        AppImage (linuxdeploy)
  --sharun          AppImage (quick-sharun)
  --flatpak         Flatpak-бандл (требует flatpak-builder)
  --native-apk      .apk (Alpine Linux, только в Alpine-окружении)
  --native-slackbuild  .txz (Slackware)
  --native-xbps        .xbps (Void Linux, требует клон void-packages)
  --native-gentoo      .tbz2 (Gentoo Linux, требует sys-apps/portage)
  --native-nix         derivation (NixOS, требует nix в PATH)

Алиасы (сохранены):
  --native-alt      = --native-rpm-alt
  --native-rosa     = --native-rpm-rosa

Комбинированные:
  --native-appimage нативные + AppImage
  --all             всё возможное

Служебные:
  --rebuild --clean --clean-only --no-menu --yes|-y
  --keep-deps       не удалять пакеты, поставленные setup-deps.sh
  -h|--help
EOF
}

# Диспатч по DISTRO (приоритет) → pkgmgr (фоллбэк).
# Покрывает все RPM-семейства этапа 1.
_dispatch_native() {
    case "$DISTRO" in
            rosa-*)                    BUILD_NATIVE_ROSA=true ;;
            ol-*|oracle-*)             BUILD_NATIVE_RPM_ORACLE=true ;;
            redos-*)                   BUILD_NATIVE_RPM_REDOS=true ;;
            mageia-*)                  BUILD_NATIVE_RPM_MAGEIA=true ;;
            openmandriva-*)            BUILD_NATIVE_RPM_OPENMANDRIVA=true ;;
            opensuse*|sles*)           BUILD_NATIVE_RPM_SUSE=true ;;
            alt-*|alt|altlinux*)       BUILD_NATIVE_ALT=true ;;
            alpine*)                   BUILD_NATIVE_APK=true ;;
            slackware*)                BUILD_NATIVE_SLACKBUILD=true ;;
            void*)                     BUILD_NATIVE_XBPS=true ;;
            gentoo*)                   BUILD_NATIVE_GENTOO=true ;;
            nixos*)                    BUILD_NATIVE_NIX=true ;;
            *)
        case "$pkgmgr" in
                apt)    BUILD_NATIVE_DEB=true ;;
                dnf)    BUILD_NATIVE_RPM=true ;;
                pacman) BUILD_NATIVE_ARCH=true ;;
        esac ;;
    esac
}

show_menu() {
    local pkgmgr="$1"
    echo ""
    echo "Обнаружена система: $DISTRO ($(uname -m))"
    echo ""

    local -a labels=() actions=()
    local native_label=""

    # Приоритет — DISTRO. pkgmgr даёт только общий фоллбэк для семейств.
    case "$DISTRO" in
        rosa-*)       native_label="Нативный .rpm для ROSA" ;;
        alpine-*)     native_label="Нативный .apk" ;;
        slackware*)   native_label="Нативный .txz" ;;
        void*)        native_label="Нативный .xbps" ;;
        gentoo*)      native_label="Нативный .tbz2" ;;
        nixos*)       native_label="Нативный Nix derivation" ;;
        ol-*|oracle-*) native_label="Нативный .rpm для Oracle Linux" ;;
        redos-*)      native_label="Нативный .rpm для RedOS" ;;
        mageia-*)     native_label="Нативный .rpm для Mageia" ;;
        openmandriva-*) native_label="Нативный .rpm для OpenMandriva" ;;
        opensuse*|sles*) native_label="Нативный .rpm для openSUSE" ;;
        alt-*|alt|altlinux*) native_label="Нативный .rpm для ALT" ;;
        *)
            case "$pkgmgr" in
                apt)     native_label="Нативный .deb" ;;
                dnf)     native_label="Нативный .rpm" ;;
                pacman)  native_label="Нативный .pkg.tar.zst" ;;
            esac ;;
    esac

    [ -n "$native_label" ] && { labels+=("$native_label (системные библиотеки)"); actions+=("native"); }
    labels+=("AppImage (linuxdeploy)"); actions+=("appimage")
    labels+=("AppImage (quick-sharun)"); actions+=("sharun")
    labels+=("Flatpak"); actions+=("flatpak")

    if [ -n "$native_label" ]; then
        labels+=("Родной + AppImage (linuxdeploy)"); actions+=("native+appimage")
        labels+=("Родной + AppImage (quick-sharun)");  actions+=("native+sharun")
    fi

    echo "Выберите, что собрать:"

    local i=1
    for opt in "${labels[@]}"; do echo "  $i) $opt"; i=$((i+1)); done

    echo "  0) Отмена"; echo ""

    local choice=""; read -p "Введите номер: " choice

    [ "$choice" = "0" ] && exit 0

    if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt "${#actions[@]}" ]; then
        echo "Неверный выбор"; exit 1
    fi

    local action="${actions[$((choice-1))]}"

    case "$action" in
        native|native+appimage|native+sharun)
            _dispatch_native
            [[ "$action" == "native+appimage" ]] && BUILD_APPIMAGE=true
            [[ "$action" == "native+sharun" ]]   && BUILD_SHARUN=true
            ;;
        appimage) BUILD_APPIMAGE=true ;;
        sharun)   BUILD_SHARUN=true ;;
        flatpak)  BUILD_FLATPAK=true ;;
    esac

    return 0
}

main() {
    BUILD_NATIVE_DEB=false
    BUILD_NATIVE_RPM=false
    BUILD_NATIVE_ARCH=false
    BUILD_NATIVE_ALT=false
    BUILD_NATIVE_ROSA=false
    BUILD_NATIVE_RPM_ORACLE=false
    BUILD_NATIVE_RPM_REDOS=false
    BUILD_NATIVE_RPM_SUSE=false
    BUILD_NATIVE_RPM_MAGEIA=false
    BUILD_NATIVE_RPM_OPENMANDRIVA=false
    BUILD_APPIMAGE=false
    BUILD_SHARUN=false
    BUILD_FLATPAK=false
    BUILD_NATIVE_APK=false
    BUILD_NATIVE_SLACKBUILD=false
    BUILD_NATIVE_XBPS=false
    BUILD_NATIVE_GENTOO=false
    BUILD_NATIVE_NIX=false
    NATIVE_AUTO=false
    SHOW_MENU=true

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --native-deb)              BUILD_NATIVE_DEB=true ;;
            --native-rpm)              BUILD_NATIVE_RPM=true ;;
            --native-arch)             BUILD_NATIVE_ARCH=true ;;
            --native-alt|--native-rpm-alt)   BUILD_NATIVE_ALT=true ;;
            --native-rosa|--native-rpm-rosa) BUILD_NATIVE_ROSA=true ;;
            --native-rpm-oracle)       BUILD_NATIVE_RPM_ORACLE=true ;;
            --native-rpm-redos)        BUILD_NATIVE_RPM_REDOS=true ;;
            --native-rpm-suse)         BUILD_NATIVE_RPM_SUSE=true ;;
            --native-rpm-mageia)       BUILD_NATIVE_RPM_MAGEIA=true ;;
            --native-rpm-openmandriva) BUILD_NATIVE_RPM_OPENMANDRIVA=true ;;
            --appimage)                BUILD_APPIMAGE=true ;;
            --sharun)                  BUILD_SHARUN=true ;;
            --flatpak)                 BUILD_FLATPAK=true ;;
            --native-apk)              BUILD_NATIVE_APK=true ;;
            --native-slackbuild)       BUILD_NATIVE_SLACKBUILD=true ;;
            --native-xbps)             BUILD_NATIVE_XBPS=true ;;
            --native-gentoo)           BUILD_NATIVE_GENTOO=true ;;
            --native-nix)              BUILD_NATIVE_NIX=true ;;
            --native)
                NATIVE_AUTO=true ;;
            --native-appimage)
                NATIVE_AUTO=true; BUILD_APPIMAGE=true ;;
            --all)
                NATIVE_AUTO=true
                BUILD_APPIMAGE=true; BUILD_SHARUN=true; BUILD_FLATPAK=true ;;
            --rebuild)    FORCE_REBUILD=true ;;
            --keep-deps|--no-cleanup-deps) KEEP_DEPS=true ;;
            --clean)      DO_CLEAN=true ;;
            --clean-only) DO_CLEAN=true; CLEAN_ONLY=true ;;
            --no-menu)    SHOW_MENU=false ;;
            --yes|-y)     NON_INTERACTIVE=true; SHOW_MENU=false ;;
            -h|--help)    show_help; exit 0 ;;
            *) echo "Неизвестный аргумент: $1"; show_help; exit 1 ;;
        esac
        shift
    done

    if [[ "$CLEAN_ONLY" == true ]]; then
        echo "[+] Очистка $OUTPUT_DIR..."
        find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        mkdir -p "$OUTPUT_DIR"; exit 0
    fi

    detect_arch
    detect_distro
    local pkgmgr; pkgmgr="$(detect_pkgmgr)"
    echo "[i] Пакетный менеджер: $pkgmgr"

    # --native: выбираем ОДИН адаптер по DISTRO → pkgmgr. Выставлять
    # все флаги сразу нельзя — check_deps упадёт на первом отсутствующем
    # инструменте чужого дистрибутива.
    if [[ "$NATIVE_AUTO" == true ]]; then
        case "$DISTRO" in
            rosa-*)                    BUILD_NATIVE_ROSA=true ;;
            alpine*)                   BUILD_NATIVE_APK=true ;;
            slackware*)                BUILD_NATIVE_SLACKBUILD=true ;;
            void*)                     BUILD_NATIVE_XBPS=true ;;
            gentoo*)                   BUILD_NATIVE_GENTOO=true ;;
            nixos*)                    BUILD_NATIVE_NIX=true ;;
            ol-*|oracle-*)             BUILD_NATIVE_RPM_ORACLE=true ;;
            redos-*)                   BUILD_NATIVE_RPM_REDOS=true ;;
            mageia-*)                  BUILD_NATIVE_RPM_MAGEIA=true ;;
            openmandriva-*)            BUILD_NATIVE_RPM_OPENMANDRIVA=true ;;
            opensuse*|sles*)           BUILD_NATIVE_RPM_SUSE=true ;;
            alt-*|alt|altlinux*)       BUILD_NATIVE_ALT=true ;;
            *)
                case "$pkgmgr" in
                    apt)    BUILD_NATIVE_DEB=true ;;
                    dnf)    BUILD_NATIVE_RPM=true ;;
                    pacman) BUILD_NATIVE_ARCH=true ;;
                    *)      echo "[!] --native: неизвестная система $DISTRO" >&2
                            exit 1 ;;
                esac ;;
        esac
    fi

    local any=false
    for v in BUILD_NATIVE_DEB BUILD_NATIVE_RPM BUILD_NATIVE_ARCH \
             BUILD_NATIVE_ALT BUILD_NATIVE_ROSA \
             BUILD_NATIVE_RPM_ORACLE BUILD_NATIVE_RPM_REDOS \
             BUILD_NATIVE_RPM_SUSE BUILD_NATIVE_RPM_MAGEIA \
             BUILD_NATIVE_RPM_OPENMANDRIVA BUILD_APPIMAGE BUILD_SHARUN \
             BUILD_FLATPAK BUILD_NATIVE_APK BUILD_NATIVE_SLACKBUILD \
             BUILD_NATIVE_XBPS BUILD_NATIVE_GENTOO BUILD_NATIVE_NIX; do
        [[ "${!v}" == true ]] && any=true
    done

    if [[ "$any" == false ]]; then
        if [[ "$SHOW_MENU" == true && "$NON_INTERACTIVE" == false ]]; then
            show_menu "$pkgmgr"
        else
            BUILD_APPIMAGE=true
        fi
    fi

    check_deps "$pkgmgr" \
        "$BUILD_NATIVE_DEB" "$BUILD_NATIVE_RPM" "$BUILD_NATIVE_ARCH" \
        "$BUILD_NATIVE_ALT" "$BUILD_NATIVE_ROSA" \
        "$BUILD_NATIVE_RPM_ORACLE" "$BUILD_NATIVE_RPM_REDOS" \
        "$BUILD_NATIVE_RPM_SUSE" "$BUILD_NATIVE_RPM_MAGEIA" \
        "$BUILD_NATIVE_RPM_OPENMANDRIVA" \
        "$BUILD_APPIMAGE" "$BUILD_SHARUN" "$BUILD_FLATPAK" \
        "$BUILD_NATIVE_APK" "$BUILD_NATIVE_SLACKBUILD" \
        "$BUILD_NATIVE_XBPS" "$BUILD_NATIVE_GENTOO" \
        "$BUILD_NATIVE_NIX"
    setup_dirs

    build_binary || { echo "[!] Бинарник не собран — выходим." >&2; exit 1; }

    if ! { read -r VERSION_DISPLAY; read -r VERSION_FILE; read -r VERSION; } \
        < <(read_versions_from_install); then
        echo "[ERROR] Не удалось прочитать версии." >&2
        exit 1
    fi
    if [ -z "$VERSION_DISPLAY" ] || [ -z "$VERSION_FILE" ] || [ -z "$VERSION" ]; then
        echo "[ERROR] Не удалось прочитать версии." >&2
        exit 1
    fi

    echo "=== $PACKAGE_NAME:$VERSION (файл: $VERSION_FILE, arch: $DEB_ARCH/$RPM_ARCH/$APPIMAGE_ARCH, distro: $DISTRO) ==="

    local FAILED=()

    if [[ "$BUILD_NATIVE_DEB" == true ]]; then
        if distro_is 'ubuntu-*' 'debian-*' 'linuxmint-*' 'pop-*' 'astra-*'; then
            prepare_staging && build_deb_native || FAILED+=("native-deb")
        else
            echo "[i] --native-deb: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM" == true ]]; then
        if distro_is 'fedora-*' 'rocky-*' 'rhel-*' 'centos-*' 'almalinux-*'; then
            prepare_staging && build_rpm_native || FAILED+=("native-rpm")
        else
            echo "[i] --native-rpm: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM_ORACLE" == true ]]; then
        if distro_is 'ol-*' 'oracle-*'; then
            prepare_staging && build_rpm_oracle || FAILED+=("native-rpm-oracle")
        else
            echo "[i] --native-rpm-oracle: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM_REDOS" == true ]]; then
        if distro_is 'redos-*'; then
            prepare_staging && build_rpm_redos || FAILED+=("native-rpm-redos")
        else
            echo "[i] --native-rpm-redos: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM_SUSE" == true ]]; then
        if distro_is 'opensuse*' 'sles*'; then
            prepare_staging && build_rpm_suse || FAILED+=("native-rpm-suse")
        else
            echo "[i] --native-rpm-suse: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_ALT" == true ]]; then
        if distro_is 'alt-*' 'alt' 'altlinux*'; then
            prepare_staging && build_rpm_alt || FAILED+=("native-alt")
        else
            echo "[i] --native-alt: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_ROSA" == true ]]; then
        if distro_is 'rosa-*'; then
            prepare_staging && build_rpm_rosa || FAILED+=("native-rosa")
        else
            echo "[i] --native-rosa: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM_MAGEIA" == true ]]; then
        if distro_is 'mageia-*'; then
            prepare_staging && build_rpm_mageia || FAILED+=("native-rpm-mageia")
        else
            echo "[i] --native-rpm-mageia: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM_OPENMANDRIVA" == true ]]; then
        if distro_is 'openmandriva-*'; then
            prepare_staging && build_rpm_openmandriva || FAILED+=("native-rpm-openmandriva")
        else
            echo "[i] --native-rpm-openmandriva: пропускаем на $DISTRO"
        fi
    fi
    if [[ "$BUILD_NATIVE_ARCH" == true ]]; then
        if distro_is 'arch' 'arch-*' 'manjaro*' 'endeavouros*' 'cachyos*'; then
            prepare_staging && build_pkg_arch || FAILED+=("native-arch")
        else
            echo "[i] --native-arch: пропускаем на $DISTRO"
        fi
    fi

    if [[ "$BUILD_NATIVE_APK" == true ]]; then
        if distro_is 'alpine*'; then
            prepare_staging && build_apk_alpine || FAILED+=("native-apk")
        else
            echo "[i] --native-apk: пропускаем на $DISTRO"
        fi
    fi

    if [[ "$BUILD_NATIVE_SLACKBUILD" == true ]]; then
        if distro_is 'slackware*'; then
            prepare_staging && build_native_slackbuild || FAILED+=("native-slackbuild")
        else
            echo "[i] --native-slackbuild: пропускаем на $DISTRO"
        fi
    fi

    if [[ "$BUILD_NATIVE_XBPS" == true ]]; then
        if distro_is 'void*'; then
            build_native_xbps_void || FAILED+=("native-xbps")
        else
            echo "[i] --native-xbps: пропускаем на $DISTRO"
        fi
    fi

    if [[ "$BUILD_NATIVE_GENTOO" == true ]]; then
        if distro_is 'gentoo*'; then
            build_native_gentoo || FAILED+=("native-gentoo")
        else
            echo "[i] --native-gentoo: пропускаем на $DISTRO"
        fi
    fi

    if [[ "$BUILD_NATIVE_NIX" == true ]]; then
        if command -v nix >/dev/null 2>&1; then
            build_native_nix || FAILED+=("native-nix")
        else
            echo "[i] --native-nix: nix не найден в PATH — пропускаем."
        fi
    fi

    [[ "$BUILD_APPIMAGE" == true ]] && { build_appimage || FAILED+=("appimage"); }
    [[ "$BUILD_SHARUN"   == true ]] && { build_sharun_appimage || FAILED+=("sharun"); }
    [[ "$BUILD_FLATPAK" == true ]] && { build_flatpak || FAILED+=("flatpak"); }

    sign_files

    echo ""
    if (( ${#FAILED[@]} > 0 )); then
        echo "[!] Не собраны: ${FAILED[*]}" >&2
        ls -la "$OUTPUT_DIR/"; exit 1
    fi
    echo "🎉 Готово! Артефакты в '$OUTPUT_DIR':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"