#!/bin/bash

# =============================================================================
# build-release.sh – сборка и установка iptvplayer
# =============================================================================
#
# Назначение:
#   - Собирает проект (Release или Debug) и устанавливает в локальную папку.
#   - По умолчанию создаёт папку сборки build-release (или build-debug)
#     и устанавливает в ./install (корень проекта/install).
#   - Проверяет наличие собранных зависимостей (wxWidgets, wxSQLite3)
#     и предупреждает, если они отсутствуют.
#
# Использование:
#   ./scripts/build-release.sh [опции]
#
# Опции:
#   --clean         удалить старые папки сборки и установки перед сборкой
#   --type TYPE     тип сборки: release (по умолчанию) или debug
#   --prefix PATH   каталог установки (по умолчанию ./install)
#   --log           сохранить лог сборки в файл build_YYYYMMDD_HHMMSS.log
#   --yes, -y       неинтерактивный режим (авто-ответы, для CI)
#   -h, --help      показать справку
#
# Примеры:
#   ./scripts/build-release.sh                             # релизная сборка в ./install
#   ./scripts/build-release.sh --type debug                # отладочная сборка
#   ./scripts/build-release.sh --clean --prefix ./my_build # очистка и установка в ./my_build
#   ./scripts/build-release.sh --log                       # сборка с логированием
#   ./scripts/build-release.sh --yes --prefix ./install    # неинтерактивная сборка (CI)
#
# Примечание:
#   Версия определяется в CMakeLists.txt: чистая версия читается из корневого
#   файла VERSION, git-хеш добавляется автоматически. Результат попадает в
#   version.h (для приложения) и в install/VERSION{,_FULL,_FILE} (для упаковки).
#   После успешной сборки готовый к запуску набор файлов находится в папке,
#   указанной в --prefix (по умолчанию ./install). Запускайте из неё:
#     cd install/bin && ./iptvplayer
# =============================================================================

set -euo pipefail

# ---- Каталог скриптов и корень проекта ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---- Общие утилиты ----
source "$SCRIPT_DIR/common.sh"

# -------- Настройки (можно менять) --------
PROJECT_NAME="iptvplayer"
BUILD_TYPE="Release"          # по умолчанию
DO_CLEAN=false
PREFIX="install"              # по умолчанию — папка в корне проекта
JOBS=$(nproc)                 # количество потоков
LOG_FILE=""                   # если задан, вывод дублируется в файл
# -----------------------------------------

show_help() {
    cat << EOF
Использование: $0 [ОПЦИИ]

Сборка и установка iptvplayer.

Опции:
  --clean         удалить старые папки сборки и установки перед сборкой
  --type TYPE     тип сборки: release (по умолчанию) или debug
  --prefix PATH   каталог установки (по умолчанию ./install)
  --log           сохранить лог сборки в файл build_YYYYMMDD_HHMMSS.log
  --yes, -y       неинтерактивный режим (авто-ответы, для CI)
  -h, --help      показать эту справку

Примеры:
  $0
  $0 --type debug
  $0 --clean --prefix ./my_build
  $0 --log
  $0 --yes --prefix ./install
EOF
}

# Парсинг аргументов
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean)
            DO_CLEAN=true
            shift
            ;;
        --type)
            if [[ -z "${2:-}" ]]; then error "Не указан тип сборки"; fi
            case "$2" in
                release|debug) BUILD_TYPE="$2" ;;
                *) error "Неверный тип: $2. Допустимы: release, debug" ;;
            esac
            shift 2
            ;;
        --prefix)
            if [[ -z "${2:-}" ]]; then error "Не указан путь для --prefix"; fi
            PREFIX="$2"
            shift 2
            ;;
        --log)
            LOG_FILE="build_$(date +%Y%m%d_%H%M%S).log"
            shift
            ;;
        --yes|-y)
            NON_INTERACTIVE=true
            shift
            ;;
        clean)   # совместимость со старым синтаксисом
            DO_CLEAN=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            error "Неизвестный аргумент: $1"
            ;;
    esac
done

# Если LOG_FILE задан, перенаправляем весь вывод (stdout и stderr) через tee
if [[ -n "$LOG_FILE" ]]; then
    exec > >(tee -a "$LOG_FILE") 2>&1
    log "Лог будет сохранён в $LOG_FILE"
fi

# Проверка cmake
command -v cmake >/dev/null || error "cmake не установлен (требуется >= 3.16)"

# Автовыбор генератора
MAKE_CMD="make"
if command -v ninja >/dev/null; then
    MAKE_CMD="ninja"
fi
log "Используется генератор: $MAKE_CMD"

# Преобразуем PREFIX в абсолютный путь, если он относительный
if [[ ! "$PREFIX" = /* ]]; then
    PREFIX="$PROJECT_ROOT/$PREFIX"
fi

# Директории
BUILD_DIR="$PROJECT_ROOT/build-${BUILD_TYPE,,}"   # build-release или build-debug
INSTALL_DIR="$PREFIX"
EXE_PATH="$INSTALL_DIR/bin/$PROJECT_NAME"

cd "$PROJECT_ROOT"

# Локально — зависимости собирает setup-deps.sh (сам решит, что делать).
# В CI — не зовём: там setup-deps.sh отдельный шаг workflow'а ради кеша third_party.
if [[ "$NON_INTERACTIVE" == false ]]; then
    if ! "$SCRIPT_DIR/setup-deps.sh"; then
        error "setup-deps.sh завершился с ошибкой — сборка невозможна."
    fi
fi

# Решение «собрать или переиспользовать» принимается здесь.
REBUILD=true

if [[ "$DO_CLEAN" == true ]]; then
    log "Очистка предыдущих сборок (--clean)..."
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
elif [[ -x "$EXE_PATH" ]]; then
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: бинарник уже собран ($EXE_PATH) — используем существующий."
        REBUILD=false
    elif ask "Бинарник уже собран ($EXE_PATH). Пересобрать? [y/N]:" n; then
        log "Пересобираем бинарник."
        rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    else
        log "Используем существующий бинарник."
        REBUILD=false
    fi
fi

if [[ "$REBUILD" == false ]]; then
    log "✅ Готово (пересборка не требовалась)."
    echo -e "${GREEN}Запуск:${NC} cd $INSTALL_DIR/bin && ./$PROJECT_NAME"
    exit 0
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Конфигурация CMake
log "Конфигурация CMake (${BUILD_TYPE})..."
cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Сборка
log "Сборка проекта (потоков: $JOBS)..."
cmake --build . --config "$BUILD_TYPE" --target all -j"$JOBS"

# Установка
log "Установка в $INSTALL_DIR"
cmake --build . --config "$BUILD_TYPE" --target install

if [[ "$BUILD_TYPE" == "Release" || "$BUILD_TYPE" == "MinSizeRel" ]]; then
    log "Удаление отладочных символов (strip)..."
    #--strip-all агрессивнее --strip-unneeded
    strip --strip-all \
      -R .comment -R .note -R .note.ABI-tag \
      "$INSTALL_DIR/bin/$PROJECT_NAME" 2>/dev/null || warn "strip не удался"
fi

# Проверка исполняемого файла
if [[ ! -x "$EXE_PATH" ]]; then
    error "Исполняемый файл не найден: $EXE_PATH"
fi

log "✅ Сборка завершена! Приложение готово к запуску."
echo -e "${GREEN}Запуск:${NC} cd $INSTALL_DIR/bin && ./$PROJECT_NAME"

# Копируем compile_commands.json в корень (для IDE)
cp "$BUILD_DIR/compile_commands.json" "$PROJECT_ROOT/" 2>/dev/null || true

if [[ -n "$LOG_FILE" ]]; then
    log "Полный лог сохранён в $LOG_FILE"
fi