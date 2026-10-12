#!/usr/bin/env bash
# =============================================================================
# setup-deps.sh — установка зависимостей под Windows (MSYS2).
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/bootstrap.sh"

WXSQLITE3_VERSION="${WXSQLITE3_VERSION:-5.0.1}"
WXSQLITE3_DIR="$(win_thirdparty)/wxsqlite3"
prefix_lower="${MSYSTEM,,}"

section "1/3. Пакеты MSYS2"

PKGS=(
    "mingw-w64-${prefix_lower}-gcc"
    "mingw-w64-${prefix_lower}-cmake"
    "mingw-w64-${prefix_lower}-ninja"
    "mingw-w64-${prefix_lower}-make"
    "mingw-w64-${prefix_lower}-wxwidgets3.2-msw"
    "mingw-w64-${prefix_lower}-mpv"
    "mingw-w64-${prefix_lower}-curl"
    "mingw-w64-${prefix_lower}-expat"
    "mingw-w64-${prefix_lower}-zlib"
    "mingw-w64-${prefix_lower}-libwebp"
    "mingw-w64-${prefix_lower}-rapidjson"
    "mingw-w64-${prefix_lower}-gettext"
    "mingw-w64-${prefix_lower}-nsis"
    "git"
)

MISSING=()
for pkg in "${PKGS[@]}"; do
    if ! pacman -Qi "$pkg" >/dev/null 2>&1; then
        MISSING+=("$pkg")
    fi
done

if [[ ${#MISSING[@]} -gt 0 ]]; then
    log "Отсутствуют пакеты: ${MISSING[*]}"
    if ask "Установить их через pacman? [Y/n]:" y; then
        pacman -S --needed --noconfirm "${MISSING[@]}"
    else
        error "Без этих пакетов сборка невозможна."
    fi
else
    log "Все пакеты установлены."
fi

section "2/3. wxSQLite3"

if ls "$WXSQLITE3_DIR/install/lib/"libwxcode*.a >/dev/null 2>&1; then
    log "wxSQLite3 уже собран: $WXSQLITE3_DIR/install"
else
    log "wxSQLite3 не найден — собираю из исходников."
    mkdir -p "$WXSQLITE3_DIR"
    cd "$WXSQLITE3_DIR"

    if [[ ! -d src ]]; then
        ARCHIVE="v${WXSQLITE3_VERSION}.tar.gz"
        URLS=(
            "https://codeload.github.com/utelle/wxsqlite3/tar.gz/refs/tags/v${WXSQLITE3_VERSION}"
            "https://github.com/utelle/wxsqlite3/archive/refs/tags/${ARCHIVE}"
        )
        ok=0
        for u in "${URLS[@]}"; do
            log "Скачиваю: $u"
            if curl -fsSL "$u" -o "$ARCHIVE"; then
                ok=1
                break
            fi
        done
        [[ "$ok" = 1 ]] || error "Не удалось скачать wxSQLite3."
        tar xf "$ARCHIVE"
        rm "$ARCHIVE"
        mv wxsqlite3-* src 2>/dev/null || mv wxSQLite3-* src 2>/dev/null || true
    fi

    cd src
    [[ -f configure ]] || autoreconf -fi

    mkdir -p ../build
    cd ../build

    prefix="$(msys2_prefix)"
    ../src/configure \
        --prefix="$WXSQLITE3_DIR/install" \
        --with-wx-config="$prefix/bin/wx-config" \
        --disable-shared \
        --enable-static \
        --without-sqlcipher

    lib_target=$(grep -E '^[[:space:]]*lib.*_LTLIBRARIES' Makefile 2>/dev/null \
                 | sed 's/.*=//' | awk '{print $1}' | head -1 | xargs || true)
    [[ -n "$lib_target" ]] || error "Не удалось определить цель библиотеки."

    make -j"$(nproc)" "$lib_target"
    make install-exec
    mkdir -p "$WXSQLITE3_DIR/install/include/wx"
    cp -r ../src/include/wx/* "$WXSQLITE3_DIR/install/include/wx/"

    log "wxSQLite3 собран в $WXSQLITE3_DIR/install"
    cd "$PROJECT_ROOT"
fi

section "3/3. Готово"
log "Все зависимости на месте."
echo "Следующий шаг: windows\\bats\\build-release.bat"