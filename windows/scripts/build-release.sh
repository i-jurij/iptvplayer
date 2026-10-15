#!/usr/bin/env bash
# =============================================================================
# build-release.sh — сборка iptvplayer под Windows.
# =============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"
source "$SCRIPT_DIR/bootstrap.sh"

# Проверяем инструменты сборки. setup-deps.sh их уже поставил.
check_build_tools

BUILD_DIR="$(win_build_dir)"
INSTALL_DIR="$(win_install_dir)"
THIRDPARTY="$(win_thirdparty)"
PREFIX="$(msys2_prefix)"

FORCE_CLEAN=false
for arg in "$@"; do
    case "$arg" in
        --clean) FORCE_CLEAN=true ;;
        -h|--help)
            cat <<'EOF'
Использование: ./windows/scripts/build-release.sh [--clean]

  --clean    удалить windows/build и windows/install перед сборкой
EOF
            exit 0 ;;
        *) error "Неизвестный аргумент: $arg" ;;
    esac
done

if ! ls "$THIRDPARTY/wxsqlite3/install/lib/"libwxcode*.a >/dev/null 2>&1; then
    error "wxSQLite3 не найден. Запустите: windows\\bats\\setup-deps.bat"
fi

if [[ "$FORCE_CLEAN" == true ]]; then
    log "Очистка build и install..."
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
fi

mkdir -p "$BUILD_DIR" "$INSTALL_DIR"

section "1/3. CMake configure"

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

section "2/3. Сборка и установка"

cmake --build "$BUILD_DIR" -j"$(nproc)"
cmake --build "$BUILD_DIR" --target install

section "3/3. Runtime DLL"

EXE="$INSTALL_DIR/bin/iptvplayer.exe"
[[ -f "$EXE" ]] || error "Бинарник не найден: $EXE"

log "Анализ зависимостей: $EXE"
mapfile -t all_dlls < <(
    ldd "$EXE" 2>/dev/null \
        | awk '/=> \// {print $3}' \
        | grep -E "^${PREFIX}/(bin|lib)/" \
        | sort -u
)

copied=0
for src in "${all_dlls[@]}"; do
    [[ -f "$src" ]] || continue
    base="$(basename "$src")"
    if [[ ! -f "$INSTALL_DIR/bin/$base" ]]; then
        cp "$src" "$INSTALL_DIR/bin/"
        copied=$((copied + 1))
    fi
done
log "Скопировано runtime DLL: $copied"

# --- Vulkan loader ---
# libplacebo в MSYS2 UCRT64 пока линкуется с vulkan-1.dll напрямую
# (исправление в 7.360.1-3 доступно только в Cygwin).
# На Windows 10 без Vulkan Runtime этой DLL нет — кладём рядом с exe.
VULKAN_DLL="$PREFIX/bin/vulkan-1.dll"
if [[ -f "$VULKAN_DLL" ]]; then
    if [[ ! -f "$INSTALL_DIR/bin/vulkan-1.dll" ]]; then
        cp "$VULKAN_DLL" "$INSTALL_DIR/bin/"
        log "Скопирован Vulkan loader: vulkan-1.dll"
    fi
else
    error "vulkan-1.dll не найден в $PREFIX/bin — установите ${PKG_PREFIX}-vulkan-loader"
fi

# --- CA bundle для libcurl (OpenSSL) ---
# libcurl-4.dll в MSYS2 UCRT64 собран с OpenSSL и не имеет встроенного
# пути к CA-файлу. Кладём ca-bundle.crt рядом с exe — ApplyCurlCaBundle()
# найдёт его при запуске вне MSYS2-шелла.
CA_BUNDLE="/etc/ssl/certs/ca-bundle.crt"
if [[ -f "$CA_BUNDLE" ]]; then
    cp "$CA_BUNDLE" "$INSTALL_DIR/bin/ca-bundle.crt"
    log "Скопирован CA bundle: ca-bundle.crt"
else
    error "ca-bundle.crt не найден: $CA_BUNDLE. Установите: pacman -S ca-certificates"
fi

# --- Strip отладочных символов из DLL и EXE ---
# Уменьшает размер пакета на ~20-30%. Символы в релизе не нужны:
# бэктрейсы всё равно нечитаемы без .pdb-файлов.
if command -v strip >/dev/null 2>&1; then
    before_kb=$(du -sk "$INSTALL_DIR/bin" | cut -f1)
    find "$INSTALL_DIR/bin" -maxdepth 1 -type f \
        \( -name '*.dll' -o -name '*.exe' \) -print0 \
        | xargs -0 -r strip --strip-unneeded 2>/dev/null || true
    after_kb=$(du -sk "$INSTALL_DIR/bin" | cut -f1)
    log "Strip: bin/ $((before_kb / 1024))MB -> $((after_kb / 1024))MB"
else
    warn "strip не найден — пропускаю оптимизацию размера"
fi

log "Копирование ресурсов в bin/..."
for res in icons epg_sources.json matching_rules.json channel_aliases.json about.html; do
    src="$PROJECT_ROOT/resources/$res"
    [[ -e "$src" ]] || src="$PROJECT_ROOT/$res"
    if [[ -e "$src" ]]; then
        cp -r "$src" "$INSTALL_DIR/bin/" 2>/dev/null || true
    fi
done
if [[ -d "$INSTALL_DIR/share/locale" ]]; then
    cp -r "$INSTALL_DIR/share/locale" "$INSTALL_DIR/bin/"
fi

log "Проверка: $EXE --version"
"$EXE" --version || warn "--version вернул ненулевой код (не критично)"

section "Готово"
log "Установлено в: $INSTALL_DIR"
log "Запуск:        $INSTALL_DIR/bin/iptvplayer.exe"