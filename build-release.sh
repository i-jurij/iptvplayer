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
#   ./build-release.sh [опции]
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
#   ./build-release.sh                             # релизная сборка в ./install
#   ./build-release.sh --type debug                # отладочная сборка
#   ./build-release.sh --clean --prefix ./my_build # очистка и установка в ./my_build
#   ./build-release.sh --log                       # сборка с логированием
#   ./build-release.sh --yes --prefix ./install    # неинтерактивная сборка (CI)
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

# -------- Настройки (можно менять) --------
PROJECT_NAME="iptvplayer"
BUILD_TYPE="Release"          # по умолчанию
DO_CLEAN=false
PREFIX="install"              # по умолчанию — папка в корне проекта
JOBS=$(nproc)                 # количество потоков
LOG_FILE=""                   # если задан, вывод дублируется в файл
# -----------------------------------------

# -------- Неинтерактивный режим --------
# Авто-детект: CI env vars или отсутствие TTY.
NON_INTERACTIVE=false
if [[ -n "${CI:-}" ]] || [[ -n "${GITHUB_ACTIONS:-}" ]] || [[ ! -t 0 ]]; then
    NON_INTERACTIVE=true
fi

# Цвета для вывода
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[INFO]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1" >&2; }
error() { echo -e "${RED}[ERROR]${NC} $1" >&2; exit 1; }

# Спросить пользователя. Возвращает 0 для "да", 1 для "нет".
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

# Определяем корень проекта (там, где лежит этот скрипт)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
    PREFIX="$SCRIPT_DIR/$PREFIX"
fi

# Директории
BUILD_DIR="$SCRIPT_DIR/build-${BUILD_TYPE,,}"   # build-release или build-debug
INSTALL_DIR="$PREFIX"

# Проверка наличия собранных зависимостей
check_deps() {
    local wx_lib="$SCRIPT_DIR/third_party/wx/install/lib/libwx_gtk3u_core-3.3.a"
    local sqlite_lib="$SCRIPT_DIR/third_party/wxsqlite3/install/lib/libwxcode_gtk3u_wxsqlite3-3.3.a"
    local missing=()
    [[ ! -f "$wx_lib" ]] && missing+=("wxWidgets (не найден $wx_lib)")
    [[ ! -f "$sqlite_lib" ]] && missing+=("wxSQLite3 (не найден $sqlite_lib)")
    if [[ ${#missing[@]} -gt 0 ]]; then
        warn "Отсутствуют зависимости:"
        printf '  - %s\n' "${missing[@]}"
        echo "Запустите $SCRIPT_DIR/setup-deps.sh для установки зависимостей."
        if ! ask "Продолжить сборку всё равно? [y/N]:" n; then
            error "Сборка прервана из-за отсутствующих зависимостей. Запустите $SCRIPT_DIR/setup-deps.sh."
        fi
    else
        log "Все зависимости найдены."
    fi
}

cd "$SCRIPT_DIR"
check_deps

# Очистка
if [[ "$DO_CLEAN" == true ]]; then
    log "Очистка предыдущих сборок..."
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
elif [[ -d "$BUILD_DIR" && -n "$(ls -A "$BUILD_DIR" 2>/dev/null)" ]]; then
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: директория '$BUILD_DIR' существует, выполняем чистую пересборку."
        rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    else
        warn "Директория '$BUILD_DIR' уже существует."
        if ! ask "Пересобрать с очисткой? [y/N]:" n; then
            error "Сборка прервана пользователем."
        fi
        rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Конфигурация CMake
log "Конфигурация CMake (${BUILD_TYPE})..."
cmake "$SCRIPT_DIR" \
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
EXE_PATH="$INSTALL_DIR/bin/$PROJECT_NAME"
if [[ ! -x "$EXE_PATH" ]]; then
    error "Исполняемый файл не найден: $EXE_PATH"
fi

log "✅ Сборка завершена! Приложение готово к запуску."
echo -e "${GREEN}Запуск:${NC} cd $INSTALL_DIR/bin && ./$PROJECT_NAME"

# Копируем compile_commands.json в корень (для IDE)
cp "$BUILD_DIR/compile_commands.json" "$SCRIPT_DIR/" 2>/dev/null || true

if [[ -n "$LOG_FILE" ]]; then
    log "Полный лог сохранён в $LOG_FILE"
fi