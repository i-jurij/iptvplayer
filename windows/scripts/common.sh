#!/usr/bin/env bash
# =============================================================================
# windows/scripts/common.sh — общие функции для Windows-сборки.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "common.sh — библиотека, не запускается напрямую." >&2
    exit 1
fi

: "${SCRIPT_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
: "${PROJECT_ROOT:=$(cd "$SCRIPT_DIR/../.." && pwd)}"

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log()     { echo -e "${GREEN}[INFO]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC} $*" >&2; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }
section() { echo -e "\n${BLUE}════════════════════════════════════════════${NC}"; \
            echo -e "${BLUE}$*${NC}"; \
            echo -e "${BLUE}════════════════════════════════════════════${NC}\n"; }

# Если ASSUME_YES=true — ask() молча возвращает true (default).
# Используется флагом --yes для неинтерактивного режима в CI.
ask() {
    local prompt="$1" default="${2:-n}" reply=""
    if [[ "${ASSUME_YES:-false}" == "true" ]]; then
        return 0
    fi
    read -p "$prompt " -n 1 -r reply || true
    echo
    [[ -z "$reply" ]] && reply="$default"
    [[ "$reply" =~ ^[Yy]$ ]]
}

check_msys2() {
    if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
        error "Скрипт должен запускаться из MSYS2 UCRT64. " \
              "MINGW64 сворачивается с 2026-03-15 и не поддерживается. " \
              "Текущее окружение: MSYSTEM=${MSYSTEM:-<не задано>}"
    fi
}

# Корень MSYS2-окружения (используется в CMake и линковке).
msys2_prefix() {
    case "$MSYSTEM" in
        UCRT64) echo "/ucrt64" ;;
        *) error "Неизвестный MSYSTEM: $MSYSTEM (ожидается UCRT64)" ;;
    esac
}

# Префикс имён pacman-пакетов. Отличается от msys2_prefix():
# для UCRT64 это mingw-w64-ucrt-x86_64-*.
msys2_pkg_prefix() {
    case "$MSYSTEM" in
        UCRT64) echo "mingw-w64-ucrt-x86_64" ;;
        *) error "Неизвестный MSYSTEM: $MSYSTEM (ожидается UCRT64)" ;;
    esac
}

win_build_dir()   { echo "$PROJECT_ROOT/windows/build"; }
win_install_dir() { echo "$PROJECT_ROOT/windows/install"; }
win_dist_dir()    { echo "$PROJECT_ROOT/windows/dist"; }
win_thirdparty()  { echo "$PROJECT_ROOT/windows/third_party"; }