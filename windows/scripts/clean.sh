#!/usr/bin/env bash
# Очистка рабочих каталогов Windows-сборки.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

section "Очистка"

rm -rf "$(win_build_dir)"   && log "Удалён windows/build/"
rm -rf "$(win_install_dir)" && log "Удалён windows/install/"
rm -rf "$(win_dist_dir)"    && log "Удалён windows/dist/"

log "Готово."