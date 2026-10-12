#!/usr/bin/env bash
# bootstrap.sh — проверка окружения перед сборкой.

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "bootstrap.sh — библиотека." >&2
    exit 1
fi

check_msys2

command -v cmake >/dev/null 2>&1 || error "cmake не найден. Установите: pacman -S cmake"
command -v ninja >/dev/null 2>&1 || error "ninja не найден. Установите: pacman -S ninja"
command -v make  >/dev/null 2>&1 || error "make не найден. Установите: pacman -S make"
command -v gcc   >/dev/null 2>&1 || error "gcc не найден. Установите: pacman -S mingw-w64-${MSYSTEM,,}-gcc"
command -v ldd   >/dev/null 2>&1 || error "ldd не найден"