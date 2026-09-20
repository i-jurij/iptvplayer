#!/bin/bash
# =============================================================================
# build-sharun.sh – AppImage через quick-sharun (максимальная переносимость)
# =============================================================================
#
# Библиотека, не запускается напрямую. Сорсится из build-package.sh.
#
# Отличие от build_appimage() (linuxdeploy + appimagetool):
#   - работает с уже собранным бинарником из install/, staging не нужен;
#   - quick-sharun сам сканирует зависимости (ldd + strace, включая dlopen);
#   - встраивает собственный ld-linux/musl, поэтому AppImage запускается на
#     старых glibc, musl-системах (Alpine) и NixOS без FHS.
#
# Требует установленных переменных (выставляются в build-package.sh):
#   PROJECT_ROOT, SCRIPT_DIR, APPDIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, APPIMAGE_ARCH
#   VERSION, VERSION_FILE
#
# Переменные окружения (опционально):
#   QUICK_SHARUN_REF   git-ref (branch/tag/commit) для quick-sharun.sh.
#                      По умолчанию "main". Позволяет зафиксировать
#                      рабочую версию, если upstream сломает main.
#   SHARUN_LINK        полный URL до sharun+helper-libs-<arch>.tar.
#                      Пробрасывается в quick-sharun.sh как есть.
#
# Функции:
#   build_sharun_appimage — собирает AppImage через quick-sharun
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-sharun.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"

# =============================================================================
#                         APPIMAGE (quick-sharun)
# =============================================================================
build_sharun_appimage() {
    local appimage_file="${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}-sharun.AppImage"
    local bin_src="$PROJECT_ROOT/install/bin/$PACKAGE_NAME"
    local share_src="$PROJECT_ROOT/install/share/$PACKAGE_NAME"
    local QUICK_SHARUN="$SCRIPT_DIR/quick-sharun.sh"

    echo "[+] Сборка AppImage через quick-sharun..."

    if [ ! -f "$bin_src" ]; then
        echo "[!] build_sharun_appimage: не найден бинарник $bin_src" >&2
        return 1
    fi
    # После artifact upload/download exec-бит может не сохраниться.
    chmod +x "$bin_src" 2>/dev/null || true

    # -------------------------------------------------------------------------
    # quick-sharun.sh — всегда качаем свежий.    #
    # Пин версии на случай сбоя upstream:
    #   QUICK_SHARUN_REF=<commit-sha>  (branch/tag/commit)
    # Пин версии самого sharun — переменной окружения SHARUN_LINK
    # (её читает quick-sharun.sh: SHARUN_LINK=${SHARUN_LINK:-...}).
    # -------------------------------------------------------------------------
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

    # Свой минимальный AppDir. Ничего общего со staging не имеет:
    # quick-sharun сам разложит библиотеки, создаст AppRun, выставит
    # переменные окружения (XDG_DATA_DIRS и т.д.).
    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" \
             "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/scalable/apps"

    # 1. Бинарник
    cp "$bin_src" "$APPDIR/usr/bin/$PACKAGE_NAME"

    # 2. Все данные приложения — зеркалом, как в install/.
    #    Сюда же попадают UI-иконки: share/iptvplayer/icons/*.svg
    #    → $APPDIR/usr/share/iptvplayer/icons/.
    #    Приложение находит их через FindAppDataFile(), которая ищет
    #    в $APPDIR/usr/share/iptvplayer/ (см. Utils.cpp).
    if [ -d "$share_src" ]; then
        mkdir -p "$APPDIR/usr/share/$PACKAGE_NAME"
        cp -a "$share_src/." "$APPDIR/usr/share/$PACKAGE_NAME/"
    fi

    # 3. .desktop для рабочего стола
    write_desktop_file "$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"

    # 4. Иконка рабочего стола
    if [ -f "$share_src/icons/$ICON_NAME" ]; then
        cp "$share_src/icons/$ICON_NAME" \
           "$APPDIR/usr/share/icons/hicolor/scalable/apps/"
    fi

    # Переменные quick-sharun.
    export APPDIR
    export ARCH="$APPIMAGE_ARCH"
    export VERSION="$VERSION_FILE"
    export OUTPATH="$OUTPUT_DIR"
    export OUTNAME="$appimage_file"
    export ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
    export DESKTOP="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"
    export UPDATE_INFORMATION="gh-releases-zsync|i-jurij|iptvplayer|latest|iptvplayer-linux-*-sharun.AppImage.zsync"
    # Поправляет WM_CLASS для GTK-приложений.
    export GTK_CLASS_FIX=1
    # Форсируем deployment gdk-pixbuf (SVG-лоадеры и кэш).
    # Авто-детект по NEEDED может промахнуться: libgdk_pixbuf — не прямая
    # зависимость бинарника, а транзитивная через libgtk. Без лоадеров
    # GTK падает в assert при рендере SVG-иконок.
    export DEPLOY_GDK=1

    # 1) Развёртывание зависимостей
    echo "[+] Развёртывание зависимостей через quick-sharun..."
    if ! "$QUICK_SHARUN" "$APPDIR/usr/bin/$PACKAGE_NAME"; then
        echo "[!] quick-sharun (deploy) завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPDATE_INFORMATION GTK_CLASS_FIX DEPLOY_GDK
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    # =====================================================================
    # Постобработка gdk-pixbuf.
    #
    # quick-sharun при DEPLOY_GDK=1 копирует libpixbufloader-*.so и
    # librsvg в бандл, но НЕ создаёт loaders.cache и НЕ прописывает
    # GDK_PIXBUF_MODULE_FILE. Без этого GTK берёт хостовый кэш, тот
    # ссылается на хостовый загрузчик, а хостовый загрузчик dlopen'ит
    # бандленный librsvg (LD_LIBRARY_PATH от sharun подсовывает бандл
    # первым) и валится на несовпадении ABI:
    #   undefined symbol: rsvg_handle_get_pixbuf_and_error
    #
    # Решение: сгенерировать бандленный loaders.cache, поправить пути
    # на относительные, прописать GDK_PIXBUF_MODULE_FILE=${SHARUN_DIR}/...
    # =====================================================================

    # 1. Найти директорию с бандленными загрузчиками.
    _loaders_dir=""
    if [ -d "$APPDIR/lib" ]; then
        _loaders_dir="$(find "$APPDIR/lib" -type d \
                        -path '*gdk-pixbuf*/loaders' -print -quit 2>/dev/null || true)"
    fi

    if [ -z "$_loaders_dir" ]; then
        echo "[i] gdk-pixbuf loaders в \$APPDIR/lib не найдены —" \
             "кэш не создаём, GTK будет использовать хостовый"
    else
        echo "[+] Директория gdk-pixbuf loaders: $_loaders_dir"
        _cache_path="${_loaders_dir%/loaders}/loaders.cache"

        # 2. Найти gdk-pixbuf-query-loaders в системе сборки.
        _query_tool=""
        for _t in gdk-pixbuf-query-loaders-64 \
                  gdk-pixbuf-query-loaders \
                  gdk-pixbuf-query-loaders-32; do
            if command -v "$_t" >/dev/null 2>&1; then
                _query_tool="$_t"
                break
            fi
        done

        # 3a. Сгенерировать кэш через утилиту.
        #     cd в _loaders_dir, чтобы вывод содержал имена .so без пути.
        #     LD_LIBRARY_PATH=$APPDIR/lib нужен, если загрузчики линкуются
        #     с бандленными библиотеками, которые ещё не в системном пути.
        if [ -n "$_query_tool" ]; then
            echo "[+] Генерация loaders.cache через $_query_tool"
            (
                cd "$_loaders_dir" || exit 1
                LD_LIBRARY_PATH="$APPDIR/lib:$APPDIR/lib/fallback:${LD_LIBRARY_PATH:-}" \
                    "$_query_tool" ./*.so* > "$_cache_path" 2>/dev/null
            ) || true

            if [ -s "$_cache_path" ]; then
                echo "[+] loaders.cache сгенерирован ($(wc -l < "$_cache_path") строк)"
            else
                echo "[!] Не удалось сгенерировать loaders.cache"
                rm -f "$_cache_path"
            fi
        else
            echo "[i] gdk-pixbuf-query-loaders не найден в PATH"
        fi

        # 3b. Fallback: если утилиты нет или она дала пустой результат —
        #     копируем хостовый кэш. Пути потом всё равно правим на
        #     относительные, так что имена .so совпадут с бандленными.
        if [ ! -s "$_cache_path" ]; then
            _host_cache=""
            for _c in /usr/lib/*/gdk-pixbuf-*/*/loaders.cache \
                      /usr/lib64/gdk-pixbuf-*/*/loaders.cache \
                      /usr/lib/gdk-pixbuf-*/*/loaders.cache; do
                if [ -f "$_c" ]; then
                    _host_cache="$_c"
                    break
                fi
            done
            if [ -n "$_host_cache" ]; then
                echo "[+] Копирование хостового кэша: $_host_cache"
                cp "$_host_cache" "$_cache_path"
            else
                echo "[!] Хостовый loaders.cache тоже не найден —" \
                     "SVG-загрузчик в GTK работать не будет"
            fi
        fi

        # 4. Пути в кэше → относительные.
        if [ -s "$_cache_path" ]; then
            if grep -qE '/usr/(lib|lib64)/[^"]*/loaders/' "$_cache_path" 2>/dev/null; then
                sed -i -E 's|/usr/(lib|lib64)/[^"]*/loaders/||g' "$_cache_path"
                echo "[+] loaders.cache: пути приведены к относительным"
            fi

            if ! grep -q 'svg' "$_cache_path"; then
                echo "[!] В loaders.cache нет записи svg —" \
                     "SVG-иконки GTK работать не будут"
            fi

            # 5. Прописать GDK_PIXBUF_MODULE_FILE в .env.
            _cache_rel="${_cache_path#"$APPDIR"}"

            if [ ! -f "$APPDIR/.env" ]; then
                : > "$APPDIR/.env"
            fi

            if ! grep -q '^GDK_PIXBUF_MODULE_FILE=' "$APPDIR/.env"; then
                echo "GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}" \
                    >> "$APPDIR/.env"
                echo "[+] .env += GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}"
            else
                echo "[i] GDK_PIXBUF_MODULE_FILE уже прописан в .env"
            fi
        fi
    fi

    # 2) Упаковка AppDir → AppImage (внутри вызывается appimagetool)
    echo "[+] Упаковка AppDir в AppImage..."
    if ! "$QUICK_SHARUN" --make-appimage; then
        echo "[!] quick-sharun --make-appimage завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPDATE_INFORMATION GTK_CLASS_FIX DEPLOY_GDK
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
          UPDATE_INFORMATION GTK_CLASS_FIX DEPLOY_GDK

    if [ ! -f "$OUTPUT_DIR/$appimage_file" ]; then
        echo "[!] quick-sharun не создал $appimage_file" >&2
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    echo "[✓] AppImage (sharun): $OUTPUT_DIR/$appimage_file"

    if command -v zsyncmake >/dev/null; then
        zsyncmake "$OUTPUT_DIR/$appimage_file" \
            -o "$OUTPUT_DIR/$(basename "$appimage_file" .AppImage).zsync" || true
    fi

    rm -f "${OUTPUT_DIR:?}/appinfo"
    rm -f "$QUICK_SHARUN"

    return 0
}