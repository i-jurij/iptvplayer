#!/bin/bash
# =============================================================================
# common.sh – общие утилиты для скриптов сборки iptvplayer
# =============================================================================
#
# Библиотека, не запускается напрямую.
# Сорсится из build-package.sh (а в будущем — из build-release.sh и setup-deps.sh).
#
# Содержит:
#   - цвета и логирование (log/warn/error/section)
#   - автодетект NON_INTERACTIVE
#   - ask() — интерактивный запрос y/n
#   - detect_arch, detect_distro, detect_pkgmgr — окружение
#   - read_versions_from_install — читает install/VERSION*
#   - prepare_staging — заполняет STAGING_DIR для нативных пакетов
#   - detect_deb_depends — вычисляет Depends для .deb
#   - check_deps — проверяет наличие утилит для запрошенных сборок
#
# Требует: SCRIPT_DIR, PROJECT_ROOT (могут быть установлены снаружи;
#          если нет — вычисляются здесь).
# =============================================================================

: "${SCRIPT_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
: "${PROJECT_ROOT:=$(cd "$SCRIPT_DIR/.." && pwd)}"

# === Цвета ===
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# === Логирование ===
log()     { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $1" >&2; }
error()   { echo -e "${RED}[ERROR]${NC} $1" >&2; exit 1; }
section() { echo -e "\n${BLUE}═══════════════════════════════════════${NC}\n${BLUE}$1${NC}\n${BLUE}═══════════════════════════════════════${NC}\n"; }

# === Неинтерактивный режим ===
NON_INTERACTIVE=false
if [[ -n "${CI:-}" ]] || [[ -n "${GITHUB_ACTIONS:-}" ]] || [[ ! -t 0 ]]; then
    NON_INTERACTIVE=true
fi

# === Спросить пользователя: 0 для "да", 1 для "нет" ===
# $1 — вопрос
# $2 — default в интерактивном режиме ("y"/"n")
# $3 — default в неинтерактивном режиме (по умолчанию совпадает с $2)
ask() {
    local prompt="$1"
    local di="${2:-n}"
    local dni="${3:-$di}"
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: '${prompt}' → ${dni} (авто)"
        [[ "$dni" == "y" ]]
        return
    fi
    local reply=""
    read -p "$prompt " -n 1 -r reply || true
    echo
    [[ -z "$reply" ]] && reply="$di"
    [[ "$reply" =~ ^[Yy]$ ]]
}

# === Загрузчик ===
if command -v wget &>/dev/null; then
    DOWNLOADER="wget"
elif command -v curl &>/dev/null; then
    DOWNLOADER="curl"
else
    DOWNLOADER=""
fi

# Скачивание с повторами и таймаутом.
# $1 — URL, $2 — путь назначения, $3 — необязательный User-Agent.
# Скачивание с повторами и таймаутом.
# $1 — URL, $2 — путь назначения, $3 — необязательный User-Agent.
#
# В TTY: прогресс-бар на stderr, ошибки загрузчика видны вживую.
# В CI/пайпе: тихий режим, весь вывод загрузчика пишется в лог
# и показывается при окончательном провале.
download() {
    local url="$1"
    local out="$2"
    local ua="${3:-}"
    local tries=5
    local i
    local log
    log="$(mktemp -t download.XXXXXX)"

    if [[ -z "$DOWNLOADER" ]]; then
        error "Нужен wget или curl для скачивания $url"
    fi

    # Прогресс — только когда stderr это терминал.
    local -a progress=()
    if [[ -t 2 ]]; then
        if [[ "$DOWNLOADER" == "wget" ]]; then
            progress=(--show-progress)
        else
            progress=(--progress-bar)
        fi
    fi

    for ((i = 1; i <= tries; i++)); do
        : > "$log"
        if [[ "$DOWNLOADER" == "wget" ]]; then
            local -a args=(--tries=1 --timeout=30 --read-timeout=60 "${progress[@]}")
            [[ -n "$ua" ]] && args+=(--header="User-Agent: $ua")
            # stderr → tee: и в лог, и на экран. В TTY видно прогресс, в CI — тишина.
            if wget "${args[@]}" "$url" -O "$out" 2> >(tee "$log" >&2); then
                rm -f "$log"
                return 0
            fi
        else
            local -a args=(-L -f --connect-timeout 30 --retry 0 "${progress[@]}")
            [[ -n "$ua" ]] && args+=(-H "User-Agent: $ua")
            if curl "${args[@]}" -o "$out" "$url" 2> >(tee "$log" >&2); then
                rm -f "$log"
                return 0
            fi
        fi
        warn "Скачивание не удалось (попытка $i/$tries): $url"
        if ((i == tries)) && [[ -s "$log" ]]; then
            echo "--- Вывод загрузчика ---" >&2
            cat "$log" >&2
            echo "------------------------" >&2
        fi
        ((i < tries)) && sleep 5
    done
    rm -f "$log"
    return 1
}

# === Определение архитектуры ===
# Устанавливает глобальные: DEB_ARCH, RPM_ARCH, APPIMAGE_ARCH
detect_arch() {
    local machine
    machine="$(uname -m)"
    case "$machine" in
        x86_64|amd64) DEB_ARCH="amd64";  RPM_ARCH="x86_64";  APPIMAGE_ARCH="x86_64" ;;
        aarch64|arm64) DEB_ARCH="arm64"; RPM_ARCH="aarch64"; APPIMAGE_ARCH="aarch64" ;;
        armv7l|armhf)  DEB_ARCH="armhf"; RPM_ARCH="armv7hl"; APPIMAGE_ARCH="armhf" ;;
        i686|i386)     DEB_ARCH="i386";  RPM_ARCH="i686";    APPIMAGE_ARCH="i686" ;;
        *) echo "[!] Неизвестная архитектура: $machine" >&2; exit 1 ;;
    esac
    echo "[i] Архитектура: $machine → deb=$DEB_ARCH, rpm=$RPM_ARCH, appimage=$APPIMAGE_ARCH"
}

# === Определение дистрибутива ===
# Устанавливает глобальную: DISTRO
detect_distro() {
    if [ -n "${DISTRO:-}" ]; then
        echo "[i] DISTRO задан извне: $DISTRO"
        return 0
    fi
    if [ -r /etc/os-release ]; then
        . /etc/os-release
        local id="${ID:-unknown}"
        local ver="${VERSION_ID:-}"
        if [ -n "$ver" ]; then DISTRO="${id}-${ver}"; else DISTRO="$id"; fi
    else
        DISTRO="unknown"
    fi
    echo "[i] DISTRO определён локально: $DISTRO"
}

# Пакетный менеджер текущей системы (возвращает через stdout)
detect_pkgmgr() {
    if [ -n "${DISTRO:-}" ]; then
        case "$DISTRO" in
            ubuntu-*|debian-*|linuxmint-*|pop-*) echo "deb" ;;
            fedora-*|rocky-*|rhel-*|centos-*|almalinux-*|opensuse*|sles*) echo "rpm" ;;
            arch|arch-*|manjaro*|endeavouros*) echo "arch" ;;
            *) echo "unknown" ;;
        esac
    else
        echo "unknown"
    fi
}

# === Чтение версий из install/ ===
# Возвращает через stdout три строки: VERSION_FULL, VERSION_FILE, VERSION
read_versions_from_install() {
    local INSTALL_DIR="$PROJECT_ROOT/install"
    local VERSION_FILE_PATH="$INSTALL_DIR/VERSION"
    local VERSION_FULL_PATH="$INSTALL_DIR/VERSION_FULL"
    local VERSION_FILE_NAME_PATH="$INSTALL_DIR/VERSION_FILE"

    if [ ! -f "$VERSION_FILE_PATH" ] || [ ! -f "$VERSION_FULL_PATH" ] || [ ! -f "$VERSION_FILE_NAME_PATH" ]; then
        echo "[ERROR] Файлы версий не найдены в $INSTALL_DIR." >&2
        return 1
    fi
    local VERSION VERSION_FULL VERSION_FILE
    VERSION=$(tr -d '\n\r' < "$VERSION_FILE_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VERSION_FULL=$(tr -d '\n\r' < "$VERSION_FULL_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VERSION_FILE=$(tr -d '\n\r' < "$VERSION_FILE_NAME_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    if [ -z "$VERSION" ] || [ -z "$VERSION_FULL" ] || [ -z "$VERSION_FILE" ]; then
        return 1
    fi
    printf '%s\n%s\n%s\n' "$VERSION_FULL" "$VERSION_FILE" "$VERSION"
}

# === Генерация .desktop-файла ===
# $1 — путь к целевому .desktop
# Требует: PACKAGE_NAME, ICON_NAME
write_desktop_file() {
    local target="$1"
    mkdir -p "$(dirname "$target")"
    cat > "$target" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME %F
Icon=${ICON_NAME%.svg}
Type=Application
Categories=AudioVideo;
Comment=IPTV Playlist Player
Terminal=false
StartupNotify=true
StartupWMClass=$PACKAGE_NAME
MimeType=video/mp4;video/x-matroska;video/avi;video/mpeg;video/quicktime;video/x-msvideo;video/x-flv;video/ogg;video/webm;application/x-mpegURL;audio/x-mpegurl;audio/x-scpls;application/xspf+xml;application/vnd.apple.mpegurl;
EOF
}

# === Подготовка STAGING_DIR для нативных пакетов ===
# Требует: PROJECT_ROOT, STAGING_DIR, PACKAGE_NAME, ICON_NAME, METAINFO_NAME
prepare_staging() {
    local BIN_PATH="$PROJECT_ROOT/install/bin/$PACKAGE_NAME"
    if [ ! -d "$PROJECT_ROOT/install/bin" ] || [ ! -f "$BIN_PATH" ]; then
        echo "[!] Бинарник не найден: $BIN_PATH" >&2
        return 1
    fi

    mkdir -p "$STAGING_DIR/usr/bin"
    cp "$BIN_PATH" "$STAGING_DIR/usr/bin/"

    mkdir -p "$STAGING_DIR/usr/share/$PACKAGE_NAME"
    cp -r "$PROJECT_ROOT/install/share/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/$PACKAGE_NAME/" 2>/dev/null || true

    write_desktop_file "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop"

    mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
    cp "$PROJECT_ROOT/install/share/$PACKAGE_NAME/icons/$ICON_NAME" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" 2>/dev/null || true

    if [ -f "$PROJECT_ROOT/install/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/metainfo"
        cp "$PROJECT_ROOT/install/share/metainfo/$METAINFO_NAME" "$STAGING_DIR/usr/share/metainfo/"
    fi

    if [ -d "$PROJECT_ROOT/install/share/doc/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME"
        cp -r "$PROJECT_ROOT/install/share/doc/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME/" 2>/dev/null || true
    fi
    if [ -d "$PROJECT_ROOT/install/share/licenses/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME"
        cp -r "$PROJECT_ROOT/install/share/licenses/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME/" 2>/dev/null || true
    fi
    return 0
}

# === Проверка зависимостей ===
# $1 — pkgmgr (deb/rpm/arch/unknown)
# $2..$7 — need_native_deb, need_native_rpm, need_native_arch,
#          need_bundle_deb, need_bundle_rpm, need_appimage
check_deps() {
    local pkgmgr="$1"
    shift
    local need_native_deb=$1
    local need_native_rpm=$2
    local need_native_arch=$3
    local need_bundle_deb=$4
    local need_bundle_rpm=$5
    local need_appimage=$6
    local need_sharun=$7

    local required=()
    local optional=()

    for tool in tar readelf; do
        command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
    done

    if [ -n "${GPG_KEY_ID:-}" ]; then
        command -v gpg >/dev/null 2>&1 || required+=("gpg")
    fi

    if { [[ "$need_native_deb" == true && "$pkgmgr" == deb ]] || [[ "$need_bundle_deb" == true ]]; }; then
        command -v dpkg-deb >/dev/null 2>&1 || required+=("dpkg-deb")
    fi
    if [[ "$need_native_deb" == true && "$pkgmgr" == deb ]]; then
        command -v dpkg-shlibdeps >/dev/null 2>&1 || required+=("dpkg-shlibdeps")
    fi
    if { [[ "$need_native_rpm" == true && "$pkgmgr" == rpm ]] || [[ "$need_bundle_rpm" == true ]]; }; then
        for tool in rpmbuild rpm; do
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
        done
    fi
    if [[ "$need_native_arch" == true && "$pkgmgr" == arch ]]; then
        command -v makepkg >/dev/null 2>&1 || required+=("makepkg")
    fi
    if [[ "$need_appimage" == true || "$need_bundle_deb" == true || \
          "$need_bundle_rpm" == true || "$need_sharun" == true ]]; then
        command -v wget >/dev/null 2>&1 || required+=("wget")
    fi
        if [[ "$need_sharun" == true ]]; then
        command -v patchelf >/dev/null 2>&1 || required+=("patchelf")
    fi

    if { [[ "$need_native_deb" == true && "$pkgmgr" == deb ]] || [[ "$need_bundle_deb" == true ]]; } && ! command -v debsigs >/dev/null 2>&1; then
        optional+=("debsigs")
    fi
    if [[ "$need_appimage" == true ]] && ! command -v zsyncmake >/dev/null 2>&1; then
        optional+=("zsyncmake")
    fi

    if [ ${#required[@]} -ne 0 ]; then
        echo "[!] Не хватает обязательных инструментов: ${required[*]}"
        exit 1
    fi
    if [ ${#optional[@]} -ne 0 ]; then
        echo "[i] Опциональные инструменты не найдены: ${optional[*]}"
        for tool in "${optional[@]}"; do
            case "$tool" in
                debsigs)   echo "    → .deb не будет подписан (sudo apt install debsigs)" ;;
                zsyncmake) echo "    → .zsync не будет сгенерирован (sudo apt install zsync)" ;;
            esac
        done
    fi
}

# === Вычисление Depends для .deb ===
# $1 — путь к бинарнику
# $2 — (опц.) путь к бандленным .so
# $3 — (опц.) путь к fallback-библиотекам
# Возвращает через stdout строку Depends (может быть пустой)
detect_deb_depends() {
    local bin_path="$1"
    local bundle_lib="${2:-}"
    local bundle_fb="${3:-}"

    # Кэш имеет смысл только для native-варианта (без -l):
    # в CI install/DEB_DEPENDS готовится шагом
    # "Compute .deb dependencies" и приезжает в артефакте install.
    # Для bundled кэш не используется — там свои -l пути,
    # dpkg-shlibdeps должен посчитать заново, видя бандл.
    if [ -z "$bundle_lib" ] && [ -z "$bundle_fb" ]; then
        local cached="$PROJECT_ROOT/install/DEB_DEPENDS"
        if [ -f "$cached" ]; then
            local d; d=$(tr -d '\n\r' < "$cached")
            [ -n "$d" ] && { echo "$d"; return 0; }
        fi
    fi

    local tmp_dir; tmp_dir=$(mktemp -d)
    mkdir -p "$tmp_dir/debian"
    cat > "$tmp_dir/debian/control" << EOF
Source: $PACKAGE_NAME
Package: $PACKAGE_NAME
Architecture: any
EOF
    local args=(-O)
    [ -n "$bundle_lib" ] && args+=(-l"$bundle_lib")
    [ -n "$bundle_fb" ]  && args+=(-l"$bundle_fb")
    local output
    output=$(cd "$tmp_dir" && dpkg-shlibdeps "${args[@]}" "$bin_path" 2>&1) || true
    rm -rf "$tmp_dir"
    echo "$output" | sed -n 's/^shlibs:Depends=//p' | tr -d '\n\r'
}