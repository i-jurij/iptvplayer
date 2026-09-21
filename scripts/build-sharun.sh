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
#     старых glibc, musl-системах (Alpine) и NixOS без FHS;
#   - бандлит GPU-стек целиком (OpenGL + Vulkan, включая loader libvulkan.so.1
#     и mesa ICD-драйверы), чтобы приложение работало даже на системах без
#     Mesa. На хостах с проприетарным NVIDIA-драйвером рендеринг уйдёт в
#     llvmpipe (software) — осознанный trade-off ради переносимости.
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
    # quick-sharun.sh — всегда качаем свежий.
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
    #    в $SHARUN_DIR/usr/share/iptvplayer/ (см. Utils.cpp).
    if [ -d "$share_src" ]; then
        mkdir -p "$APPDIR/usr/share/$PACKAGE_NAME"
        cp -a "$share_src/." "$APPDIR/usr/share/$PACKAGE_NAME/"
    else
        echo "[!] не найдена директория данных приложения: $share_src" >&2
        rm -f "$QUICK_SHARUN"
        return 1
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
    export UPINFO="gh-releases-zsync|i-jurij|iptvplayer|latest|iptvplayer-linux-*-sharun.AppImage.zsync"
    # Поправляет WM_CLASS для GTK-приложений.
    export GTK_CLASS_FIX=1
    # Форсируем deployment gdk-pixbuf (SVG-лоадеры и кэш).
    export DEPLOY_GDK=1
    # Полный бандл GPU-стека. Цель — запускаться и на системах без Mesa
    # (минимальные контейнеры, musl-дистрибутивы). quick-sharun развернёт
    # mesa-gl, mesa-vulkan, ICD-драйверы и loader libvulkan.so.1.
    # На хостах с проприетарным NVIDIA-драйвером рендеринг уйдёт в
    # llvmpipe (software) — это осознанный trade-off ради переносимости.
    export DEPLOY_OPENGL=1
    export DEPLOY_VULKAN=1
    # Явно снимаем любые ограничения на загрузку библиотек, если они
    # остались в окружении от предыдущих версий скрипта.
    unset ANYLINUX_DO_NOT_LOAD_LIBS 2>/dev/null || true

    # 1) Развёртывание зависимостей
    echo "[+] Развёртывание зависимостей через quick-sharun..."
    if ! "$QUICK_SHARUN" "$APPDIR/usr/bin/$PACKAGE_NAME"; then
        echo "[!] quick-sharun (deploy) завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi

    # =====================================================================
    # Проверка gdk-pixbuf SVG-loader.
    # =====================================================================
    #
    # quick-sharun ищет SVG-лоадер глобом по LIB_DIR (не через
    # loaders.cache), поэтому при установленном librsvg он попадёт в бандл
    # независимо от состояния кэша. А вот loaders.cache в рантайме
    # обязателен: без него gdk-pixbuf не увидит НИ ОДНОГО лоадера, даже
    # если .so лежат рядом. quick-sharun копирует кэш из
    # $LIB_DIR/gdk-pixbuf-*/*/loaders.cache, который генерируется
    # post-install-скриптом pacman. В Docker эти скрипты часто не
    # отрабатывают — в Containerfile мы явно вызываем
    # `gdk-pixbuf-query-loaders --update-cache`.
    # =====================================================================
    _bundle_loader="$(find "$APPDIR" -type f \
                      -name 'libpixbufloader*svg*.so*' -print -quit 2>/dev/null || true)"

    if [ -z "$_bundle_loader" ]; then
        echo "[!] quick-sharun не выложил gdk-pixbuf SVG-loader в бандл." >&2
        echo "[!] Установите на сборочной машине пакет с SVG-лоадером:" >&2
        echo "[!]   Debian/Ubuntu: librsvg2-common" >&2
        echo "[!]   Fedora/RHEL:   librsvg2" >&2
        echo "[!]   Arch:          librsvg" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    echo "[+] gdk-pixbuf SVG-loader: ${_bundle_loader#"$APPDIR"}"

    _bundle_cache="$(find "$APPDIR" -type f -name 'loaders.cache' -print -quit 2>/dev/null || true)"

    if [ -z "$_bundle_cache" ]; then
        # Fallback: собрать кэш самим из уже развёрнутых лоадеров.
        # Страховка на случай, если в образе забыли прогнать
        # gdk-pixbuf-query-loaders --update-cache.
        if command -v gdk-pixbuf-query-loaders >/dev/null 2>&1; then
            _loader_dir="$(dirname "$_bundle_loader")"
            _generated="${_loader_dir}/loaders.cache"
            echo "[i] loaders.cache отсутствует, генерирую из $_loader_dir..."
            if gdk-pixbuf-query-loaders "$_loader_dir"/*.so* > "$_generated" 2>/dev/null \
               && [ -s "$_generated" ]; then
                # Убираем абсолютные пути — sharun резолвит голые имена
                # через LD_LIBRARY_PATH (та же логика, что у quick-sharun).
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

    if [ -z "$_bundle_cache" ]; then
        echo "[!] loaders.cache отсутствует в бандле и не удалось его создать." >&2
        echo "[!] Убедитесь, что в сборочном образе выполняется:" >&2
        echo "[!]   gdk-pixbuf-query-loaders --update-cache" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    echo "[+] gdk-pixbuf loaders.cache: ${_bundle_cache#"$APPDIR"}"

    # Прописать GDK_PIXBUF_MODULE_FILE, если quick-sharun сам не сделал.
    # Голое имя loader'а в кэше (после sed от quick-sharun, стр. ~4629)
    # резолвится через LD_LIBRARY_PATH, поэтому достаточно указать путь
    # к самому кэшу.
    if ! grep -q '^GDK_PIXBUF_MODULE_FILE=' "$APPDIR/.env" 2>/dev/null; then
        _cache_rel="${_bundle_cache#"$APPDIR"}"
        [ -f "$APPDIR/.env" ] || : > "$APPDIR/.env"
        echo "GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}" >> "$APPDIR/.env"
        echo "[+] .env += GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_cache_rel}"
    else
        echo "[i] GDK_PIXBUF_MODULE_FILE уже прописан в .env"
    fi

    # GPU-стек НЕ удаляем — quick-sharun развернул его по DEPLOY_OPENGL/DEPLOY_VULKAN,
    # и AppImage должен его нести целиком.

    # 2) Упаковка AppDir → AppImage (внутри вызывается appimagetool)
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

    # .zsync для sharun-варианта генерирует сам quick-sharun

    rm -f "${OUTPUT_DIR:?}/appinfo"
    rm -f "$QUICK_SHARUN"

    return 0
}