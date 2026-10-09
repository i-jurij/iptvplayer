#!/bin/sh

# =============================================================================
# setup-deps.sh – установка зависимостей iptvplayer
# =============================================================================
#
# Назначение:
#   - Проверяет и устанавливает системные зависимости (если не отключено)
#   - Скачивает и собирает статические библиотеки wxWidgets и wxSQLite3
#     в папку third_party/ проекта.
#   - Поддерживает интерактивный и неинтерактивный (CI) режимы.
#
# Использование:
#   ./scripts/setup-deps.sh [--yes] [--skip-system] [--rebuild-deps]
#
# Опции:
#   --yes            Автоматически соглашаться на все запросы (неинтерактивный).
#   --skip-system    Не устанавливать системные пакеты (только сборка third_party).
#   --rebuild-deps   Удалить third_party/wx и third_party/wxsqlite3 перед сборкой
#                    (принудительная пересборка без смены версии).
#
# Переменные окружения:
#   SETUP_DEPS_SKIP_SYSTEM=1   эквивалентно --skip-system
#
# Примеры:
#   ./scripts/setup-deps.sh                              # интерактивный режим
#   ./scripts/setup-deps.sh --yes                        # неинтерактивный (для CI)
#   ./scripts/setup-deps.sh --skip-system                # только сборка third_party
#   ./scripts/setup-deps.sh --yes --rebuild-deps         # принудительная пересборка third_party
# =============================================================================

# --- POSIX-бутстрап: гарантирует bash; при отсутствии — скачивает статический ---
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/bootstrap-bash.sh" || exit 1

if [ -z "${BASH_VERSION:-}" ]; then
    echo "[!] bootstrap-bash.sh не передал управление bash." >&2
    exit 1
fi

set -euo pipefail

# Версии закреплены здесь — единый источник правды.
# Переопределяются через переменные окружения (для бампа версии или
# фиксации конкретной ревизии в CI):
#   WX_VERSION=3.4.0 WXSQLITE3_VERSION=5.1.0 ./scripts/setup-deps.sh
WX_VERSION="${WX_VERSION:-3.3.2}"
WXSQLITE3_VERSION="${WXSQLITE3_VERSION:-5.0.1}"

# ---- Каталог скриптов и корень проекта ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---- Общие утилиты ----
source "$SCRIPT_DIR/common.sh"

if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    error "Скрипт должен быть запущен из корня проекта (там, где CMakeLists.txt)."
fi

log "Корень проекта: $PROJECT_ROOT"
log "Версии зависимостей: wxWidgets $WX_VERSION, wxSQLite3 $WXSQLITE3_VERSION"

THIRD_PARTY_DIR="$PROJECT_ROOT/third_party"
WX_DIR="$THIRD_PARTY_DIR/wx"
WXSQLITE3_DIR="$THIRD_PARTY_DIR/wxsqlite3"

STATE_FILE="$PROJECT_ROOT/.iptvplayer-deps-installed"

# ---- Обработка аргументов ----
SKIP_SYSTEM=false
FORCE_DEPS_REBUILD=false
KEEP_DEPS=false
CLEANUP_ONLY=false

for arg in "$@"; do
    case "$arg" in
        --yes|-y)                      NON_INTERACTIVE=true ;;
        --skip-system)                 SKIP_SYSTEM=true ;;
        --rebuild-deps)                FORCE_DEPS_REBUILD=true ;;
        --keep-deps|--no-cleanup-deps) KEEP_DEPS=true ;;
        --cleanup-deps)                CLEANUP_ONLY=true ;;
        *) error "Неизвестный аргумент: $arg" ;;
    esac
done

# -------------------------------------------------------------------------
# Уборка зависимостей: write_deps_state / cleanup_deps / select_deps_file.
# -------------------------------------------------------------------------

write_deps_state() {
    [ "$KEEP_DEPS" = true ] && return 0
    [ ${#MISSING_PACKAGES[@]} -eq 0 ] && return 0
    touch "$STATE_FILE"
    printf '%s\n' "${MISSING_PACKAGES[@]}" >> "$STATE_FILE"
    sort -u "$STATE_FILE" -o "$STATE_FILE"
    log "Состояние сохранено: $STATE_FILE"
}

cleanup_deps() {
    [ -f "$STATE_FILE" ] || { log "Нечего удалять: $STATE_FILE отсутствует."; return 0; }
    local pkgs=()
    while IFS= read -r _p; do
        [ -z "$_p" ] && continue
        case "$_p" in \#*) continue ;; esac
        pkgs+=("$_p")
    done < "$STATE_FILE"
    if [ ${#pkgs[@]} -eq 0 ]; then
        rm -f "$STATE_FILE"; log "Стейт пуст — удалён."; return 0
    fi
    log "Удаление установленных нами пакетов (${#pkgs[@]} шт.)..."
    deps_remove "${pkgs[@]}" || warn "deps_remove вернул ошибку — проверьте вручную."
    rm -f "$STATE_FILE"
    log "✅ Пакеты удалены, состояние очищено."
}

select_deps_file() {
    PKG_MANAGER="$(detect_pkgmgr)"
    if [[ "$PKG_MANAGER" == dnf ]] && distro_is 'rosa-*'; then
        DEPS_FILE="$SCRIPT_DIR/setup-deps-rosa.sh"
    else
        case "$PKG_MANAGER" in
            apt)     DEPS_FILE="$SCRIPT_DIR/setup-deps-deb.sh"  ;;
            dnf)     DEPS_FILE="$SCRIPT_DIR/setup-deps-rpm.sh"  ;;
            zypper)  DEPS_FILE="$SCRIPT_DIR/setup-deps-suse.sh" ;;
            apt-rpm) DEPS_FILE="$SCRIPT_DIR/setup-deps-alt.sh"  ;;
            pacman)  DEPS_FILE="$SCRIPT_DIR/setup-deps-arch.sh" ;;
            apk)     DEPS_FILE="$SCRIPT_DIR/setup-deps-apk.sh"  ;;
            *)       error "Неподдерживаемая ОС: $DISTRO (pkgmgr=$PKG_MANAGER)" ;;
        esac
    fi
    [ -f "$DEPS_FILE" ] || error "Не найден $DEPS_FILE для pkgmgr=$PKG_MANAGER"
}

# Ранний выход: только уборка, без сборки. Доступен только после
# bootstrap (bash) — иначе local/declare были бы недоступны.
if [ "$CLEANUP_ONLY" = true ]; then
    detect_distro
    # select_deps_file присваивает глобальные PKG_MANAGER и DEPS_FILE;
    # отсюда их и берём (аналогично основной секции установки).
    select_deps_file
    # shellcheck disable=SC1090
    source "$DEPS_FILE"
    if command -v sudo >/dev/null 2>&1; then SUDO="sudo"
    elif [ "$(id -u)" = "0" ]; then SUDO=""
    else error "Нужен sudo или root для удаления пакетов"; fi
    cleanup_deps
    exit 0
fi

# Переменная окружения также управляет SKIP_SYSTEM
if [[ "${SETUP_DEPS_SKIP_SYSTEM:-0}" == "1" ]]; then
    SKIP_SYSTEM=true
    log "Переменная SETUP_DEPS_SKIP_SYSTEM=1, установка системных пакетов пропущена."
fi

# =============================================================================
# ДЕТЕКТИРОВАНИЕ ОС И ПАКЕТНОГО МЕНЕДЖЕРА (если не пропущена установка)
# =============================================================================
if [[ "$SKIP_SYSTEM" == false ]]; then
    section "ДЕТЕКТИРОВАНИЕ ОС И ПАКЕТНОГО МЕНЕДЖЕРА"

    detect_distro
    select_deps_file
    log "Пакетный менеджер: $PKG_MANAGER"

    if command -v sudo >/dev/null 2>&1; then SUDO="sudo"
    elif [ "$(id -u)" = "0" ]; then SUDO=""
    else error "Нужен sudo или root для установки пакетов"; fi

    # shellcheck disable=SC1090
    source "$DEPS_FILE"

    # RHEL-family: EPEL/RPM Fusion. Только для чистого dnf, не для ROSA.
    # _pkgtable — локальная переменная select_deps_file и здесь недоступна;
    # проверяем по PKG_MANAGER + DISTRO.
    if [[ "$PKG_MANAGER" == dnf ]] && ! distro_is 'rosa-*'; then
        _id="${DISTRO%%-*}"; _major="$(distro_major)"
        case "$_id" in
            rhel|rocky|almalinux|centos)
                case "$_major" in
                    8)    _repo_pkg=rpmfusion-free-release
                          _repo_name="RPM Fusion"
                          _repo_url="https://download1.rpmfusion.org/free/el/rpmfusion-free-release-8.noarch.rpm" ;;
                    9|10) _repo_pkg=epel-release
                          _repo_name="EPEL $_major"
                          _repo_url="https://dl.fedoraproject.org/pub/epel/epel-release-latest-${_major}.noarch.rpm" ;;
                    *)    _repo_pkg="" ;;
                esac
                if [[ -n "$_repo_pkg" ]] && ! rpm -q "$_repo_pkg" &>/dev/null; then
                    echo
                    warn "Для сборки на $DISTRO требуется $_repo_name."
                    if ask "Подключить $_repo_name? [Y/n]:" y; then
                        $SUDO dnf install -y "$_repo_url" \
                            || error "Не удалось подключить $_repo_name."
                    else
                        error "Без $_repo_name сборка невозможна."
                    fi
                fi ;;
        esac
    fi

    section "ПРОВЕРКА УСТАНОВЛЕННЫХ ПАКЕТОВ"

    MISSING_LABELS=()
    MISSING_PACKAGES=()

    while IFS=: read -r _label _pkgs; do
        [ -z "$_label" ] && continue
        _found=false
        for _p in $_pkgs; do
            if deps_is_installed "$_p"; then _found=true; break; fi
        done
        if [[ "$_found" == true ]]; then
            log "✓ $_label: установлен"
        else
            warn "✗ $_label: НЕ установлен ($_pkgs)"
            MISSING_LABELS+=("$_label")
            # shellcheck disable=SC2206
            MISSING_PACKAGES+=($_pkgs)
        fi
    done < <(deps_packages)

    if [[ ${#MISSING_PACKAGES[@]} -gt 0 ]]; then
        section "УСТАНОВКА НЕДОСТАЮЩИХ ПАКЕТОВ"
        echo "Требуется установить (${MISSING_LABELS[*]}):"
        printf '  - %s\n' "${MISSING_PACKAGES[@]}"
        echo
        if [[ "$NON_INTERACTIVE" == true ]]; then
            log "Неинтерактивный режим: установка будет выполнена автоматически."
            RESPONSE="y"
        else
            read -p "Продолжить установку? [y/N] " -n 1 -r
            echo
            RESPONSE="$REPLY"
        fi
        [[ $RESPONSE =~ ^[Yy]$ ]] || error "Установка прервана пользователем."

        log "Обновление индексов пакетного менеджера..."
        deps_update

        INSTALL_LOG="$PROJECT_ROOT/.install-deps.log"
        log "Установка: ${MISSING_PACKAGES[*]}"
        log "Полный лог: $INSTALL_LOG"
        if ! deps_install "${MISSING_PACKAGES[@]}" 2>&1 | tee "$INSTALL_LOG"; then
            echo
            error "Установка не удалась. Полный лог: $INSTALL_LOG"
        fi
        log "✅ Пакеты установлены"

        # Сохраняем список установленного — build-release.sh --cleanup
        # уберёт их после успешной сборки (если не --keep-deps).
        write_deps_state
    else
        log "✅ Все обязательные пакеты уже установлены"
    fi
else
    log "Пропускаем установку системных пакетов (--skip-system или переменная)."
fi

# =============================================================================
# СБОРКА ЗАВИСИМОСТЕЙ ПРОЕКТА (wxWidgets, wxSQLite3) – выполняется всегда
# =============================================================================

section "СБОРКА WXWIDGETS И WXSQLITE3"

if [[ "$FORCE_DEPS_REBUILD" == true ]]; then
    log "--rebuild-deps: удаляем third_party/wx и third_party/wxsqlite3 перед сборкой."
    rm -rf "$THIRD_PARTY_DIR/wx" "$THIRD_PARTY_DIR/wxsqlite3"
fi

# Проверяем наличие критических инструментов после установки пакетов
# Проверяем критические инструменты. Если чего-то нет — предлагаем
# доустановить через deps_install (кроме --skip-system, где установка
# системных пакетов явно отключена пользователем).
_missing_tools=()
command -v cmake    >/dev/null 2>&1 || _missing_tools+=("cmake")
command -v git      >/dev/null 2>&1 || warn "git не найден — версионирование будет отключено"
command -v autoreconf >/dev/null 2>&1 || _missing_tools+=("autoreconf")

if [ ${#_missing_tools[@]} -gt 0 ]; then
    if [[ "$SKIP_SYSTEM" == true ]]; then
        error "Не хватает: ${_missing_tools[*]}. Установка системных пакетов отключена (--skip-system) — поставьте вручную."
    fi

    # Имена пакетов для тулов: cmake везде cmake; autoreconf — это
    # autoconf+automake+libtool (отдельного пакета autoreconf нет).
    _to_install=()
    for _t in "${_missing_tools[@]}"; do
        case "$_t" in
            autoreconf) _to_install+=("autoconf" "automake" "libtool") ;;
            *)          _to_install+=("$_t") ;;
        esac
    done

    warn "Не хватает обязательных инструментов: ${_missing_tools[*]}"
    if ask "Установить сейчас (${_to_install[*]})? [Y/n]:" y; then
        log "Установка: ${_to_install[*]}"
        if ! deps_install "${_to_install[@]}"; then
            error "Не удалось установить ${_missing_tools[*]}. Установите вручную."
        fi
        hash -r   # сбросить кэш команд bash — иначе command -v может не увидеть свежий бинарник
    else
        error "Без ${_missing_tools[*]} сборка невозможна."
    fi

    # Перепроверяем после установки.
    _still_missing=()
    for _t in "${_missing_tools[@]}"; do
        command -v "$_t" >/dev/null 2>&1 || _still_missing+=("$_t")
    done
    if [ ${#_still_missing[@]} -gt 0 ]; then
        error "После установки всё ещё не хватает: ${_still_missing[*]}. Проверьте PATH или установите вручную."
    fi
fi

log "Используется загрузчик: $DOWNLOADER"


mkdir -p "$THIRD_PARTY_DIR"
cd "$PROJECT_ROOT"

# -------------------- wxWidgets --------------------
section "СБОРКА WXWIDGETS $WX_VERSION"

# Готовность = директория + артефакт. Директория без артефакта — сломанное
# состояние (сборка не докатилась), поэтому такой случай не отличается от «нет директории».
if [[ -d "$WX_DIR" && -f "$WX_DIR/install/lib/libwx_gtk3u_core-${WX_VERSION%.*}.a" ]]; then
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: пропускаем сборку wxWidgets (используем существующую)."
        SKIP_WX=true
    else
        warn "wx уже существует в каталоге. Пропускаем сборку wxWidgets? [y/N]"
        read -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            SKIP_WX=true
        else
            SKIP_WX=false
            rm -rf "$WX_DIR"
        fi
    fi
else
    if [[ -d "$WX_DIR" ]]; then
        log "wxWidgets: директория есть, артефакт не найден — пересобираем."
        rm -rf "$WX_DIR"
    fi
    SKIP_WX=false
fi

if [[ "$SKIP_WX" != true ]]; then
    log "Сборка wxWidgets $WX_VERSION..."
    mkdir -p "$WX_DIR"
    cd "$WX_DIR"

    WX_ARCHIVE="wxWidgets-$WX_VERSION.tar.bz2"
    WX_URL="https://github.com/wxWidgets/wxWidgets/releases/download/v$WX_VERSION/$WX_ARCHIVE"

    if ! download "$WX_URL" "$WX_ARCHIVE"; then
        error "Скачивание wxWidgets не удалось"
    fi

    tar xf "$WX_ARCHIVE" && rm "$WX_ARCHIVE"
    mv "wxWidgets-$WX_VERSION" src
    mkdir -p build install
    cd build

    export CXXFLAGS="-std=c++20"
     cmake ../src \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DwxBUILD_SHARED=OFF \
        -DwxUSE_LIBWEBP=builtin \
        -DwxUSE_LIBJPEG=builtin \
        -DwxUSE_SVG=ON \
        -DwxUSE_REGEX:BOOL=OFF \
        -DwxUSE_MEDIACTRL:BOOL=OFF \
        -DwxUSE_GSTREAMER:BOOL=OFF \
        -DwxUSE_WEBKIT:BOOL=OFF \
        -DwxUSE_WEBVIEW_WEBKIT:BOOL=OFF \
        -DwxUSE_LIBTIFF:BOOL=OFF \
        -DwxUSE_GIF:BOOL=OFF \
        -DwxUSE_ACCESSIBILITY:BOOL=OFF \
        -DwxUSE_HELP:BOOL=OFF \
        -DwxUSE_HTML:BOOL=OFF \
        -DwxUSE_STC:BOOL=OFF \
        -DwxUSE_MS_HTML_HELP:BOOL=OFF \
        -DwxUSE_WXHTML_HELP:BOOL=OFF \
        -DwxUSE_XRC:BOOL=OFF \
        -DwxUSE_XML:BOOL=OFF \
        -DwxUSE_NET:BOOL=OFF \
        -DwxUSE_RICHTEXT:BOOL=OFF \
        -DwxUSE_LIBSDL:BOOL=OFF \
        -DwxUSE_PRINTING_ARCHITECTURE=OFF \
        -DCMAKE_INSTALL_PREFIX=../install

    cmake --build . --target install -j$(nproc)
    log "wxWidgets установлен в $WX_DIR/install"
    cd "$PROJECT_ROOT"
fi

# Проверка, что wx-config существует
if [[ ! -f "$WX_DIR/install/bin/wx-config" ]]; then
    error "wx-config не найден в $WX_DIR/install/bin. Сборка wxWidgets не удалась."
fi

# -------------------- wxSQLite3 --------------------
section "СБОРКА WXSQLITE3 $WXSQLITE3_VERSION"

if [[ -d "$WXSQLITE3_DIR" && -f "$WXSQLITE3_DIR/install/lib/libwxcode_gtk3u_wxsqlite3-${WX_VERSION%.*}.a" ]]; then
    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "Неинтерактивный режим: пропускаем сборку wxSQLite3 (используем существующую)."
        SKIP_WXSQLITE3=true
    else
        warn "wxsqlite3 уже существует. Пропускаем сборку wxSQLite3? [y/N]"
        read -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            SKIP_WXSQLITE3=true
        else
            SKIP_WXSQLITE3=false
            rm -rf "$WXSQLITE3_DIR"
        fi
    fi
else
    if [[ -d "$WXSQLITE3_DIR" ]]; then
        log "wxSQLite3: директория есть, артефакт не найден — пересобираем."
        rm -rf "$WXSQLITE3_DIR"
    fi
    SKIP_WXSQLITE3=false
fi

if [[ "$SKIP_WXSQLITE3" != true ]]; then
    log "Сборка wxSQLite3 $WXSQLITE3_VERSION..."
    mkdir -p "$WXSQLITE3_DIR"
    cd "$WXSQLITE3_DIR"

    WXSQLITE3_ARCHIVE="v$WXSQLITE3_VERSION.tar.gz"
    # Первым — codeload.github.com напрямую: минует прокси github.com,
    # который чаще всего и отдаёт 504 (см. transient-сбои в CI).
    # Вторым — привычный github.com/.../archive/... (редиректит на тот же
    # codeload, но иногда жив, когда прямой путь залип).
    WXSQLITE3_URLS=(
        "https://codeload.github.com/utelle/wxsqlite3/tar.gz/refs/tags/v$WXSQLITE3_VERSION"
        "https://github.com/utelle/wxsqlite3/archive/refs/tags/$WXSQLITE3_ARCHIVE"
    )

    # UA сохраняем — изначально он был нужен именно для codeload.
    DOWNLOAD_UA="Mozilla/5.0"
    if ! download_multi "$WXSQLITE3_ARCHIVE" "${WXSQLITE3_URLS[@]}"; then
        error "Скачивание wxSQLite3 не удалось"
    fi
    unset DOWNLOAD_UA

    tar xf "$WXSQLITE3_ARCHIVE" && rm "$WXSQLITE3_ARCHIVE"

    if [[ -d "wxsqlite3-$WXSQLITE3_VERSION" ]]; then
        mv "wxsqlite3-$WXSQLITE3_VERSION" src
    elif [[ -d "wxSQLite3-$WXSQLITE3_VERSION" ]]; then
        mv "wxSQLite3-$WXSQLITE3_VERSION" src
    else
        error "Не найдена распакованная папка. Содержимое: $(ls -la)"
    fi

    cd src
    autoreconf -fi

    WX_ROOT_ABS="$WX_DIR/install"
    log "Путь к wxWidgets: $WX_ROOT_ABS"

    mkdir -p ../build
    cd ../build

    export CXXFLAGS="-std=c++20"
    ../src/configure \
        --prefix="$WXSQLITE3_DIR/install" \
        --with-wx-config="$WX_ROOT_ABS/bin/wx-config" \
        --disable-shared \
        --enable-static \
        --without-sqlcipher \
        --without-aes128cbc \
        --without-ascon128 \
        --without-aegis

    MAKEFILE="Makefile"
    if [[ ! -f "$MAKEFILE" ]]; then
        error "Makefile не найден в $(pwd)"
    fi

    LIB_TARGET=$(grep -E '^[[:space:]]*lib.*_LTLIBRARIES' "$MAKEFILE" 2>/dev/null | sed 's/.*=//' | awk '{print $1}' | head -1 | xargs || true)
    if [[ -z "$LIB_TARGET" ]]; then
        LIB_TARGET=$(find . -maxdepth 1 -type f -name "*.la" -printf "%f\n" | head -1 || true)
    fi
    if [[ -z "$LIB_TARGET" ]]; then
        error "Не удалось определить цель библиотеки из Makefile"
    fi
    log "Цель библиотеки: $LIB_TARGET"

    make -j$(nproc) "$LIB_TARGET"
    make install-exec

    if [[ ! -d "$WXSQLITE3_DIR/install/include/wx" ]]; then
        log "Копирование заголовков вручную..."
        mkdir -p "$WXSQLITE3_DIR/install/include/wx"
        cp -r "$WXSQLITE3_DIR/src/include/wx/"* "$WXSQLITE3_DIR/install/include/wx/"
    fi

    log "wxSQLite3 установлен в $WXSQLITE3_DIR/install"
    cd "$PROJECT_ROOT"
fi

# =============================================================================
# ЗАВЕРШЕНИЕ
# =============================================================================

section "ГОТОВО"

log "✅ Все зависимости установлены!"
echo "Теперь можно собирать проект:"
echo "  ./scripts/build-release.sh"
echo "  или (ручная сборка):"
echo "  mkdir -p build && cd build"
echo "  cmake .. && make -j$(nproc)"
echo