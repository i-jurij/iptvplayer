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

ask() {
    local prompt="$1" default="${2:-n}" reply=""
    read -p "$prompt " -n 1 -r reply || true
    echo
    [[ -z "$reply" ]] && reply="$default"
    [[ "$reply" =~ ^[Yy]$ ]]
}

check_msys2() {
    if [[ "${MSYSTEM:-}" != "UCRT64" && "${MSYSTEM:-}" != "MINGW64" ]]; then
        error "Скрипт должен запускаться из MSYS2 UCRT64 или MINGW64. " \
              "Текущее окружение: MSYSTEM=${MSYSTEM:-<не задано>}"
    fi
}

msys2_prefix() {
    case "$MSYSTEM" in
        UCRT64) echo "/ucrt64" ;;
        MINGW64) echo "/mingw64" ;;
        *) error "Неизвестный MSYSTEM: $MSYSTEM" ;;
    esac
}

win_build_dir()   { echo "$PROJECT_ROOT/windows/build"; }
win_install_dir() { echo "$PROJECT_ROOT/windows/install"; }
win_dist_dir()    { echo "$PROJECT_ROOT/windows/dist"; }
win_thirdparty()  { echo "$PROJECT_ROOT/windows/third_party"; }