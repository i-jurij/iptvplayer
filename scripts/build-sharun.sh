#!/usr/bin/env bash
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

_resolve_dejavu_font() {
    local cached="${SCRIPT_DIR}/DejaVuSans.ttf"
    [ -f "$cached" ] && { printf '%s\n' "$cached"; return 0; }

    local p
    for p in \
        /usr/share/fonts/TTF/DejaVuSans.ttf \
        /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
        /usr/share/fonts/dejavu/DejaVuSans.ttf \
        /usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf \
        /usr/share/fonts/truetype/DejaVuSans.ttf \
        /usr/local/share/fonts/DejaVuSans.ttf \
        /usr/local/share/fonts/TTF/DejaVuSans.ttf
    do
        [ -f "$p" ] && { printf '%s\n' "$p"; return 0; }
    done

    local url="https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2"
    local archive="${SCRIPT_DIR}/.dejavu-ttf-2.37.tar.bz2"
    echo "[i] Шрифт не найден локально, качаю: $url" >&2
    download "$url" "$archive" || { echo "[!] Не удалось скачать DejaVu" >&2; return 1; }

    local tmp; tmp="$(mktemp -d)"
    tar -xjf "$archive" -C "$tmp" 2>/dev/null || { rm -rf "$tmp" "$archive"; return 1; }
    local src; src="$(find "$tmp" -name 'DejaVuSans.ttf' -type f -print -quit)"
    [ -n "$src" ] || { rm -rf "$tmp" "$archive"; return 1; }

    local magic
    magic="$(dd if="$src" bs=4 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    case "$magic" in
        00010000|74727565|74746366|4f54544f) ;;
        *) rm -rf "$tmp" "$archive"; return 1 ;;
    esac

    cp "$src" "$cached"
    rm -rf "$tmp" "$archive"
    echo "[+] DejaVu Sans сохранён в кэш: $cached" >&2
    printf '%s\n' "$cached"
}

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

    # ---------------------------------------------------------------------
    # Dbus хук.
    # ---------------------------------------------------------------------
    echo "[+] Создание dbus-fallback.hook..."
    cat > "$APPDIR/bin/96-dbus-fallback.hook" << 'HOOK'
#!/bin/sh
# GLib/GTK/dconf тянут session bus. Если шины нет —
# переводим GSettings на memory backend и отключаем a11y-мост.
# Если шина есть — ничего не трогаем, пользователь получает свою тему,
# курсор и настройки как обычно.

_dbus_available=0

if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    _dbus_available=1
fi

if [ "$_dbus_available" = 0 ] && [ -n "${XDG_RUNTIME_DIR:-}" ] \
   && [ -S "$XDG_RUNTIME_DIR/bus" ]; then
    _dbus_available=1
fi

if [ "$_dbus_available" = 0 ]; then
    export GSETTINGS_BACKEND=memory
    export G_DBUS_SESSION_BUS_ADDRESS=disabled:
    export NO_AT_BRIDGE=1
fi
HOOK
    chmod +x "$APPDIR/bin/96-dbus-fallback.hook"
    echo "[i] dbus-fallback.hook установлен (сработает только без шины)"

    # ---------------------------------------------------------------------
    # CA-bundle: бандл cacert.pem + хук.
    #
    # Логика зеркалит _resolve_dejavu_font:
    #   1. Системный bundle сборочной машины — приоритет.
    #   2. Скачивание с curl.se / GitHub-зеркала — fallback.
    #
    # Приоритет системному: он и так есть в Arch-образе, обновляется
    # пакетным менеджером, и это на одну сетевую зависимость меньше.
    # ---------------------------------------------------------------------
    echo "[+] Бандлинг CA-сертификатов (fallback)..."
    mkdir -p "$APPDIR/share/ca"
    local _ca_dst="$APPDIR/share/ca/cacert.pem"
    local _ca_ok=0

    # 1. Системные пути сборочной машины. Порядок: сначала то, что
    #    реально лежит в Arch (symlink на tls-ca-bundle.pem), потом —
    #    другие дистрибутивы на случай локальной сборки.
    for _ca_src in \
        /etc/ssl/certs/ca-certificates.crt \
        /usr/local/etc/ssl/certs/ca-certificates.crt \
        /usr/local/etc/ssl/cert.pem \
        /usr/local/share/ca-certificates/ca-certificates.crt \
        /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem \
        /etc/pki/tls/cert.pem \
        /etc/pki/tls/cacert.pem \
        /etc/pki/tls/certs/ca-bundle.crt \
        /etc/pki/tls/certs/ca-bundle.trust.crt \
        /etc/ssl/cert.pem \
        /etc/ssl/ca-bundle.pem \
        /var/lib/ca-certificates/ca-bundle.pem \
        /etc/ca-certificates/extracted/tls-ca-bundle.pem
    do
        if [ -f "$_ca_src" ]; then
            if cp "$_ca_src" "$_ca_dst" 2>/dev/null \
               && grep -q 'BEGIN CERTIFICATE' "$_ca_dst"; then
                echo "[+] CA-bundle из системы сборки: $_ca_src"
                _ca_ok=1
                break
            fi
        fi
    done

    # 2. Не нашли — качаем. Два URL: curl.se и зеркало на GitHub.
    if [ "$_ca_ok" = 0 ]; then
        echo "[i] Системный CA-bundle не найден, качаю..."
        DOWNLOAD_UA="Mozilla/5.0"
        if download_multi "$_ca_dst" \
            "https://curl.se/ca/cacert.pem" \
            "https://raw.githubusercontent.com/bagder/ca-bundle/master/ca-bundle.crt"
        then
            _ca_ok=1
        fi
        unset DOWNLOAD_UA
    fi

    # 3. Валидация. cacert.pem начинается с блока комментариев ##, а не
    #    с первого сертификата — проверяем наличие BEGIN CERTIFICATE
    #    по всему файлу и размер > 50 КБ (HTML-страница ошибки весит
    #    меньше, валидный бандл — 200+ КБ).
    if [ "$_ca_ok" = 1 ]; then
        local _ca_size=0
        _ca_size=$(stat -c %s "$_ca_dst" 2>/dev/null || echo 0)
        if [ "$_ca_size" -lt 51200 ] \
           || ! grep -q 'BEGIN CERTIFICATE' "$_ca_dst"; then
            echo "[!] CA-bundle невалиден (size=${_ca_size}B) — удаляю" >&2
            rm -f "$_ca_dst"
            rmdir "$APPDIR/share/ca" 2>/dev/null || true
            _ca_ok=0
        fi
    fi

    if [ "$_ca_ok" = 1 ]; then
        echo "[+] cacert.pem забандлен ($(du -h "$_ca_dst" | cut -f1), $(grep -c 'BEGIN CERTIFICATE' "$_ca_dst") сертификатов)"
    else
        echo "[!] CA-bundle не забандлен — fallback недоступен, только системные пути" >&2
    fi


    echo "[+] Создание ca-bundle.hook..."
    cat > "$APPDIR/bin/99-ca_bundle.hook" << 'EOF'
#!/bin/sh
# Приоритет: системный CA-bundle → встроенный в AppImage.
# Список путей покрывает Debian/Ubuntu, Fedora/RHEL, Arch,
# openSUSE, Alpine, NixOS, Gentoo и Tiny Core (/usr/local/...).
for c in \
    /etc/ssl/certs/ca-certificates.crt \
    /usr/local/etc/ssl/certs/ca-certificates.crt \
    /usr/local/etc/ssl/cert.pem \
    /usr/local/share/ca-certificates/ca-certificates.crt \
    /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem \
    /etc/pki/tls/cert.pem \
    /etc/pki/tls/cacert.pem \
    /etc/pki/tls/certs/ca-bundle.crt \
    /etc/pki/tls/certs/ca-bundle.trust.crt \
    /etc/ssl/cert.pem \
    /etc/ssl/ca-bundle.pem \
    /var/lib/ca-certificates/ca-bundle.pem \
    /etc/ca-certificates/extracted/tls-ca-bundle.pem
do
    if [ -f "$c" ]; then
        export IPTVPLAYER_CA_BUNDLE="$c"
        export SSL_CERT_FILE="$c"
        break
    fi
done

# Fallback: бандл внутри AppImage. Сработает, если ни один
# системный путь не найден (Tiny Core без ca-certificates.tcz).
if [ -z "$IPTVPLAYER_CA_BUNDLE" ] \
   && [ -f "${SHARUN_DIR}/share/ca/cacert.pem" ]; then
    export IPTVPLAYER_CA_BUNDLE="${SHARUN_DIR}/share/ca/cacert.pem"
    export SSL_CERT_FILE="$IPTVPLAYER_CA_BUNDLE"
fi
EOF
    chmod +x "$APPDIR/bin/99-ca_bundle.hook"

    echo "[i] CA-bundle: системный в приоритете, встроенный — fallback."
    echo "[i] Переопределение — через IPTVPLAYER_CA_BUNDLE (см. README)."

    # --- Бандл шрифта ---
    echo "[+] Бандлинг fallback-шрифта..."
    local font_src
    if ! font_src="$(_resolve_dejavu_font)"; then
        echo "[!] Шрифт не забандлен — на системах без fontconfig будет тофу." >&2
        unset ARCH VERSION OUTPATH OUTNAME ICON DESKTOP \
              UPINFO GTK_CLASS_FIX DEPLOY_GDK \
              DEPLOY_OPENGL DEPLOY_VULKAN
        rm -f "$QUICK_SHARUN"
        return 1
    fi
    mkdir -p "$APPDIR/share/fonts"
    cp "$font_src" "$APPDIR/share/fonts/DejaVuSans.ttf"
    echo "[+] Забандлен $(basename "$font_src") ($(du -h "$font_src" | cut -f1))"

    # --- Статический fonts.conf ---
    # ${SHARUN_DIR} в XML не раскрывается. prefix="relative" разрешает
    # ../../share/fonts относительно самого fonts.conf → $APPDIR/share/fonts.
    # sharun сам выставит FONTCONFIG_FILE=$SHARUN_DIR/etc/fonts/fonts.conf,
    # если системного /etc/fonts/fonts.conf нет (документировано в README sharun).
    echo "[+] Создание fonts.conf..."
    mkdir -p "$APPDIR/etc/fonts"
    cat > "$APPDIR/etc/fonts/fonts.conf" << 'FONTCONF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir prefix="relative">../../share/fonts</dir>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <cachedir>/tmp/fontconfig-cache</cachedir>
  <include ignore_missing="yes">/etc/fonts/conf.d</include>
  <include ignore_missing="yes">/usr/local/etc/fonts/conf.d</include>
</fontconfig>
FONTCONF

    # --- fonts.hook: cachedir + защита от fallback-скана ---
    echo "[+] Создание fonts.hook..."
    cat > "$APPDIR/bin/98-fonts.hook" << 'HOOK'
#!/bin/sh
# FONTCONFIG_FILE выставлен sharun'ом на $SHARUN_DIR/etc/fonts/fonts.conf.
# FONTCONFIG_PATH sharun не выставляет — задаём сами, если конфиг
# использует <include> с относительными путями.
export FONTCONFIG_PATH="${SHARUN_DIR}/etc/fonts"

# Cache — writable-каталог, иначе fontconfig пытается писать
# рядом со шрифтами и сдаётся.
mkdir -p /tmp/fontconfig-cache 2>/dev/null || true

# XDG_DATA_HOME на пустой temp: отрезает рекурсивный обход
# ~/.fonts и ~/.local/share/fonts — именно там fontconfig виснет
# на Tiny Core (симлинки вглубь /tmp/tcloop/...).
export XDG_DATA_HOME="${TMPDIR:-/tmp}/sharun-xdg-$$"
mkdir -p "$XDG_DATA_HOME/fonts" 2>/dev/null || true
HOOK
    chmod +x "$APPDIR/bin/98-fonts.hook"


    # ---------------------------------------------------------------------
    # Упаковка
    # ---------------------------------------------------------------------
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