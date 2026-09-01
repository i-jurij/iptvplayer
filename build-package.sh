#!/bin/bash
# =============================================
# build-package.sh – Сборка пакетов .deb, .rpm, .AppImage
#
# Использование:
#   ./build-package.sh [VERSION] [--deb] [--rpm] [--appimage] [--all]
#
# Примеры:
#   ./build-package.sh 2.3.1 --deb          # только .deb
#   ./build-package.sh --all                # все пакеты (версия из git)
#   ./build-package.sh 2.3.1 --deb --rpm    # .deb и .rpm
# =============================================

set -e

# === Настройки ===
PACKAGE_NAME="iptvplayer"
BUILD_RELEASE_SCRIPT="./build-release.sh"
OUTPUT_DIR="dist"
STAGING_DIR="pkg-staging"
APPDIR="${PACKAGE_NAME}.AppDir"
ICON_NAME="program.svg"   # основная иконка в проекте

# === Определение версии ===
get_version() {
    if [ -n "$1" ]; then
        echo "$1"
    elif command -v git >/dev/null 2>&1 && git rev-parse --git-dir >/dev/null 2>&1; then
        git describe --tags --always | sed 's/^v//'
    else
        echo "1.0.0"
    fi
}

# === Проверка зависимостей ===
check_deps() {
    local missing=()
    for tool in cmake git fakeroot dpkg-deb rpmbuild desktop-file-utils wget; do
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
    rm -rf "$STAGING_DIR" "$OUTPUT_DIR" "$APPDIR"
    mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"
}

# === Сборка бинарника через build-release.sh ===
build_binary() {
    echo "[+] Сборка через $BUILD_RELEASE_SCRIPT..."
    if [ ! -f "$BUILD_RELEASE_SCRIPT" ]; then
        echo "[!] $BUILD_RELEASE_SCRIPT не найден. Запустите из корня проекта."
        exit 1
    fi

    # Собираем Release с установкой в локальную папку install/
    ./build-release.sh --type release --prefix ./install

    # Подготавливаем STAGING_DIR для пакетирования
    # Бинарник → /usr/bin
    mkdir -p "$STAGING_DIR/usr/bin"
    cp install/bin/$PACKAGE_NAME "$STAGING_DIR/usr/bin/"

    # Ресурсы (icons, json, etc.) → /usr/share/iptvplayer
    mkdir -p "$STAGING_DIR/usr/share/$PACKAGE_NAME"
    cp -r install/share/$PACKAGE_NAME/* "$STAGING_DIR/usr/share/$PACKAGE_NAME/"
}

# === Создание .deb ===
build_deb() {
    local version=$1
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${version}_amd64.deb"

    echo "[+] Создание .deb..."

    mkdir -p "$STAGING_DIR/DEBIAN"
    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $version
Section: network
Priority: optional
Architecture: amd64
Maintainer: Your Name <your.email@example.com>
Homepage: https://github.com/yourusername/$PACKAGE_NAME
Description: IPTV Playlist Player
 A simple player for M3U playlists with GUI.
EOF

    # .desktop файл
    mkdir -p "$STAGING_DIR/usr/share/applications"
    cat > "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME
Icon=$PACKAGE_NAME
Type=Application
Categories=Network;Player;
Comment=IPTV Playlist Player
EOF

    # Иконка для меню (из ресурсов)
    mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
    cp "install/share/$PACKAGE_NAME/icons/$ICON_NAME" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg" 2>/dev/null || true

    fakeroot chown -R root:root "$STAGING_DIR/usr"
    fakeroot chmod -R 755 "$STAGING_DIR/usr"
    fakeroot chmod 755 "$STAGING_DIR/DEBIAN"

    fakeroot dpkg-deb --build "$STAGING_DIR" "$deb_file"
    echo "[✓] .deb создан: $deb_file"
}

# === Создание .rpm ===
build_rpm() {
    local version=$1
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${version}-${release}.x86_64.rpm"
    local SPEC_DIR="pkg-rpm"

    echo "[+] Создание .rpm..."

    mkdir -p "$SPEC_DIR/SOURCES"
    # Упаковываем содержимое STAGING_DIR в архив
    cd "$STAGING_DIR" && tar -czf "../$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${version}.tar.gz" . && cd - > /dev/null

    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define _topdir %(pwd)/$SPEC_DIR
Name:           $PACKAGE_NAME
Version:        $version
Release:        $release
Summary:        IPTV Playlist Player
License:        MIT
URL:            https://github.com/yourusername/$PACKAGE_NAME
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
tar -xzf %{SOURCE0} -C \$RPM_BUILD_ROOT

%files
/usr/bin/$PACKAGE_NAME
/usr/share/$PACKAGE_NAME/*
/usr/share/applications/$PACKAGE_NAME.desktop
/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg

%post
# (опционально) обновить кэш иконок
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi

%preun
if [ \$1 == 0 ]; then
    rm -f /usr/share/applications/$PACKAGE_NAME.desktop
    rm -f /usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg
    rm -f /usr/bin/$PACKAGE_NAME
    # (опционально) очистить кэш
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi

%changelog
* $(date +"%a %b %d %Y") Your Name <your.email@example.com> - $version-$release
- Initial build
EOF

    rpmbuild -bb --define "_topdir $PWD/$SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"
    mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null || mv "$SPEC_DIR/RPMS/x86_64/"*.rpm "$rpm_file"
    echo "[✓] .rpm создан: $rpm_file"
}

# === Создание AppImage ===
build_appimage() {
    local version=$1
    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-x64.AppImage"

    echo "[+] Создание AppImage..."

    # Подготовка AppDir
    mkdir -p "$APPDIR/usr/bin"
    mkdir -p "$APPDIR/usr/share/applications"
    mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

    # Копируем бинарник и ресурсы из STAGING_DIR
    cp "$STAGING_DIR/usr/bin/$PACKAGE_NAME" "$APPDIR/usr/bin/"
    cp -r "$STAGING_DIR/usr/share/$PACKAGE_NAME" "$APPDIR/usr/share/"

    # Иконка для AppDir
    cp "$APPDIR/usr/share/$PACKAGE_NAME/icons/$ICON_NAME" "$APPDIR/usr/share/icons/hicolor/scalable/apps/$PACKAGE_NAME.svg"

    # .desktop файл
    cat > "$APPDIR/$PACKAGE_NAME.desktop" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME
Icon=$PACKAGE_NAME
Type=Application
Categories=Network;Player;
Comment=IPTV Playlist Player
EOF

    # Скачиваем linuxdeploy
    local LINUXDEPLOY="linuxdeploy-x86_64.AppImage"
    if [ ! -f "$LINUXDEPLOY" ]; then
        wget -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$LINUXDEPLOY"
        chmod +x "$LINUXDEPLOY"
    fi

    # Создаём AppImage
    ARCH=x86_64 ./$LINUXDEPLOY --appdir="$APPDIR" --output=appimage
    mv "$PACKAGE_NAME-"*-x86_64.AppImage "$appimage_file"

    # (опционально) zsync
    if command -v zsyncmake >/dev/null; then
        zsyncmake "$appimage_file" -o "$(basename "$appimage_file" .AppImage).zsync"
    fi

    echo "[✓] AppImage создан: $appimage_file"
}

# === Главная функция ===
main() {
    local VERSION=""
    local BUILD_DEB=false
    local BUILD_RPM=false
    local BUILD_APPIMAGE=false

    # Парсинг аргументов
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --deb) BUILD_DEB=true ;;
            --rpm) BUILD_RPM=true ;;
            --appimage) BUILD_APPIMAGE=true ;;
            --all) BUILD_DEB=true; BUILD_RPM=true; BUILD_APPIMAGE=true ;;
            -h|--help)
                echo "Использование: $0 [VERSION] [--deb] [--rpm] [--appimage] [--all]"
                echo "  VERSION      – версия (по умолчанию из git или 1.0.0)"
                echo "  --deb        – собрать .deb"
                echo "  --rpm        – собрать .rpm"
                echo "  --appimage   – собрать AppImage"
                echo "  --all        – собрать все пакеты"
                exit 0
                ;;
            *)
                if [[ -z "$VERSION" ]]; then
                    VERSION="$1"
                else
                    echo "Неизвестный аргумент: $1"
                    exit 1
                fi
                ;;
        esac
        shift
    done

    # Если не указан ни один тип, собираем всё (для совместимости)
    if [[ "$BUILD_DEB" == false && "$BUILD_RPM" == false && "$BUILD_APPIMAGE" == false ]]; then
        BUILD_DEB=true; BUILD_RPM=true; BUILD_APPIMAGE=true
    fi

    VERSION=$(get_version "$VERSION")
    echo "=== Сборка пакетов для $PACKAGE_NAME:$VERSION ==="
    echo ""

    check_deps
    setup_dirs
    build_binary

    if [[ "$BUILD_DEB" == true ]]; then build_deb "$VERSION"; fi
    if [[ "$BUILD_RPM" == true ]]; then build_rpm "$VERSION"; fi
    if [[ "$BUILD_APPIMAGE" == true ]]; then build_appimage "$VERSION"; fi

    echo ""
    echo "🎉 Готово! Артефакты в './$OUTPUT_DIR/':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"