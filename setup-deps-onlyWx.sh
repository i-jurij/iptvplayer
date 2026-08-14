#!/bin/bash

# setup-deps.sh - Автоматическая установка зависимостей для iptvplayer
# Использование: ./setup-deps.sh

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

# Проверки
if ! command -v cmake &> /dev/null; then
    error "cmake не установлен. Установите CMake (>= 3.20)."
fi

if ! command -v wget &> /dev/null && ! command -v curl &> /dev/null; then
    error "Установите wget или curl."
fi

# Выбираем загрузчик
DOWNLOADER="wget"
if command -v curl &> /dev/null; then
    DOWNLOADER="curl"
fi
log "Используется: $DOWNLOADER"

# Директории
THIRD_PARTY_DIR="third_party"
WX_DIR="$THIRD_PARTY_DIR/wx"

# Версии
WX_VERSION="3.3.2"

# Создаём структуру
mkdir -p "$THIRD_PARTY_DIR"
cd "$THIRD_PARTY_DIR"

# ================================
# Сборка wxWidgets 3.3.2 (статически)
# ================================
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

    # Скачиваем
    WX_ARCHIVE="wxWidgets-$WX_VERSION.tar.bz2"
    WX_URL="https://github.com/wxWidgets/wxWidgets/releases/download/v$WX_VERSION/$WX_ARCHIVE"

    if [[ "$DOWNLOADER" == "wget" ]]; then
        wget -q --show-progress "$WX_URL" -O "$WX_ARCHIVE" || error "Скачивание wxWidgets не удалось"
    else
        curl -L -o "$WX_ARCHIVE" "$WX_URL" --progress-bar || error "Скачивание wxWidgets не удалось"
    fi

    # Распаковываем
    tar xf "$WX_ARCHIVE" && rm "$WX_ARCHIVE"
    mv "wxWidgets-$WX_VERSION" src
    mkdir -p build install
    cd build

    # Конфигурируем
    cmake ../src \
        -DCMAKE_BUILD_TYPE=Release \
        -DwxBUILD_SHARED=OFF \
        -DwxUSE_WEBP=ON \
        -DwxUSE_LIBWEBP=builtin \
        -DCMAKE_INSTALL_PREFIX=../install

    # Собираем
    cmake --build . --target install -j$(nproc)
    log "wxWidgets установлен в $WX_DIR/install"
    cd ../../..
fi

log "✅ Все зависимости установлены!"
log "Теперь можно собирать проект:"
echo
echo "  ./build-release.sh"
echo "  или"
echo "  cd build && cmake .. && make"