#!/usr/bin/env bash
# bootstrap.sh — проверка окружения перед сборкой.
#
# ВАЖНО: здесь только проверка MSYS2. Проверки наличия инструментов
# (cmake/ninja/gcc) — в check_build_tools() из common.sh. Их нельзя
# звать из setup-deps.sh: на свежей системе эти инструменты ещё не
# установлены, их ставит как раз setup-deps.sh.

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "bootstrap.sh — библиотека." >&2
    exit 1
fi

check_msys2