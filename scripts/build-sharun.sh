#!/bin/bash
# =============================================================================
# build-sharun.sh – AppImage через quick-sharun
# =============================================================================
# Библиотека, не запускается напрямую. Сорсится из build-package.sh.
#
# Раскладка AppDir — каноническая: корень = эффективный /usr (bin/, lib/,
# share/). install/ уже имеет нужную форму, отдельный staging не создаётся.
# quick-sharun принимает путь к бинарнику из любого места, DESKTOP/ICON —
# через env. Данные приложения (share/iptvplayer/) копируются в APPDIR
# вручную после deploy — quick-sharun их не переносит.
#
# Переменные окружения (опционально):
#   QUICK_SHARUN_REF   git-ref (branch/tag/commit), по умолчанию "main".
#   SHARUN_LINK        URL до sharun+helper-libs-<arch>.tar.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-sharun.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"

build_sharun_appimage() {
    local appimage_file="${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}-sharun.AppImage"
    local bin_src="$PROJECT_ROOT/install/bin/$PACKAGE_NAME"
    local share_src="$PROJECT_ROOT/install/share/$PACKAGE_NAME"
    local desktop_file="$PROJECT_ROOT/install/share/applications/$PACKAGE_NAME.desktop"
    local icon_file="$PROJECT_ROOT/install/share/$PACKAGE_NAME/icons/$ICON_NAME"
    local QUICK_SHARUN="$SCRIPT_DIR/quick-sharun.sh"

    echo "[+] Сборка AppImage через quick-sharun..."

    # $APPDIR уходит в rm -rf — защищаемся от пустых и системных значений.
    : "${APPDIR:?APPDIR не задан}"
    : "${OUTPUT_DIR:?OUTPUT_DIR не задан}"
    : "${PROJECT_ROOT:?PROJECT_ROOT не задан}"
    : "${PACKAGE_NAME:?PACKAGE_NAME не задан}"
    : "${APPIMAGE_ARCH:?APPIMAGE_ARCH не задан}"
    : "${VERSION_FILE:?VERSION_FILE не задан}"

    case "$APPDIR" in
        "$PROJECT_ROOT"/*) ;;
        *)
            echo "[!] APPDIR вне PROJECT_ROOT: $APPDIR — отказ" >&2
            return 1
            ;;
    esac

    if [ ! -f "$bin_src" ]; then
        echo "[!] не найден бинарник $bin_src" >&2
        return 1
    fi
    if [ ! -d "$share_src" ]; then
        echo "[!] не найдена директория данных приложения: $share_src" >&2
        return 1
    fi
    chmod +x "$bin_src" 2>/dev/null || true

    local QUICK_SHARUN_REF="${QUICK_SHARUN_REF:-main}"
    local QUICK_SHARUN_URL="https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/${QUICK_SHARUN_REF}/useful-tools/quick-sharun.sh"

    rm -f "$QUICK_SHARUN"
    echo "[+] Скачивание quick-sharun (ref=${QUICK_SHARUN_REF})..."
    if ! download "$QUICK_SHARUN_URL" "$QUICK_SHARUN"; then
        echo "[!] не удалось скачать quick-sharun" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    chmod +x "$QUICK_SHARUN"
    # download() возвращает 0 даже если на диск лёг не скрипт (HTML-страница
    # ошибки от прокси). Проверяем shebang — bash не парсит HTML молча.
    if ! head -c 2 "$QUICK_SHARUN" | grep -q '#!'; then
        echo "[!] quick-sharun не является shell-скриптом (нет shebang)" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    # cmake install .desktop не ставит — генерируем здесь, кладём в install/,
    # чтобы DESKTOP указывал на обычный файл, а не на путь внутри APPDIR.
    write_desktop_file "$desktop_file"
    if [ ! -f "$desktop_file" ] || ! grep -q '^\[Desktop Entry\]' "$desktop_file"; then
        echo "[!] write_desktop_file не создал корректный .desktop: $desktop_file" >&2
        return 1
    fi

    rm -rf "$APPDIR"

    export APPDIR
    export ARCH="$APPIMAGE_ARCH"
    export VERSION="$VERSION_FILE"
    export OUTPATH="$OUTPUT_DIR"
    export OUTNAME="$appimage_file"
    export DESKTOP="$desktop_file"
    export ICON="$icon_file"
    export UPINFO="gh-releases-zsync|i-jurij|iptvplayer|latest|iptvplayer-linux-*-sharun.AppImage.zsync"
    export GTK_CLASS_FIX=1
    export DEPLOY_GDK=1
    export DEPLOY_OPENGL=1
    export DEPLOY_VULKAN=1
    unset ANYLINUX_DO_NOT_LOAD_LIBS 2>/dev/null || true

    # Добавляем найденную директорию loaders в аргументы quick-sharun.
    if [ "$DEPLOY_GDK" = 1 ]; then
        _gdk_loaders_dir=""
        for _d in /usr/lib/gdk-pixbuf-*/*/loaders \
                  /usr/lib64/gdk-pixbuf-*/*/loaders \
                  /usr/lib/*-linux-gnu/gdk-pixbuf-*/*/loaders; do
            [ -d "$_d" ] || continue
            if ls "$_d"/*pixbufloader*svg*.so* >/dev/null 2>&1; then
                _gdk_loaders_dir="$_d"
                break
            fi
        done

        if [ -n "$_gdk_loaders_dir" ]; then
            echo "[i] gdk-pixbuf loaders dir: $_gdk_loaders_dir"
            set -- "$@" "$_gdk_loaders_dir"
        else
            echo "[i] gdk-pixbuf loaders dir не найден — SVG через glycin или librsvg отсутствует"
        fi
    fi

    echo "[+] Развёртывание зависимостей через quick-sharun..."
    if ! "$QUICK_SHARUN" "$bin_src" "$@"; then
        echo "[!] quick-sharun (deploy) завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    # Данные приложения quick-sharun не переносит — копируем в корневой
    # share/. Отсюда их найдёт FindAppDataFile ($SHARUN_DIR/share/iptvplayer).
    echo "[+] Копирование данных приложения в $APPDIR/share/$PACKAGE_NAME..."
    mkdir -p "$APPDIR/share/$PACKAGE_NAME"
    if ! cp -a "$share_src/." "$APPDIR/share/$PACKAGE_NAME/"; then
        echo "[!] не удалось скопировать данные приложения в APPDIR" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    if [ -z "$(ls -A "$APPDIR/share/$PACKAGE_NAME" 2>/dev/null)" ]; then
        echo "[!] $APPDIR/share/$PACKAGE_NAME пуст после копирования" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    # gdk-pixbuf: SVG через .so-loader (Debian/Fedora) или через glycin (Arch).
    # Отсутствие обоих — фатально: иконки не отрисуются, приложение упадёт.
    _bundle_loader="$(find "$APPDIR" \( -type f -o -type l \) \
                      -name '*pixbufloader*svg*.so*' -print -quit 2>/dev/null || true)"
    _bundle_glycin="$(find "$APPDIR" -type f -name 'glycin-svg' -print -quit 2>/dev/null || true)"

    if [ -n "$_bundle_loader" ]; then
        echo "[+] gdk-pixbuf SVG-loader: ${_bundle_loader#"$APPDIR"}"
    elif [ -n "$_bundle_glycin" ]; then
        echo "[+] gdk-pixbuf SVG via glycin: ${_bundle_glycin#"$APPDIR"}"
    else
        echo "[!] SVG-загрузчик не найден в бандле — приложение не сможет" >&2
        echo "[!] отрисовать ни одной иконки и упадёт при старте." >&2
        echo "[!] Установите на сборочной машине один из пакетов:" >&2
        echo "[!]   Debian/Ubuntu: librsvg2-common" >&2
        echo "[!]   Fedora/RHEL:   librsvg2" >&2
        echo "[!]   Arch:          librsvg (использует glycin)" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    _bundle_cache="$(find "$APPDIR" \( -type f -o -type l \) \
                     -name 'loaders.cache' -print -quit 2>/dev/null || true)"

    if [ -z "$_bundle_cache" ] && [ -n "$_bundle_loader" ]; then
        # Fallback: .so-loader есть, но loaders.cache не доехал — генерируем
        # сами и чистим абсолютные пути (sharun резолвит имена по LD_LIBRARY_PATH).
        if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
            _loader_dir="$(dirname "$_bundle_loader")"
            _generated="${_loader_dir}/loaders.cache"
            echo "[i] loaders.cache отсутствует, генерирую из $_loader_dir..."
            if gdk-pixbuf-query-loaders "$_loader_dir"/*.so* > "$_generated" 2>/dev/null \
               && [ -s "$_generated" ] \
               && grep -q '\.so' "$_generated"; then
                sed -i \
                    -e 's|/usr/lib/.*/loaders/||g' \
                    -e "s|$_loader_dir/||g" \
                    "$_generated"
                _bundle_cache="$_generated"
                echo "[+] Сгенерирован loaders.cache: ${_bundle_cache#"$APPDIR"}"
            else
                rm -f "$_generated"
                echo "[!] не удалось сгенерировать loaders.cache" >&2
            fi
        fi
    fi

    if [ -n "$_bundle_cache" ]; then
        echo "[+] gdk-pixbuf loaders.cache: ${_bundle_cache#"$APPDIR"}"
    elif [ -n "$_bundle_glycin" ]; then
        echo "[i] loaders.cache не найден (норма для Arch, используется glycin)"
    else
        echo "[!] loaders.cache отсутствует в бандле и не был сгенерирован." >&2
        echo "[!] gdk-pixbuf не увидит ни одного загрузчика — приложение упадёт." >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    if [ -n "$_bundle_cache" ] \
       && ! grep -q '^GDK_PIXBUF_MODULE_FILE=' "$APPDIR/.env" 2>/dev/null; then
        _cache_rel="${_bundle_cache#"$APPDIR"}"
        [ -f "$APPDIR/.env" ] || : > "$APPDIR/.env"
        echo "GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}" >> "$APPDIR/.env"
        echo "[+] .env += GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}"
    elif [ -n "$_bundle_cache" ]; then
        echo "[i] GDK_PIXBUF_MODULE_FILE уже прописан в .env"
    else
        echo "[i] GDK_PIXBUF_MODULE_FILE не прописан (нет loaders.cache)"
    fi

    echo "[+] Упаковка AppDir в AppImage..."
    if ! "$QUICK_SHARUN" --make-appimage; then
        echo "[!] quick-sharun --make-appimage завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
          UPINFO GTK_CLASS_FIX DEPLOY_GDK \
          DEPLOY_OPENGL DEPLOY_VULKAN

    if [ ! -f "$OUTPUT_DIR/$appimage_file" ]; then
        echo "[!] quick-sharun не создал $appimage_file" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    echo "[✓] AppImage (sharun): $OUTPUT_DIR/$appimage_file"

    rm -f "${OUTPUT_DIR:?}/appinfo"
    rm -f "$QUICK_SHARUN"

    return 0
}