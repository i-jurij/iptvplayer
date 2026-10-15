#!/usr/bin/env bash
# =============================================================================
# setup-deps.sh — установка зависимостей под Windows (MSYS2 UCRT64).
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/bootstrap.sh"

# --- Парсинг аргументов ---------------------------------------------------
ASSUME_YES=false
for arg in "$@"; do
    case "$arg" in
        -y|--yes)
            ASSUME_YES=true
            ;;
        -h|--help)
            cat <<'EOF'
Использование: ./windows/scripts/setup-deps.sh [--yes]

  --yes    не задавать вопросов; ставить отсутствующие пакеты без подтверждения
EOF
            exit 0
            ;;
        *)
            error "Неизвестный аргумент: $arg"
            ;;
    esac
done

WXSQLITE3_VERSION="${WXSQLITE3_VERSION:-5.0.1}"
WXSQLITE3_DIR="$(win_thirdparty)/wxsqlite3"
PKG_PREFIX="$(msys2_pkg_prefix)"

section "1/3. Пакеты MSYS2"

# MSYS-инструменты: autotools работают в базовом MSYS-окружении, не в UCRT64.
# Префикс mingw-w64-ucrt-x86_64- им не подходит.
#   autoconf, automake, libtool — для autoreconf -fi.
#   autoconf-archive — дополнительные макросы, которые ищет wxSQLite3.
#   make — MSYS make; autotools зовёт именно make.exe.
#   pkgconf — pkg-config для configure.
#   gettext, gettext-devel — MSYS gettext, нужен для autopoint.
#   texinfo — для генерации документации autoreconf.
MSYS_PKGS=(
    "autoconf"
    "autoconf-archive"
    "automake"
    "libtool"
    "make"
    "pkgconf"
    "gettext"
    "gettext-devel"
    "texinfo"
)

# UCRT64-пакеты: компилятор и библиотеки, линкуются в итоговый бинарник.
UCRT_PKGS=(
    "${PKG_PREFIX}-gcc"
    "${PKG_PREFIX}-cmake"
    "${PKG_PREFIX}-ninja"
    "${PKG_PREFIX}-wxwidgets3.2-msw"
    "${PKG_PREFIX}-mpv"
    "${PKG_PREFIX}-curl"
    "${PKG_PREFIX}-expat"
    "${PKG_PREFIX}-zlib"
    "${PKG_PREFIX}-libwebp"
    "${PKG_PREFIX}-rapidjson"
    "${PKG_PREFIX}-gettext"
    "${PKG_PREFIX}-ca-certificates"
    "${PKG_PREFIX}-nsis"
    "git"
)

PKGS=("${MSYS_PKGS[@]}" "${UCRT_PKGS[@]}")

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

    # Используем wx-config из UCRT64. Формат пути — Unix-стиль (/ucrt64/...),
    # MSYS2 autotools его понимает. Проверяем, что бинарь существует.
    prefix="$(msys2_prefix)"
    WX_CONFIG="$prefix/bin/wx-config"
    [[ -x "$WX_CONFIG" ]] || error "wx-config не найден: $WX_CONFIG"

    ../src/configure \
        --prefix="$WXSQLITE3_DIR/install" \
        --with-wx-config="$WX_CONFIG" \
        --disable-shared \
        --enable-static \
        --without-sqlcipher \
        --disable-dependency-tracking \
        2>&1 | tee configure.log

    # Если configure упал — покажем хвост лога и выйдем.
    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo "=== хвост configure.log ===" >&2
        tail -50 configure.log >&2 || true
        error "configure wxSQLite3 завершился с ошибкой"
    fi

    # Определяем цель библиотеки. В Makefile.am lib_LTLIBRARIES = libwxcode_..._wxsqlite3-3.2.la
    # Поэтому ищем в Makefile именно lib_LTLIBRARIES.
    lib_target=$(grep -E '^lib_LTLIBRARIES' Makefile 2>/dev/null \
                 | sed 's/.*=[[:space:]]*//' | awk '{print $1}' || true)
    [[ -n "$lib_target" ]] || lib_target=$(grep -E '^[[:space:]]*lib.*_LTLIBRARIES' Makefile 2>/dev/null \
                 | sed 's/.*=//' | awk '{print $1}' | head -1 | xargs || true)
    [[ -n "$lib_target" ]] || error "Не удалось определить цель библиотеки в Makefile"

    log "Цель сборки: $lib_target"

    # -j$(nproc): nproc есть в MSYS coreutils. --no-print-directory — чище лог.
    make -j"$(nproc)" --no-print-directory "$lib_target" 2>&1 | tee build.log

    if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
        echo "=== хвост build.log ===" >&2
        tail -80 build.log >&2 || true
        error "make wxSQLite3 завершился с ошибкой"
    fi

    # install-exec может отсутствовать в некоторых automake-сборках.
    # Проверяем наличие цели; если нет — копируем вручную.
    if ! make -n install-exec >/dev/null 2>&1; then
        warn "цель install-exec отсутствует — копирую вручную"
        mkdir -p "$WXSQLITE3_DIR/install/lib"
        cp -v .libs/libwxcode*.a "$WXSQLITE3_DIR/install/lib/" 2>/dev/null || true
    else
        make install-exec 2>&1 | tee install.log
        [[ ${PIPESTATUS[0]} -eq 0 ]] || error "make install-exec wxSQLite3 завершился с ошибкой"
    fi

    # Проверяем, что .a действительно установлена.
    ls "$WXSQLITE3_DIR/install/lib/"libwxcode*.a >/dev/null 2>&1 \
        || error "libwxcode*.a не найдена в $WXSQLITE3_DIR/install/lib после сборки"

    mkdir -p "$WXSQLITE3_DIR/install/include/wx"
    cp -r ../src/include/wx/* "$WXSQLITE3_DIR/install/include/wx/"

    log "wxSQLite3 собран в $WXSQLITE3_DIR/install"
    cd "$PROJECT_ROOT"
fi

section "3/3. Готово"
log "Все зависимости на месте."
printf 'Next step: windows\\bats\\build-release.bat\n'