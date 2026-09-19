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
#   - отдельный appimagetool не нужен: quick-sharun вызывает его сам
#     при --make-appimage;
#   - результат называется с суффиксом "-sharun", чтобы не конфликтовать
#     с linuxdeploy-вариантом.
#
# Требует установленных переменных (выставляются в build-package.sh):
#   PROJECT_ROOT, SCRIPT_DIR, APPDIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, APPIMAGE_ARCH
#   VERSION, VERSION_FILE
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

    echo "[+] Сборка AppImage через quick-sharun..."

    if [ ! -f "$bin_src" ]; then
        echo "[!] build_sharun_appimage: не найден бинарник $bin_src" >&2
        return 1
    fi
    # После artifact upload/download exec-бит может не сохраниться.
    chmod +x "$bin_src" 2>/dev/null || true

    # quick-sharun (один раз)
    local QUICK_SHARUN="$SCRIPT_DIR/quick-sharun.sh"
    if [ ! -f "$QUICK_SHARUN" ]; then
        echo "[+] Скачивание quick-sharun..."
        if ! download \
            "https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/raw/main/useful-tools/quick-sharun.sh" \
            "$QUICK_SHARUN"; then
            echo "[!] не удалось скачать quick-sharun" >&2
            return 1
        fi
        chmod +x "$QUICK_SHARUN"
    fi

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
    #    Приложение находит их через FindResourceFile(), которая ищет
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
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP GTK_CLASS_FIX DEPLOY_GDK
        return 1
    fi

    # =====================================================================
    # Постобработка gdk-pixbuf.
    #
    # gdk-pixbuf ищет loaders.cache в порядке:
    #   1) $GDK_PIXBUF_MODULE_FILE
    #   2) вкомпилированный дефолт (у нас — хостовый
    #      /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0/2.10.0/loaders.cache)
    #
    # Без (1) GTK грузит ХОСТОВЫЕ pixbuf-лоадеры. Хостовый SVG-лоадер
    # dlopen'ит бандленный librsvg (потому что LD_LIBRARY_PATH от sharun
    # подсовывает бандл первым) и валится на несовпадении символов:
    #   undefined symbol: rsvg_handle_get_pixbuf_and_error
    # Это не warning, а abort() внутри GTK — приложение падает с "Bail out!".
    #
    # Лечится тем, что переменная GDK_PIXBUF_MODULE_FILE указывает на
    # бандленный loaders.cache, а сам кэш в бандле правится так, чтобы пути
    # внутри были относительными (иначе лоадеры всё равно резолвятся
    # в хостовые .so).
    # =====================================================================
    _gdkpixbuf_cache=""
    if [ -d "$APPDIR/lib" ]; then
        _gdkpixbuf_cache="$(find "$APPDIR/lib" -maxdepth 5 -type f \
                            -name 'loaders.cache' -print -quit 2>/dev/null || true)"
    fi

    if [ -z "$_gdkpixbuf_cache" ]; then
        echo "[!] loaders.cache не найден в \$APPDIR/lib —" \
             "SVG-иконки GTK работать не будут" >&2
    else
        echo "[+] Найден gdk-pixbuf кэш: $_gdkpixbuf_cache"

        # Патчим абсолютные пути на относительные. Idempotent: после
        # первого прогона вхождений /usr/lib/.../loaders/ уже не остаётся,
        # повторный вызов grep -q ничего не найдёт и sed не сработает.
        if grep -q '/usr/lib' "$_gdkpixbuf_cache" 2>/dev/null; then
            sed -i -e 's|/usr/lib/.*/loaders/||g' "$_gdkpixbuf_cache"
            echo "[+] loaders.cache: абсолютные пути заменены относительными"
        fi

        if ! grep -q 'svg' "$_gdkpixbuf_cache"; then
            echo "[!] В loaders.cache нет записи для SVG-лоадера —" \
                 "проверьте, что DEPLOY_GDK отработал" >&2
        fi

        # Путь относительно корня AppDir, для подстановки через ${SHARUN_DIR}.
        # ${SHARUN_DIR} разворачивается в runtime sharun'ом и указывает
        # на корень смонтированного AppImage.
        _gdkpixbuf_cache_rel="${_gdkpixbuf_cache#"$APPDIR"}"

        if [ ! -f "$APPDIR/.env" ]; then
            : > "$APPDIR/.env"
        fi

        if ! grep -q '^GDK_PIXBUF_MODULE_FILE=' "$APPDIR/.env"; then
            echo "GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_gdkpixbuf_cache_rel}" \
                >> "$APPDIR/.env"
            echo "[+] .env += GDK_PIXBUF_MODULE_FILE=\${SHARUN_DIR}${_gdkpixbuf_cache_rel}"
        else
            echo "[i] GDK_PIXBUF_MODULE_FILE уже прописан в .env — оставляем как есть"
        fi
    fi

    # 2) Упаковка AppDir → AppImage (внутри вызывается appimagetool)
    echo "[+] Упаковка AppDir в AppImage..."
    if ! "$QUICK_SHARUN" --make-appimage; then
        echo "[!] quick-sharun --make-appimage завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP GTK_CLASS_FIX DEPLOY_GDK
        return 1
    fi

    unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP GTK_CLASS_FIX DEPLOY_GDK

    if [ ! -f "$OUTPUT_DIR/$appimage_file" ]; then
        echo "[!] quick-sharun не создал $appimage_file" >&2
        return 1
    fi

    echo "[✓] AppImage (sharun): $OUTPUT_DIR/$appimage_file"

    if command -v zsyncmake >/dev/null; then
        zsyncmake "$OUTPUT_DIR/$appimage_file" \
            -o "$OUTPUT_DIR/$(basename "$appimage_file" .AppImage).zsync" || true
    fi

    rm -f "${OUTPUT_DIR:?}/appinfo"

    return 0
}