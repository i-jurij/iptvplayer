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

    if [ ! -x "$bin_src" ]; then
        echo "[!] build_sharun_appimage: не найден бинарник $bin_src" >&2
        return 1
    fi

    # quick-sharun (один раз)
    local QUICK_SHARUN="$SCRIPT_DIR/quick-sharun.sh"
    if [ ! -f "$QUICK_SHARUN" ]; then
        echo "[+] Скачивание quick-sharun..."
        if ! wget -q --show-progress \
            "https://github.com/pkgforge-dev/Anylinux-AppImages/raw/main/useful-tools/quick-sharun.sh" \
            -O "$QUICK_SHARUN"; then
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

    # 2. Все данные приложения — зеркалом, как в install/
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

    # 5. Иконки UI — приложение ищет их рядом с бинарником (см. warning'и
    #    "SvgIcon: NOT FOUND .../usr/bin/icons/*.svg" в логах).
    if [ -d "$share_src/icons" ]; then
        mkdir -p "$APPDIR/usr/bin/icons"
        cp -a "$share_src/icons/." "$APPDIR/usr/bin/icons/"
    fi

    # Переменные quick-sharun.
    export ARCH="$APPIMAGE_ARCH"
    export VERSION="$VERSION_FILE"
    export OUTPATH="$OUTPUT_DIR"
    export OUTNAME="$appimage_file"
    export ICON="$APPDIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
    export DESKTOP="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"
        export UPDATE_INFORMATION="gh-releases-zsync|i-jurij|iptvplayer|latest|iptvplayer-linux-*-sharun.AppImage.zsync"
    # Поправляет WM_CLASS для GTK-приложений.
    export GTK_CLASS_FIX=1

    # 1) Развёртывание зависимостей
    echo "[+] Развёртывание зависимостей через quick-sharun..."
    if ! "$QUICK_SHARUN" "$APPDIR/usr/bin/$PACKAGE_NAME"; then
        echo "[!] quick-sharun (deploy) завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP GTK_CLASS_FIX
        return 1
    fi

    # 2) Упаковка AppDir → AppImage (внутри вызывается appimagetool)
    echo "[+] Упаковка AppDir в AppImage..."
    if ! "$QUICK_SHARUN" --make-appimage; then
        echo "[!] quick-sharun --make-appimage завершился с ошибкой" >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP GTK_CLASS_FIX
        return 1
    fi

    unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP GTK_CLASS_FIX

    if [ ! -f "$OUTPUT_DIR/$appimage_file" ]; then
        echo "[!] quick-sharun не создал $appimage_file" >&2
        return 1
    fi

    echo "[✓] AppImage (sharun): $OUTPUT_DIR/$appimage_file"

    if command -v zsyncmake >/dev/null; then
        zsyncmake "$OUTPUT_DIR/$appimage_file" \
            -o "$OUTPUT_DIR/$(basename "$appimage_file" .AppImage).zsync" || true
    fi
    return 0
}