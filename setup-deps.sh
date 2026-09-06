#!/bin/bash
# =============================================================================
# setup-deps.sh – установка зависимостей iptvplayer
# =============================================================================
#
# Назначение:
#   - Проверяет и устанавливает системные зависимости (если не отключено)
#   - Скачивает и собирает статические библиотеки wxWidgets и wxSQLite3
#     в папку third_party/ проекта.
#   - Поддерживает интерактивный и неинтерактивный (CI) режимы.
#
# Использование:
#   ./setup-deps.sh [--yes] [--skip-system]
#
# Опции:
#   --yes            Автоматически соглашаться на все запросы (неинтерактивный).
#   --skip-system    Не устанавливать системные пакеты (только сборка third_party).
#
# Переменные окружения:
#   SETUP_DEPS_SKIP_SYSTEM=1   эквивалентно --skip-system
#
# Примеры:
#   ./setup-deps.sh                     # интерактивный режим
#   ./setup-deps.sh --yes               # неинтерактивный (для CI)
#   ./setup-deps.sh --skip-system       # только сборка third_party
# =============================================================================

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1" >&2; }
error() { echo -e "${RED}[ERROR]${NC} $1" >&2; exit 1; }
section() { echo -e "\n${BLUE}═══════════════════════════════════════${NC}\n${BLUE}$1${NC}\n${BLUE}═══════════════════════════════════════${NC}\n"; }

# ---- Определение корня проекта ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    error "Скрипт должен быть запущен из корня проекта (там, где CMakeLists.txt)."
fi

log "Корень проекта: $PROJECT_ROOT"

# ---- Обработка аргументов ----
NON_INTERACTIVE=false
SKIP_SYSTEM=false

for arg in "$@"; do
    case "$arg" in
        --yes|-y) NON_INTERACTIVE=true ;;
        --skip-system) SKIP_SYSTEM=true ;;
        *) error "Неизвестный аргумент: $arg" ;;
    esac
done

# Если запущено в CI, автоматически включаем неинтерактивный режим
if [[ -n "${CI:-}" ]] || [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    NON_INTERACTIVE=true
    log "Обнаружена среда CI, включён неинтерактивный режим."
fi

# Переменная окружения также управляет SKIP_SYSTEM
if [[ "${SETUP_DEPS_SKIP_SYSTEM:-0}" == "1" ]]; then
    SKIP_SYSTEM=true
    log "Переменная SETUP_DEPS_SKIP_SYSTEM=1, установка системных пакетов пропущена."
fi

# =============================================================================
# ДЕТЕКТИРОВАНИЕ ОС И ПАКЕТНОГО МЕНЕДЖЕРА (если не пропущена установка)
# =============================================================================

if [[ "$SKIP_SYSTEM" == false ]]; then
    section "ДЕТЕКТИРОВАНИЕ ОС И ПАКЕТНОГО МЕНЕДЖЕРА"

    OS_ID=""
    OS_VERSION=""
    PKG_MANAGER=""
    INSTALL_CMD=""

    if [[ -f /etc/os-release ]]; then
        source /etc/os-release
        OS_ID="${ID:-unknown}"
        OS_VERSION="${VERSION_ID:-unknown}"
    else
        error "Не найден /etc/os-release. Не удалось определить ОС."
    fi

    log "Обнаружена ОС: $OS_ID $OS_VERSION"

    case "$OS_ID" in
        debian|ubuntu)
            PKG_MANAGER="apt"
            INSTALL_CMD="sudo apt-get install -y"
            ;;
        fedora|rhel|centos|rocky)
            PKG_MANAGER="dnf"
            if ! command -v dnf &>/dev/null; then
                PKG_MANAGER="yum"
                INSTALL_CMD="sudo yum install -y"
            else
                INSTALL_CMD="sudo dnf install -y"
            fi
            ;;
        arch|manjaro)
            PKG_MANAGER="pacman"
            INSTALL_CMD="sudo pacman -S --noconfirm"
            ;;
        *)
            error "Неподдерживаемая ОС: $OS_ID"
            ;;
    esac

    log "Пакетный менеджер: $PKG_MANAGER"

    # =========================================================================
    # ОПРЕДЕЛЕНИЕ ПАКЕТОВ ДЛЯ УСТАНОВКИ
    # =========================================================================

    section "ПОДГОТОВКА СПИСКА ЗАВИСИМОСТЕЙ"

    declare -A PACKAGES

    case "$PKG_MANAGER" in
        apt)
            PACKAGES=(
                [build-essential]="build-essential"
                [cmake]="cmake"
                [git]="git"
                [autoconf]="autoconf"
                [automake]="automake"
                [libtool]="libtool"
                [pkg-config]="pkg-config"
                [wget]="wget"
                [curl]="curl"
                [gtk3-dev]="libgtk-3-dev"
                [x11-dev]="libx11-dev"
                [x11-xcb-dev]="libx11-xcb-dev"
                [gl-dev]="libgl1-mesa-dev"
                [egl-dev]="libegl1-mesa-dev"
                [png-dev]="libpng-dev"
                [jpeg-dev]="libjpeg-dev"
                [webp-dev]="libwebp-dev"
                [zlib-dev]="zlib1g-dev"
                [expat-dev]="libexpat1-dev"
                [freetype-dev]="libfreetype-dev"
            )
            ;;
        dnf)
            PACKAGES=(
                [build-essential]="gcc gcc-c++ make"
                [cmake]="cmake"
                [git]="git"
                [autoconf]="autoconf"
                [automake]="automake"
                [libtool]="libtool"
                [pkg-config]="pkg-config"
                [wget]="wget"
                [curl]="curl"
                [gtk3-dev]="gtk3-devel"
                [x11-dev]="libX11-devel"
                [x11-xcb-dev]="libxcb-devel"
                [gl-dev]="mesa-libGL-devel"
                [egl-dev]="mesa-libEGL-devel"
                [png-dev]="libpng-devel"
                [jpeg-dev]="libjpeg-turbo-devel"
                [webp-dev]="libwebp-devel"
                [zlib-dev]="zlib-devel"
                [expat-dev]="expat-devel"
                [freetype-dev]="freetype-devel"
            )
            ;;
        pacman)
            PACKAGES=(
                [build-essential]="base-devel"
                [cmake]="cmake"
                [git]="git"
                [autoconf]="autoconf"
                [automake]="automake"
                [libtool]="libtool"
                [pkg-config]="pkg-config"
                [wget]="wget"
                [curl]="curl"
                [gtk3-dev]="gtk3"
                [x11-dev]="libx11"
                [x11-xcb-dev]="libxcb"
                [gl-dev]="mesa"
                [png-dev]="libpng"
                [jpeg-dev]="libjpeg-turbo"
                [webp-dev]="libwebp"
                [zlib-dev]="zlib"
                [expat-dev]="expat"
                [freetype-dev]="freetype2"
            )
            ;;
    esac

    # =========================================================================
    # ПРОВЕРКА УСТАНОВЛЕННЫХ ПАКЕТОВ
    # =========================================================================

    section "ПРОВЕРКА УСТАНОВЛЕННЫХ ПАКЕТОВ"

    MISSING_PACKAGES=()

    for pkg_key in "${!PACKAGES[@]}"; do
        pkg_names="${PACKAGES[$pkg_key]}"
        FOUND=false
        for pkg in $pkg_names; do
            case "$PKG_MANAGER" in
                apt)
                    if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
                        FOUND=true
                        break
                    fi
                    ;;
                dnf)
                    if rpm -q "$pkg" &>/dev/null; then
                        FOUND=true
                        break
                    fi
                    ;;
                pacman)
                    if pacman -Q "$pkg" &>/dev/null; then
                        FOUND=true
                        break
                    fi
                    ;;
            esac
        done
        if [[ "$FOUND" == true ]]; then
            log "✓ $pkg_key: установлен"
        else
            warn "✗ $pkg_key: НЕ установлен ($pkg_names)"
            MISSING_PACKAGES+=("$pkg_names")
        fi
    done

    # =========================================================================
    # УСТАНОВКА НЕДОСТАЮЩИХ ПАКЕТОВ
    # =========================================================================

    if [[ ${#MISSING_PACKAGES[@]} -gt 0 ]]; then
        section "УСТАНОВКА НЕДОСТАЮЩИХ ПАКЕТОВ"
        echo "Требуется установить следующие пакеты:"
        for pkg in "${MISSING_PACKAGES[@]}"; do
            echo "  - $pkg"
        done
        echo

        if [[ "$NON_INTERACTIVE" == true ]]; then
            log "Неинтерактивный режим: установка будет выполнена автоматически."
            RESPONSE="y"
        else
            read -p "Продолжить установку? [y/N] " -n 1 -r
            echo
            RESPONSE="$REPLY"
        fi

        if [[ ! $RESPONSE =~ ^[Yy]$ ]]; then
            error "Установка прервана пользователем. Установите зависимости вручную."
        fi

        log "Обновление пакетного менеджера..."
        case "$PKG_MANAGER" in
            apt) sudo apt-get update -qq ;;
            dnf) sudo dnf makecache -q ;;
            pacman) sudo pacman -Sy --noconfirm > /dev/null ;;
        esac

        log "Установка пакетов..."
        for pkg in "${MISSING_PACKAGES[@]}"; do
            eval "$INSTALL_CMD $pkg" || warn "Ошибка при установке: $pkg (может быть необязательным)"
        done
        log "✅ Пакеты установлены"
    else
        log "✅ Все обязательные пакеты уже установлены"
    fi
else
    log "Пропускаем установку системных пакетов (--skip-system или переменная)."
fi

# =============================================================================
# СБОРКА ЗАВИСИМОСТЕЙ ПРОЕКТА (wxWidgets, wxSQLite3) – выполняется всегда
# =============================================================================

section "СБОРКА WXWIDGETS И WXSQLITE3"

# Проверяем наличие критических инструментов после установки пакетов
command -v cmake >/dev/null || error "cmake не установлен (требуется >= 3.16)"
command -v git >/dev/null || warn "git не найден – версионирование будет отключено"
command -v autoreconf >/dev/null || error "autoreconf не найден (установите autoconf, automake, libtool)"

if ! command -v wget &> /dev/null && ! command -v curl &> /dev/null; then
    error "Установите wget или curl."
fi

DOWNLOADER="wget"
if command -v curl &> /dev/null; then
    DOWNLOADER="curl"
fi
log "Используется загрузчик: $DOWNLOADER"

THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
WX_DIR="$THIRD_PARTY_DIR/wx"
WXSQLITE3_DIR="$THIRD_PARTY_DIR/wxsqlite3"

WX_VERSION="3.3.2"
WXSQLITE3_VERSION="5.0.1"

mkdir -p "$THIRD_PARTY_DIR"
cd "$PROJECT_ROOT"

# -------------------- wxWidgets --------------------
section "СБОРКА WXWIDGETS $WX_VERSION"

if [[ -d "$WX_DIR" ]]; then
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: пропускаем сборку wxWidgets (используем существующую)."
        SKIP_WX=true
    else
        warn "Директория wx уже существует. Пропускаем сборку wxWidgets? [y/N]"
        read -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            SKIP_WX=true
        else
            SKIP_WX=false
            rm -rf "$WX_DIR"
        fi
    fi
else
    SKIP_WX=false
fi

if [[ "$SKIP_WX" != true ]]; then
    log "Сборка wxWidgets $WX_VERSION..."
    mkdir -p "$WX_DIR"
    cd "$WX_DIR"

    WX_ARCHIVE="wxWidgets-$WX_VERSION.tar.bz2"
    WX_URL="https://github.com/wxWidgets/wxWidgets/releases/download/v$WX_VERSION/$WX_ARCHIVE"

    if [[ "$DOWNLOADER" == "wget" ]]; then
        wget -q --show-progress "$WX_URL" -O "$WX_ARCHIVE" || error "Скачивание wxWidgets не удалось"
    else
        curl -L -o "$WX_ARCHIVE" "$WX_URL" --progress-bar || error "Скачивание wxWidgets не удалось"
    fi

    tar xf "$WX_ARCHIVE" && rm "$WX_ARCHIVE"
    mv "wxWidgets-$WX_VERSION" src
    mkdir -p build install
    cd build

    export CXXFLAGS="-std=c++20"
     cmake ../src \
        -DCMAKE_BUILD_TYPE=Release \
        -DwxBUILD_SHARED=OFF \
        -DwxUSE_LIBWEBP=builtin \
        -DwxUSE_SVG=ON \
        -DwxUSE_MEDIACTRL:BOOL=OFF \
        -DwxUSE_GSTREAMER:BOOL=OFF \
        -DwxUSE_WEBKIT:BOOL=OFF \
        -DwxUSE_WEBVIEW_WEBKIT:BOOL=OFF \
        -DwxUSE_LIBTIFF:BOOL=OFF \
        -DwxUSE_GIF:BOOL=OFF \
        -DwxUSE_ACCESSIBILITY:BOOL=OFF \
        -DwxUSE_HELP:BOOL=OFF \
        -DwxUSE_HTML:BOOL=OFF \
        -DwxUSE_STC:BOOL=OFF \
        -DwxUSE_MS_HTML_HELP:BOOL=OFF \
        -DwxUSE_WXHTML_HELP:BOOL=OFF \
        -DwxUSE_XRC:BOOL=OFF \
        -DwxUSE_XML:BOOL=OFF \
        -DwxUSE_NET:BOOL=OFF \
        -DwxUSE_RICHTEXT:BOOL=OFF \
        -DwxUSE_LIBSDL:BOOL=OFF \
        -DwxUSE_PRINTING_ARCHITECTURE=OFF \
        -DCMAKE_INSTALL_PREFIX=../install

    cmake --build . --target install -j$(nproc)
    log "wxWidgets установлен в $WX_DIR/install"
    cd "$PROJECT_ROOT"
fi

# Проверка, что wx-config существует
if [[ ! -f "$WX_DIR/install/bin/wx-config" ]]; then
    error "wx-config не найден в $WX_DIR/install/bin. Сборка wxWidgets не удалась."
fi

# -------------------- wxSQLite3 --------------------
section "СБОРКА WXSQLITE3 $WXSQLITE3_VERSION"

if [[ -d "$WXSQLITE3_DIR" ]]; then
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: пропускаем сборку wxSQLite3 (используем существующую)."
        SKIP_WXSQLITE3=true
    else
        warn "Директория wxsqlite3 уже существует. Пропускаем сборку wxSQLite3? [y/N]"
        read -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            SKIP_WXSQLITE3=true
        else
            SKIP_WXSQLITE3=false
            rm -rf "$WXSQLITE3_DIR"
        fi
    fi
else
    SKIP_WXSQLITE3=false
fi

if [[ "$SKIP_WXSQLITE3" != true ]]; then
    log "Сборка wxSQLite3 $WXSQLITE3_VERSION..."
    mkdir -p "$WXSQLITE3_DIR"
    cd "$WXSQLITE3_DIR"

    WXSQLITE3_ARCHIVE="v$WXSQLITE3_VERSION.tar.gz"
    WXSQLITE3_URL="https://github.com/utelle/wxsqlite3/archive/refs/tags/$WXSQLITE3_ARCHIVE"

    if [[ "$DOWNLOADER" == "wget" ]]; then
        wget -q --show-progress --header="User-Agent: Mozilla/5.0" "$WXSQLITE3_URL" -O "$WXSQLITE3_ARCHIVE" || error "Скачивание wxSQLite3 не удалось"
    else
        curl -L -f -H "User-Agent: Mozilla/5.0" -o "$WXSQLITE3_ARCHIVE" "$WXSQLITE3_URL" --progress-bar || error "Скачивание wxSQLite3 не удалось"
    fi

    tar xf "$WXSQLITE3_ARCHIVE" && rm "$WXSQLITE3_ARCHIVE"

    if [[ -d "wxsqlite3-$WXSQLITE3_VERSION" ]]; then
        mv "wxsqlite3-$WXSQLITE3_VERSION" src
    elif [[ -d "wxSQLite3-$WXSQLITE3_VERSION" ]]; then
        mv "wxSQLite3-$WXSQLITE3_VERSION" src
    else
        error "Не найдена распакованная папка. Содержимое: $(ls -la)"
    fi

    cd src
    autoreconf -fi

    WX_ROOT_ABS="$WX_DIR/install"
    log "Путь к wxWidgets: $WX_ROOT_ABS"

    mkdir -p ../build
    cd ../build

    export CXXFLAGS="-std=c++20"
    ../src/configure \
        --prefix="$WXSQLITE3_DIR/install" \
        --with-wx-config="$WX_ROOT_ABS/bin/wx-config" \
        --disable-shared \
        --enable-static \
        --without-sqlcipher \
        --without-aes128cbc \
        --without-ascon128 \
        --without-aegis

    MAKEFILE="Makefile"
    if [[ ! -f "$MAKEFILE" ]]; then
        error "Makefile не найден в $(pwd)"
    fi

    LIB_TARGET=$(grep -E '^[[:space:]]*lib.*_LTLIBRARIES' "$MAKEFILE" 2>/dev/null | sed 's/.*=//' | awk '{print $1}' | head -1 | xargs || true)
    if [[ -z "$LIB_TARGET" ]]; then
        LIB_TARGET=$(find . -maxdepth 1 -type f -name "*.la" -printf "%f\n" | head -1 || true)
    fi
    if [[ -z "$LIB_TARGET" ]]; then
        error "Не удалось определить цель библиотеки из Makefile"
    fi
    log "Цель библиотеки: $LIB_TARGET"

    make -j$(nproc) "$LIB_TARGET"
    make install-exec

    if [[ ! -d "$WXSQLITE3_DIR/install/include/wx" ]]; then
        log "Копирование заголовков вручную..."
        mkdir -p "$WXSQLITE3_DIR/install/include/wx"
        cp -r "$WXSQLITE3_DIR/src/include/wx/"* "$WXSQLITE3_DIR/install/include/wx/"
    fi

    log "wxSQLite3 установлен в $WXSQLITE3_DIR/install"
    cd "$PROJECT_ROOT"
fi

# =============================================================================
# ЗАВЕРШЕНИЕ
# =============================================================================

section "ГОТОВО"

log "✅ Все зависимости установлены!"
echo "Теперь можно собирать проект:"
echo "  ./build-release.sh"
echo "  или (ручная сборка):"
echo "  mkdir -p build && cd build"
echo "  cmake .. && make -j$(nproc)"
echo