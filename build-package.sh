#!/bin/bash
# =============================================
# Скрипт сборки .deb, .rpm и .AppImage для iptvplayer
# Работает на Debian/Ubuntu и Fedora/RHEL/CentOS
#
# Поддерживает:
#   - Автоопределение версии из git
#   - Сборку .deb (dpkg-deb)
#   - Сборку .rpm (через rpmbuild, без alien)
#   - Сборку AppImage (через linuxdeploy)
#   - Установку .desktop в меню
#   - Подпись пакетов (опционально)
#   - Проверку зависимостей
#
# Использование:
#   ./build-package.sh [версия]
#
# Примеры:
#   ./build-package.sh              # Версия из git или 1.0.0
#   ./build-package.sh 2.3.1        # Указать версию вручную
#
# Что ты хочешь?	Команда
# ✅ Просто собрать пакеты	./build-package.sh 2.3.1
# 🔐 Подписать пакеты	export GPG_KEY="you@email.com" && ./build-package.sh 2.3.1
# 🧹 Очистить и начать заново	rm -rf build pkg-* dist/ && ./build-package.sh 2.3.1
# =============================================

set -e

# === Настройки ===
PACKAGE_NAME="iptvplayer"
BUILD_DIR="build"
STAGING_DIR="pkg-staging"
OUTPUT_DIR="dist"
SPEC_DIR="pkg-rpm"
APPDIR="${PACKAGE_NAME}.AppDir"

ICON_NAME="playlists.png"
DESKTOP_FILE="$APPDIR/${PACKAGE_NAME}.desktop"

# Зависимости
REQUIRED_TOOLS=(
    "git" "gcc" "g++" "make" "cmake" "ninja-build"
    "fakeroot" "dpkg-dev" "rpm" "rpmbuild" "desktop-file-utils"
    "wget" "tar" "gzip" "patchelf" "zsyncmake"
)

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
    for tool in "${REQUIRED_TOOLS[@]}"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing+=("$tool")
        fi
    done

    if [ ${#missing[@]} -ne 0 ]; then
        echo "[!] Не хватает: ${missing[*]}"
        read -p "Установить через apt? [Y/n] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Nn]$ ]]; then
            if command -v apt >/dev/null; then
                sudo apt update && sudo apt install -y "${missing[@]}"
            elif command -v dnf >/dev/null; then
                sudo dnf install -y "${missing[@]}"
            elif command -v yum >/dev/null; then
                sudo yum install -y "${missing[@]}"
            else
                echo "[!] Не найден пакетный менеджер: apt/dnf/yum"
                exit 1
            fi
            echo "[✓] Зависимости установлены"
        else
            echo "[!] Прервано пользователем"
            exit 1
        fi
    fi
}

# === Очистка и подготовка ===
setup_dirs() {
    rm -rf "$BUILD_DIR" "$STAGING_DIR" "$SPEC_DIR" "$OUTPUT_DIR" "$APPDIR"
    mkdir -p \
        "$BUILD_DIR" \
        "$STAGING_DIR/opt/$PACKAGE_NAME/icons" \
        "$OUTPUT_DIR" \
        "$SPEC_DIR/SOURCES" \
        "$APPDIR/Applications" \
        "$APPDIR/usr/bin" \
        "$APPDIR/usr/share/applications" \
        "$APPDIR/usr/share/icons/hicolor/scalable/apps"
}

# === Сборка бинарника ===
build_binary() {
    echo "[+] Конфигурация CMake..."
    cmake -S . -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="/opt/$PACKAGE_NAME" \
        -G Ninja

    echo "[+] Сборка проекта..."
    cmake --build "$BUILD_DIR" --config Release -j$(nproc)

    echo "[+] Установка во временную директорию..."
    DESTDIR="$STAGING_DIR" cmake --install "$BUILD_DIR" --config Release

    cp -r icons/* "$STAGING_DIR/opt/$PACKAGE_NAME/icons/" 2>/dev/null || true
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

    fakeroot chown -R root:root "$STAGING_DIR/opt"
    fakeroot chmod -R 755 "$STAGING_DIR/opt"
    fakeroot chmod 755 "$STAGING_DIR/DEBIAN"

    fakeroot dpkg-deb --build "$STAGING_DIR" "$deb_file"
    echo "[✓] .deb создан: $deb_file"
}

# === Создание .rpm ===
build_rpm() {
    local version=$1
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${version}-${release}.x86_64.rpm"

    echo "[+] Создание .rpm..."

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
# данные уже готовы

%install
rm -rf \$RPM_BUILD_ROOT
mkdir -p \$RPM_BUILD_ROOT
tar -xzf %{SOURCE0} -C \$RPM_BUILD_ROOT

%files
/opt/$PACKAGE_NAME/*

%post
if [ ! -f "/usr/share/applications/$PACKAGE_NAME.desktop" ]; then
    cat > /usr/share/applications/$PACKAGE_NAME.desktop << 'EOT'
[Desktop Entry]
Name=iptvplayer
Exec=/opt/iptvplayer/iptvplayer
Icon=playlists
Type=Application
Categories=Network;Player;
Comment=IPTV Playlist Player
EOT
fi
if [ ! -L "/usr/bin/iptvplayer" ]; then
    ln -s /opt/iptvplayer/iptvplayer /usr/bin/iptvplayer
fi

%preun
if [ \$1 == 0 ]; then
    rm -f /usr/share/applications/$PACKAGE_NAME.desktop
    rm -f /usr/bin/iptvplayer
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

    # Копируем бинарник и ресурсы
    cp -r "$STAGING_DIR/opt/$PACKAGE_NAME"/* "$APPDIR/Applications/"

    # Символическая ссылка
    ln -s /Applications/iptvplayer "$APPDIR/usr/bin/iptvplayer"

    # Иконка
    cp "icons/$ICON_NAME" "$APPDIR/usr/share/icons/hicolor/scalable/apps/${PACKAGE_NAME}.png"

    # Desktop файл
    cat > "$DESKTOP_FILE" << EOF
[Desktop Entry]
Name=iptvplayer
Exec=iptvplayer
Icon=${PACKAGE_NAME}
Type=Application
Categories=Network;Player;
Comment=IPTV Playlist Player
EOF
    cp "$DESKTOP_FILE" "$APPDIR/usr/share/applications/"

    # Скачиваем linuxdeploy
    local LINUXDEPLOY="linuxdeploy-x86_64.AppImage"
    if [ ! -f "$LINUXDEPLOY" ]; then
        wget -q "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/$LINUXDEPLOY"
        chmod +x "$LINUXDEPLOY"
    fi

    # Создаём AppImage
    ARCH=x86_64 ./$LINUXDEPLOY --appdir="$APPDIR" --output=appimage
    mv "$PACKAGE_NAME-"*-x86_64.AppImage "$appimage_file"

    # Генерация zsync
    zsyncmake "$appimage_file" -o "$(basename "$appimage_file" .AppImage).zsync"

    echo "[✓] AppImage создан: $appimage_file"
}

# === Подпись пакетов (опционально) ===
sign_packages() {
    local version=$1
    if [ -z "$GPG_KEY" ]; then
        echo "[ ] Подпись отключена (не задан GPG_KEY)"
        return
    fi

    echo "[+] Подпись пакетов GPG-ключом: $GPG_KEY"

    for file in "$OUTPUT_DIR"/*.{deb,rpm,AppImage}; do
        [ -f "$file" ] || continue
        gpg --default-key "$GPG_KEY" --detach-sign --armor "$file"
        echo "    [✓] Подписан: $file.asc"
    done
}

# === Главная функция ===
main() {
    local version=$(get_version "$1")

    echo "=== Сборка пакетов для $PACKAGE_NAME:$version ==="
    echo ""

    check_deps
    setup_dirs
    build_binary
    build_deb "$version"
    build_rpm "$version"
    build_appimage "$version"
    sign_packages "$version"

    echo ""
    echo "🎉 Готово! Артефакты в './dist/':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"