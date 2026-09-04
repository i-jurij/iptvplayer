#!/bin/bash
# =============================================
# build-package.sh – Сборка пакетов .deb, .rpm, .AppImage
#
# Использование:
#   ./build-package.sh [VERSION] [ОПЦИИ]
#
# Опции:
#   --deb         Собрать только .deb
#   --rpm         Собрать только .rpm
#   --appimage    Собрать только AppImage
#   --all         Собрать все типы пакетов (по умолчанию)
#   --rebuild     Принудительно пересобрать бинарник (даже если уже есть)
#   --clean       Очистить каталог dist/ перед сборкой
#   --clean-only  Только очистить dist/ и завершить работу
#   -h, --help    Показать эту справку
#
# Версия:
#   Если не указана, определяется автоматически из VERSION файла, затем из git describe, иначе 1.0.0
#
# Примеры:
#   ./build-package.sh 2.3.1 --deb          # собрать .deb с версией 2.3.1
#   ./build-package.sh --all                # собрать все пакеты, версия из VERSION или git
#   ./build-package.sh --rebuild --deb      # пересобрать бинарник и .deb
#   ./build-package.sh --clean --all        # очистить dist и собрать всё
#   ./build-package.sh --clean-only         # только очистить dist
#   ./build-package.sh -h                   # показать эту справку
# =============================================

set -e

# === Настройки ===
PACKAGE_NAME="iptvplayer"
BUILD_RELEASE_SCRIPT="./build-release.sh"
OUTPUT_DIR="dist"
STAGING_DIR="pkg-staging"
APPDIR="${PACKAGE_NAME}.AppDir"
ICON_NAME="program.svg"
FORCE_REBUILD=false
DO_CLEAN=false
CLEAN_ONLY=false

# ---- Функция получения версий из VERSION и Git ----
get_versions() {
    if [ -f "VERSION" ]; then
        VERSION=$(head -n1 VERSION | tr -d '\n\r')
    else
        VERSION="0.0.0"
    fi

    VERSION_DISPLAY="$VERSION"
    VERSION_FILE="$VERSION"

    if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
        SHORT=$(git rev-parse --short HEAD 2>/dev/null || true)
        if [ -n "$SHORT" ]; then
            VERSION_DISPLAY="${VERSION}+g${SHORT}"
            VERSION_FILE="${VERSION}-${SHORT}"
        fi
    fi

    printf '%s\n%s\n' "$VERSION_DISPLAY" "$VERSION_FILE"
}

# === Проверка зависимостей ===
check_deps() {
    local missing=()
    for tool in cmake git fakeroot dpkg-deb rpmbuild wget tar; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing+=("$tool")
        fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
        echo "[!] Не хватает: ${missing[*]}"
        echo "Установите их вручную или через пакетный менеджер."
        exit 1
    fi
}

# === Очистка и подготовка ===
setup_dirs() {
    if [ "$DO_CLEAN" = true ]; then
        echo "[+] Очистка каталога $OUTPUT_DIR..."
        rm -rf "$OUTPUT_DIR"/*
        mkdir -p "$OUTPUT_DIR"
    fi

    mkdir -p "$OUTPUT_DIR"
    rm -rf "$STAGING_DIR" "$APPDIR"
    mkdir -p "$STAGING_DIR"
}

# === Сборка бинарника ===
build_binary() {
    if [ -f "install/bin/$PACKAGE_NAME" ] && [ "$FORCE_REBUILD" = false ]; then
        echo "[+] Бинарник уже собран: install/bin/$PACKAGE_NAME"
        read -p "Использовать существующий? (Y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Nn]$ ]]; then
            FORCE_REBUILD=true
        else
            echo "[+] Используем существующий бинарник."
            return 0
        fi
    fi

    echo "[+] Сборка через $BUILD_RELEASE_SCRIPT..."
    if [ ! -f "$BUILD_RELEASE_SCRIPT" ]; then
        echo "[!] $BUILD_RELEASE_SCRIPT не найден. Запустите из корня проекта."
        exit 1
    fi

    # Передаём чистую версию в билд-скрипт
    ./build-release.sh --type release --prefix ./install --version "$VERSION"
    echo "[+] Бинарник собран."
}

# === Подготовка STAGING_DIR ===
prepare_staging() {
    if [ ! -d "install/bin" ] || [ ! -f "install/bin/$PACKAGE_NAME" ]; then
        echo "[!] Бинарник не найден. Запустите сборку или укажите --rebuild."
        exit 1
    fi

    mkdir -p "$STAGING_DIR/usr/bin"
    cp "install/bin/$PACKAGE_NAME" "$STAGING_DIR/usr/bin/"

    mkdir -p "$STAGING_DIR/usr/share/$PACKAGE_NAME"
    cp -r "install/share/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/$PACKAGE_NAME/" 2>/dev/null || true

    mkdir -p "$STAGING_DIR/usr/share/applications"
    cat > "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME %F
Icon=$PACKAGE_NAME
Type=Application
Categories=AudioVideo;Player;
Comment=IPTV Playlist Player
Terminal=false
StartupNotify=true
MimeType=video/mp4;video/x-matroska;video/avi;video/mpeg;video/quicktime;video/x-msvideo;video/x-flv;video/ogg;video/webm;application/x-mpegURL;audio/x-mpegurl;audio/x-scpls;application/xspf+xml;application/vnd.apple.mpegurl;
EOF

    mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
    cp "install/share/$PACKAGE_NAME/icons/$ICON_NAME" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg" 2>/dev/null || true
}

# === Очистка временных каталогов ===
cleanup() {
    echo "[+] Очистка временных каталогов..."
    rm -rf "$STAGING_DIR" "$APPDIR" "pkg-rpm"
}

# Удаляем временные каталоги при любом завершении скрипта
trap cleanup EXIT

# === Сборка .deb ===
build_deb() {
    # Имя файла используем VERSION_FILE, а метаданные — чистую VERSION
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION_FILE}_amd64.deb"
    echo "[+] Создание .deb..."
    mkdir -p "$STAGING_DIR/DEBIAN"
    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: network
Priority: optional
Architecture: amd64
Maintainer: ijurij <mnisjil@duck.com>
Homepage: https://github.com/i-jurij/$PACKAGE_NAME
Description: IPTV Playlist Player
 A simple player for M3U playlists with GUI.
EOF

    cat > "$STAGING_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/postinst"

    cat > "$STAGING_DIR/DEBIAN/prerm" << 'EOF'
#!/bin/bash
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    rm -f /usr/share/applications/iptvplayer.desktop
    rm -f /usr/share/icons/hicolor/scalable/apps/iptvplayer.svg
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/prerm"

    fakeroot chown -R root:root "$STAGING_DIR/usr"
    fakeroot chmod -R 755 "$STAGING_DIR/usr"
    fakeroot chmod 755 "$STAGING_DIR/DEBIAN"

    fakeroot dpkg-deb --build "$STAGING_DIR" "$deb_file"
    echo "[✓] .deb создан: $deb_file"
}

# === Сборка .rpm ===
build_rpm() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION_FILE}-${release}.x86_64.rpm"
    local SPEC_DIR="pkg-rpm"

    echo "[+] Создание .rpm..."

    mkdir -p "$SPEC_DIR/SOURCES"
    cd "$STAGING_DIR" && tar -czf "../$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
        --transform="s,^,$PACKAGE_NAME-$VERSION/," . && cd - > /dev/null

    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir %(pwd)/$SPEC_DIR
Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $release
Summary:        IPTV Playlist Player
License:        MIT
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      x86_64

%description
A simple player for M3U playlists with GUI.

%prep
%setup -q

%build
# already built

%install
rm -rf \$RPM_BUILD_ROOT
mkdir -p \$RPM_BUILD_ROOT
tar -xzf %{SOURCE0} -C \$RPM_BUILD_ROOT --strip-components=1

%files
/usr/bin/$PACKAGE_NAME
/usr/share/$PACKAGE_NAME/*
/usr/share/applications/$PACKAGE_NAME.desktop
/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg

%post
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi

%preun
if [ \$1 == 0 ]; then
    rm -f /usr/share/applications/$PACKAGE_NAME.desktop
    rm -f /usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg
    rm -f /usr/bin/$PACKAGE_NAME
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$release
- Initial build
EOF

    rpmbuild -bb --define "_topdir $PWD/$SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"
    mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null || mv "$SPEC_DIR/RPMS/x86_64/"*.rpm "$rpm_file"
    echo "[✓] .rpm создан: $rpm_file"
}

# === Создание AppImage ===
build_appimage() {
    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-x64-${VERSION_FILE}.AppImage"

    echo "[+] Создание AppImage..."

    # Подготовка AppDir
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/scalable/apps"
    cp "$STAGING_DIR/usr/bin/$PACKAGE_NAME" "$APPDIR/usr/bin/"
    cp -r "$STAGING_DIR/usr/share/$PACKAGE_NAME" "$APPDIR/usr/share/"
    cp "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" "$APPDIR/"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg" "$APPDIR/$PACKAGE_NAME.svg"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

    # Исправление категории в .desktop (убираем предупреждение appimagetool)
    sed -i 's/^Categories=.*/Categories=Network;AudioVideo;Player;/' "$APPDIR/$PACKAGE_NAME.desktop"

    local LINUXDEPLOY="linuxdeploy-x86_64.AppImage"
    local GTK_PLUGIN="linuxdeploy-plugin-gtk.sh"

    # Скачиваем linuxdeploy (если отсутствует)
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "[+] Скачивание linuxdeploy..."
        wget -q --show-progress "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$LINUXDEPLOY"
        chmod +x "$LINUXDEPLOY"
    fi

    # Скачиваем GTK-плагин (скрипт) (если отсутствует)
    if [ ! -f "$GTK_PLUGIN" ]; then
        echo "[+] Скачивание GTK-плагина (скрипт)..."
        wget -q --show-progress "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/$GTK_PLUGIN"
        chmod +x "$GTK_PLUGIN"
    fi

    echo "[+] Запуск linuxdeploy с GTK-плагином..."
    if ARCH=x86_64 ./$LINUXDEPLOY --appdir="$APPDIR" --plugin gtk --output=appimage; then
        echo "[✓] linuxdeploy завершился успешно."
    else
        echo "[!] Ошибка при создании AppImage."
        echo "Для отладки запустите вручную:"
        echo "    ARCH=x86_64 ./$LINUXDEPLOY --appdir=$APPDIR --plugin gtk --output=appimage"
        exit 1
    fi

    # ---- Ищем созданный AppImage (исключая linuxdeploy) ----
    local found_appimage=""
    if [ -f "IPTV_Player-x86_64.AppImage" ]; then
        found_appimage="IPTV_Player-x86_64.AppImage"
    elif [ -f "${PACKAGE_NAME}-x86_64.AppImage" ]; then
        found_appimage="${PACKAGE_NAME}-x86_64.AppImage"
    else
        # Ищем любой файл, заканчивающийся на -x86_64.AppImage, но не linuxdeploy
        found_appimage=$(find . -maxdepth 1 -name "*-x86_64.AppImage" ! -name "linuxdeploy*" -print -quit)
    fi

    if [ -n "$found_appimage" ]; then
        echo "[+] Найден AppImage: $found_appimage"
        mv "$found_appimage" "$appimage_file"
        echo "[✓] AppImage перемещён в $appimage_file"
    else
        echo "[!] AppImage не найден ни в корне, ни в $OUTPUT_DIR."
        exit 1
    fi

    # Генерация zsync (опционально)
    if command -v zsyncmake >/dev/null; then
        echo "[+] Генерация zsync..."
        zsyncmake "$appimage_file" -o "$(basename "$appimage_file" .AppImage).zsync"
    fi
}

# === Вывод справки ===
show_help() {
    cat << EOF
Использование: ./build-package.sh [VERSION] [ОПЦИИ]

Сборка пакетов .deb, .rpm, .AppImage для iptvplayer.

Аргументы:
  VERSION         Версия пакета (по умолчанию определяется из VERSION или git)

Опции:
  --deb           Собрать только .deb
  --rpm           Собрать только .rpm
  --appimage      Собрать только AppImage
  --all           Собрать все типы (по умолчанию)
  --rebuild       Принудительно пересобрать бинарник (даже если уже есть)
  --clean         Очистить каталог dist/ перед сборкой
  --clean-only    Только очистить dist/ и завершить работу
  -h, --help      Показать эту справку

Примеры:
  ./build-package.sh 2.3.1 --deb          # собрать .deb с версией 2.3.1
  ./build-package.sh --all                # собрать все пакеты, версия из VERSION
  ./build-package.sh --rebuild --deb      # пересобрать бинарник и .deb
  ./build-package.sh --clean --all        # очистить dist и собрать всё
  ./build-package.sh --clean-only         # только очистить dist
  ./build-package.sh -h                   # показать эту справку

Примечания:
  - Если не указан тип пакета, собираются все три.
  - Все артефакты сохраняются в каталог dist/.
  - Временные файлы удаляются автоматически после сборки.
  - Для сборки требуются: cmake, git, fakeroot, dpkg-deb, rpmbuild, wget, tar.
EOF
}

# === Главная функция ===
main() {
    local VERSION=""
    local BUILD_DEB=false
    local BUILD_RPM=false
    local BUILD_APPIMAGE=false

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --deb) BUILD_DEB=true ;;
            --rpm) BUILD_RPM=true ;;
            --appimage) BUILD_APPIMAGE=true ;;
            --all) BUILD_DEB=true; BUILD_RPM=true; BUILD_APPIMAGE=true ;;
            --rebuild) FORCE_REBUILD=true ;;
            --clean) DO_CLEAN=true ;;
            --clean-only) DO_CLEAN=true; CLEAN_ONLY=true ;;
            -h|--help) show_help; exit 0 ;;
            *)
                if [[ -z "$VERSION" ]]; then
                    VERSION="$1"
                else
                    echo "Неизвестный аргумент: $1"
                    show_help
                    exit 1
                fi
                ;;
        esac
        shift
    done

    if [[ "$CLEAN_ONLY" == true ]]; then
        echo "[+] Очистка каталога $OUTPUT_DIR..."
        rm -rf "$OUTPUT_DIR"/*
        mkdir -p "$OUTPUT_DIR"
        echo "[✓] Очистка завершена."
        exit 0
    fi

    if [[ "$BUILD_DEB" == false && "$BUILD_RPM" == false && "$BUILD_APPIMAGE" == false ]]; then
        BUILD_DEB=true; BUILD_RPM=true; BUILD_APPIMAGE=true
    fi

    # ---- Получение версий ----
    if [ -n "$VERSION" ]; then
        # Если передана явно, используем её как чистую версию
        VERSION_DISPLAY="$VERSION"
        VERSION_FILE="$VERSION"
        if command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
            SHORT=$(git rev-parse --short HEAD 2>/dev/null || true)
            if [ -n "$SHORT" ]; then
                VERSION_DISPLAY="${VERSION}+g${SHORT}"
                VERSION_FILE="${VERSION}-${SHORT}"
            fi
        fi
    else
        read VERSION_DISPLAY VERSION_FILE < <(get_versions)
        VERSION=$(echo "$VERSION_DISPLAY" | sed 's/+g.*//')
    fi

    echo "=== Сборка пакетов для $PACKAGE_NAME:$VERSION (файл: $VERSION_FILE) ==="
    echo ""

    check_deps
    setup_dirs
    build_binary
    prepare_staging

    if [[ "$BUILD_DEB" == true ]]; then build_deb; fi
    if [[ "$BUILD_RPM" == true ]]; then build_rpm; fi
    if [[ "$BUILD_APPIMAGE" == true ]]; then build_appimage; fi

    cleanup

    echo ""
    echo "🎉 Готово! Артефакты в './$OUTPUT_DIR/':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"