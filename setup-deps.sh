#!/bin/bash

# setup-deps.sh - Автоматическая установка зависимостей для iptvplayer
# Использование: ./setup-deps.sh (запускать из корня проекта)

set -euo pipefail

# Цвета
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
    exit 1
}

# ----------------------------------------------------------------------
# Определяем корень проекта
# ----------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    error "Скрипт должен быть запущен из корня проекта (там, где CMakeLists.txt)."
fi

log "Корень проекта: $PROJECT_ROOT"

# Проверки утилит
if ! command -v cmake &> /dev/null; then
    error "cmake не установлен. Установите CMake (>= 3.20)."
fi

if ! command -v wget &> /dev/null && ! command -v curl &> /dev/null; then
    error "Установите wget или curl."
fi

DOWNLOADER="wget"
if command -v curl &> /dev/null; then
    DOWNLOADER="curl"
fi
log "Используется: $DOWNLOADER"

# Директории
THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
WX_DIR="$THIRD_PARTY_DIR/wx"
WXSQLITE3_DIR="$THIRD_PARTY_DIR/wxsqlite3"

# Версии
WX_VERSION="3.3.2"
WXSQLITE3_VERSION="5.0.1"

# ----------------------------------------------------------------------
# Создаём third_party
# ----------------------------------------------------------------------
mkdir -p "$THIRD_PARTY_DIR"
cd "$PROJECT_ROOT"

# ----------------------------------------------------------------------
# Сборка wxWidgets (статически)
# ----------------------------------------------------------------------
if [[ -d "$WX_DIR" ]]; then
    warn "Директория wx уже существует. Пропускаем сборку wxWidgets? [y/N]"
    read -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log "Пропущено: wxWidgets"
    else
        rm -rf "$WX_DIR"
    fi
fi

if [[ ! -d "$WX_DIR" ]]; then
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
        -DwxUSE_WEBP=ON \
        -DwxUSE_LIBWEBP=builtin \
        -DCMAKE_INSTALL_PREFIX=../install

    cmake --build . --target install -j$(nproc)
    log "wxWidgets установлен в $WX_DIR/install"
    cd "$PROJECT_ROOT"
fi

# ----------------------------------------------------------------------
# Сборка wxSQLite3 (статически, без примеров)
# ----------------------------------------------------------------------
if [[ -d "$WXSQLITE3_DIR" ]]; then
    warn "Директория wxsqlite3 уже существует. Пропускаем сборку wxSQLite3? [y/N]"
    read -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log "Пропущено: wxSQLite3"
    else
        rm -rf "$WXSQLITE3_DIR"
    fi
fi

if [[ ! -d "$WXSQLITE3_DIR" ]]; then
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

    # Создаём build рядом с src
    mkdir -p ../build
    cd ../build

    # префикс указываем абсолютный, чтобы всё попало в WXSQLITE3_DIR/install
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

    # Определяем имя цели библиотеки
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

    # Собираем только библиотеку
    make -j$(nproc) "$LIB_TARGET"

    # Устанавливаем библиотеку и заголовки
    make install-exec

    # Теперь копируем заголовки в правильное место (они уже будут скопированы в install/include/wx)
    # Но проверим, если их нет — скопируем.
    if [[ ! -d "$WXSQLITE3_DIR/install/include/wx" ]]; then
        log "Копирование заголовков вручную..."
        mkdir -p "$WXSQLITE3_DIR/install/include/wx"
        cp -r "$WXSQLITE3_DIR/src/include/wx/"* "$WXSQLITE3_DIR/install/include/wx/"
    fi

    log "wxSQLite3 установлен в $WXSQLITE3_DIR/install"
    cd "$PROJECT_ROOT"
fi

log "✅ Все зависимости установлены!"
log "Теперь можно собирать проект:"
echo "  ./build-release.sh"
echo "  или"
echo "  cd build && cmake .. && make"
echo