#!/bin/bash

# build-release.sh - Скрипт для сборки релизной версии iptvplayer
# Использование: ./build-release.sh [clean]

set -euo pipefail

# Цвета для вывода
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

# Проверка наличия cmake
if ! command -v cmake &> /dev/null; then
    error "cmake не установлен. Установите CMake (>= 3.20)."
fi

# Проверка наличия make или ninja
MAKE_CMD="make"
if command -v ninja &> /dev/null; then
    MAKE_CMD="ninja"
fi
log "Используется: $MAKE_CMD"

# Имена директорий
BUILD_DIR="build-release"
INSTALL_DIR="install"
PROJECT_NAME="iptvplayer"  # Явно задано — лучше читаемость

# Очистка (если передан аргумент 'clean')
if [[ "${1:-}" == "clean" ]]; then
    log "Очистка предыдущей сборки..."
    rm -rf "$BUILD_DIR"
    rm -rf "$INSTALL_DIR"
fi

# Создание директории сборки
if [[ -d "$BUILD_DIR" && ! -z "$(ls -A "$BUILD_DIR")" ]]; then
    warn "Директория '$BUILD_DIR' уже существует и не пуста."
    read -p "Удалить и пересоздать? [y/N]: " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        error "Сборка прервана пользователем."
    fi
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Конфигурация CMake
log "Конфигурация CMake (Release)..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="../$INSTALL_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Сборка
log "Сборка проекта..."
cmake --build . --config Release --target all -j$(nproc)

# Установка
log "Установка в ../$INSTALL_DIR"
cmake --build . --config Release --target install

# Копирование иконок (на случай, если install не копирует)
ICONS_SRC="../icons"
ICONS_DST="../$INSTALL_DIR/bin/icons"

if [[ -d "$ICONS_SRC" ]]; then
    mkdir -p "$ICONS_DST"
    cp -r "$ICONS_SRC"/* "$ICONS_DST/" 2>/dev/null || true
    log "Иконки скопированы в: $ICONS_DST"
else
    warn "Папка иконок не найдена: $ICONS_SRC"
fi

# Проверка, что исполняемый файл создан
EXE_PATH="../$INSTALL_DIR/bin/$PROJECT_NAME"
if [[ -x "$EXE_PATH" ]]; then
    log "✅ Сборка завершена! Приложение готово к запуску."
else
    error "Исполняемый файл не найден: $EXE_PATH"
fi

echo
echo -e "${GREEN}Запуск:${NC}"
echo "  cd ../$INSTALL_DIR/bin"
echo "  ./$PROJECT_NAME"

# Копируем compile_commands.json в корень (для удобства IDE)
cp ./compile_commands.json ../ || true