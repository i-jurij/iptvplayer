#!/bin/bash
# =============================================================================
# build-appimage.sh – AppImage (linuxdeploy) через appimagetool
# =============================================================================
#
# Библиотека, не запускается напрямую. Сорсится из build-package.sh.
#
# Зависит от scripts/common.sh (log/warn/error, prepare_staging) и от
# следующих переменных окружения, устанавливаемых в build-package.sh:
#   PROJECT_ROOT, SCRIPT_DIR
#   STAGING_DIR, APPDIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME
#   APPIMAGE_ARCH
#   VERSION, VERSION_FILE
#
# Политика GPU-стека: linuxdeploy-AppImage использует СИСТЕМНЫЙ GPU-стек
# (libEGL, libOpenGL, libGLX, libvulkan, libdrm, libgbm). Это соответствует
# excludelist самого linuxdeploy и является стандартной практикой: на хосте
# с проприетарным NVIDIA-драйвером бандленный mesa не сможет использовать
# реальный GPU. Для максимальной переносимости — используйте sharun-вариант
# (см. build-sharun.sh), он бандлит GPU-стек целиком.
#
# Функции:
#   populate_appdir  — наполняет $APPDIR через linuxdeploy + GTK-плагин
#   build_appimage   — упаковывает $APPDIR в .AppImage через appimagetool
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-appimage.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"

# =============================================================================
#                         НАПОЛНЕНИЕ APPDIR (linuxdeploy)
# =============================================================================
populate_appdir() {
    if [[ "$APPIMAGE_ARCH" != "x86_64" && "$APPIMAGE_ARCH" != "aarch64" ]]; then
        echo "[!] AppImage не поддерживается для '$APPIMAGE_ARCH'."
        return 1
    fi

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/scalable/apps"

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] populate_appdir: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    cp "$STAGING_DIR/usr/bin/$PACKAGE_NAME" "$APPDIR/usr/bin/"
    cp -r "$STAGING_DIR/usr/share/$PACKAGE_NAME" "$APPDIR/usr/share/"
    cp "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" "$APPDIR/usr/share/applications/"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" "$APPDIR/$ICON_NAME"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

    if [ -f "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$APPDIR/usr/share/metainfo"
        cp "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" "$APPDIR/usr/share/metainfo/"
    fi

    local LINUXDEPLOY="$SCRIPT_DIR/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    local GTK_PLUGIN="$SCRIPT_DIR/linuxdeploy-plugin-gtk.sh"

    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "[+] Скачивание linuxdeploy ($APPIMAGE_ARCH)..."
        if ! download \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" \
            "$LINUXDEPLOY"; then
            echo "[!] не удалось скачать linuxdeploy" >&2
            return 1
        fi
        chmod +x "$LINUXDEPLOY"
    fi
    if [ ! -f "$GTK_PLUGIN" ]; then
        echo "[+] Скачивание GTK-плагина (скрипт)..."
        if ! download \
            "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
            "$GTK_PLUGIN"; then
            echo "[!] не удалось скачать GTK-плагин" >&2
            return 1
        fi
        chmod +x "$GTK_PLUGIN"
    fi

    echo "[+] Наполнение AppDir через linuxdeploy..."
    if ! APPIMAGE_EXTRACT_AND_RUN=1 DEPLOY_GTK_VERSION=3 ARCH="$APPIMAGE_ARCH" "$LINUXDEPLOY" \
        --appdir="$APPDIR" \
        --plugin gtk \
        --desktop-file="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"; then
        echo "[!] Ошибка linuxdeploy." >&2
        return 1
    fi

    chmod +x "$APPDIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # Fallback-библиотеки: все NEEDED, которых нет в системе, кладём в
    # usr/lib/fallback. AppRun подключит их к LD_LIBRARY_PATH, только
    # если в системе их нет. FORBIDDEN_RE — то, что бандлить нельзя
    # (GPU-драйверы, DRM, X11/Wayland-сервер, libc и её спутники).
    #
    # libvulkan здесь сознательно: linuxdeploy-AppImage придерживается
    # политики «GPU-стек системный», как и сам linuxdeploy в своём
    # excludelist. Для self-contained варианта см. build-sharun.sh.
    local FORBIDDEN_RE='^(libEGL|libGLX|libGLdispatch|libOpenGL|libGLES|libGL\.|libGLU|libglapi|libvulkan|libdrm|libgbm|libva|libvdpau|libdisplay-info|libX11|libxcb|libwayland|libc\.so|ld-linux|libm\.so|libpthread|libdl\.so|librt\.so|libutil\.so|libresolv|libnss_|libgcc_s|libstdc\+\+|libz\.so)'

    echo "[+] Сбор NEEDED-списка из бинарника и библиотек в APPDIR..."
    local needed
    needed=$(
        {
            readelf -d "$APPDIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true
            find "$APPDIR/usr/lib" -maxdepth 2 \( -name '*.so' -o -name '*.so.*' \) -type f 2>/dev/null \
                -exec readelf -d {} \; 2>/dev/null || true
        } \
        | awk '/NEEDED/ {gsub(/[][]/,""); print $NF}' \
        | sort -u
    )

    mkdir -p "$APPDIR/usr/lib/fallback"
    local fb_count=0
    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        echo "$lib" | grep -qE "$FORBIDDEN_RE" && continue
        [ -e "$APPDIR/usr/lib/$lib" ] && continue
        local src
        src=$(ldconfig -p 2>/dev/null | awk -v L="$lib" '$1==L {print $NF; exit}')
        if [ -z "$src" ] || [ ! -f "$src" ]; then
            continue
        fi
        local real realbase
        real=$(readlink -f "$src")
        realbase=$(basename "$real")

        # Копируем реальный файл. Если он уже лежит в fallback — пропускаем.
        if [ ! -e "$APPDIR/usr/lib/fallback/$realbase" ]; then
            cp -a "$real" "$APPDIR/usr/lib/fallback/$realbase"
        fi

        # Симлинк нужен только если NEEDED-имя отличается от имени
        # реального файла. Иначе ln -sf затрёт скопированный файл
        # симлинком на самого себя — и упаковщики (.deb/.rpm/AppImage)
        # сломаются на "Too many levels of symbolic links".
        if [ "$lib" != "$realbase" ]; then
            ln -sf "$realbase" "$APPDIR/usr/lib/fallback/$lib"
        fi

        fb_count=$((fb_count + 1))
    done <<< "$needed"
    echo "[i] Fallback-библиотек скопировано: $fb_count"

    rm -f "$APPDIR/AppRun" "$APPDIR/AppRun.wrapped" "$APPDIR/AppRun.linuxdeploy"

    cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/bash
HERE="$(dirname "$(readlink -f "$0")")"
export APPDIR="${APPDIR:-$HERE}"

# Основной LD_LIBRARY_PATH — только linuxdeploy-деплой. Fallback-каталог
# НЕ добавляем: он подключается через FB_TMP ниже, и только теми файлами,
# которых нет в системе. Иначе бандленный fallback всегда перебивал бы
# системные библиотеки.
export LD_LIBRARY_PATH="$APPDIR/usr/lib:${LD_LIBRARY_PATH:-}"

# Хук linuxdeploy-plugin-gtk.sh жёстко ставит GDK_BACKEND=x11 и
# GTK_THEME=Adwaita:light|dark. Запоминаем пользовательские значения,
# чтобы восстановить их после source и/или доуточнить.
_iptv_saved_gdk_backend="${GDK_BACKEND:-}"
_iptv_saved_gtk_theme="${GTK_THEME:-}"

if [ -f "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh" ]; then
    source "$APPDIR/apprun-hooks/linuxdeploy-plugin-gtk.sh"
fi

# GDK_BACKEND: пользовательский выбор перебивает дефолт хука.
if [ -n "$_iptv_saved_gdk_backend" ]; then
    export GDK_BACKEND="$_iptv_saved_gdk_backend"
else
    unset GDK_BACKEND
fi
unset _iptv_saved_gdk_backend

# GTK_THEME: 1) явный GTK_THEME пользователя; 2) системная тема, если у неё
# есть gtk-3.0/gtk.css; 3) baseline Adwaita:light|dark от хука.
if [ -n "$_iptv_saved_gtk_theme" ]; then
    export GTK_THEME="$_iptv_saved_gtk_theme"
else
    _iptv_sys_theme=""

    if [ -r "$HOME/.config/gtk-3.0/settings.ini" ]; then
        _iptv_sys_theme=$(sed -n \
            's/^[[:space:]]*gtk-theme-name[[:space:]]*=[[:space:]]*//p' \
            "$HOME/.config/gtk-3.0/settings.ini" | head -n1 | tr -d '\r')
    fi

    if [ -z "$_iptv_sys_theme" ]; then
        _iptv_sys_theme=$(GSETTINGS_SCHEMA_DIR="$APPDIR/usr/share/glib-2.0/schemas" \
                          gsettings get org.gnome.desktop.interface gtk-theme \
                          2>/dev/null | tr -d "'" | tr -d '\r')
    fi

    if [ -n "$_iptv_sys_theme" ] && [ "$_iptv_sys_theme" != "Adwaita" ]; then
        for _iptv_dir in /usr/share /usr/local/share \
                         "$HOME/.local/share" "$HOME/.themes"; do
            if [ -f "$_iptv_dir/themes/$_iptv_sys_theme/gtk-3.0/gtk.css" ]; then
                export GTK_THEME="$_iptv_sys_theme"
                break
            fi
        done
    fi
fi
unset _iptv_saved_gtk_theme _iptv_sys_theme _iptv_dir

# Fallback: подключаем к LD_LIBRARY_PATH только те бандленные библиотеки,
# которых нет в системе. Если библиотека есть в системе — используем
# системную, бандленная остаётся нетронутой на диске.
if [ -d "$APPDIR/usr/lib/fallback" ]; then
    FB_TMP=$(mktemp -d -t iptvplayer-fb.XXXXXX) || FB_TMP=""
    if [ -n "$FB_TMP" ]; then
        fb_needed=0
        for lib in "$APPDIR/usr/lib/fallback"/*.so*; do
            [ -e "$lib" ] || continue
            libname=$(basename "$lib")
            # Точное совпадение по столбцу, иначе libfoo.so.1 матчит libfoo.so.10.
            if ! ldconfig -p 2>/dev/null | grep -qE "[[:space:]]${libname}([[:space:]]|$)"; then
                ln -sf "$lib" "$FB_TMP/$libname"
                fb_needed=1
            fi
        done
        if [ "$fb_needed" = 1 ]; then
            export LD_LIBRARY_PATH="$FB_TMP:${LD_LIBRARY_PATH:-}"
            trap 'rm -rf "$FB_TMP"' EXIT
        else
            rm -rf "$FB_TMP"
        fi
    fi
fi

BIN="$APPDIR/usr/bin/iptvplayer"
if [ -x "$BIN" ]; then
    # Проверяем «найдётся ли всё вообще», включая fallback-каталог.
    # Это не то же самое, что фактический LD_LIBRARY_PATH, — просто
    # гарантия, что у нас есть чем закрыть пропуски.
    missing=$(LD_LIBRARY_PATH="$APPDIR/usr/lib:$APPDIR/usr/lib/fallback:${LD_LIBRARY_PATH:-}" \
              ldd "$BIN" 2>&1 | awk '/not found/ {print $1}' | sort -u)
    if [ -n "$missing" ]; then
        echo "iptvplayer: не удалось запустить — отсутствуют системные библиотеки:" >&2
        echo "$missing" | while read -r lib; do
            echo "  - $lib" >&2
        done
        echo "" >&2
        echo "Установите их средствами вашего дистрибутива." >&2
        exit 1
    fi
fi

exec "$APPDIR/usr/bin/iptvplayer" "$@"
APPRUN
    chmod +x "$APPDIR/AppRun"

    echo "[✓] AppDir наполнен."
    return 0
}

# =============================================================================
#                              APPIMAGE
# =============================================================================
build_appimage() {
    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}.AppImage"

    echo "[+] Создание AppImage ($APPIMAGE_ARCH)..."

    if [ ! -d "$APPDIR/usr/lib" ]; then
        if ! populate_appdir; then
            return 1
        fi
    fi

    if [ ! -f "$APPDIR/AppRun" ]; then
        echo "[!] $APPDIR/AppRun отсутствует — populate_appdir не отработал" >&2
        return 1
    fi
    if ! grep -q 'iptvplayer-fb' "$APPDIR/AppRun"; then
        echo "[!] $APPDIR/AppRun не содержит нашей fallback-логики — отказ" >&2
        return 1
    fi

    if [ ! -e "$APPDIR/.DirIcon" ] && [ -f "$APPDIR/$ICON_NAME" ]; then
        ln -sf "$ICON_NAME" "$APPDIR/.DirIcon"
    fi

    local APPIMAGETOOL="$SCRIPT_DIR/appimagetool-${APPIMAGE_ARCH}.AppImage"
    if [ ! -f "$APPIMAGETOOL" ]; then
        echo "[+] Скачивание appimagetool ($APPIMAGE_ARCH)..."
        if ! download \
            "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${APPIMAGE_ARCH}.AppImage" \
            "$APPIMAGETOOL"; then
            echo "[!] не удалось скачать appimagetool" >&2
            return 1
        fi
        chmod +x "$APPIMAGETOOL"
    fi

    echo "[+] Упаковка AppDir в AppImage через appimagetool..."
    rm -f "$appimage_file"
    if ! APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$APPIMAGE_ARCH" "$APPIMAGETOOL" \
        --no-appstream \
        "$APPDIR" "$appimage_file"; then
        echo "[!] appimagetool завершился с ошибкой" >&2
        return 1
    fi

    if [ ! -f "$appimage_file" ]; then
        echo "[!] appimagetool не создал $appimage_file" >&2
        return 1
    fi

    echo "[✓] AppImage: $appimage_file"

    if command -v zsyncmake >/dev/null; then
        if ! zsyncmake "$appimage_file" -o "$OUTPUT_DIR/$(basename "$appimage_file" .AppImage).zsync"; then
            echo "[!] zsyncmake упал — .zsync не сгенерирован" >&2
        fi
    fi

    rm -f "${OUTPUT_DIR:?}/appinfo"

    return 0
}