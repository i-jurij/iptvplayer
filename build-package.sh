#!/bin/bash
# =============================================
# build-package.sh – Сборка пакетов .deb, .rpm, .pkg.tar.zst, .AppImage
#
# Использование:
#   ./build-package.sh [ОПЦИИ]
#
# Варианты сборки:
#   --native-deb      Нативный .deb (системные библиотеки, Ubuntu/Debian)
#   --native-rpm      Нативный .rpm (Fedora/Rocky/RHEL/openSUSE)
#   --native-arch     Нативный .pkg.tar.zst (Arch/Manjaro)
#   --appimage        AppImage (bundled, работает везде)
#
# Bundled-варианты (обычно только для CI):
#   --bundle-deb      Bundled .deb (всё внутри /opt/iptvplayer)
#   --bundle-rpm      Bundled .rpm (всё внутри /opt/iptvplayer)
#
# Комбинированные:
#   --native          Все нативные пакеты, доступные здесь
#   --native-appimage Нативные + AppImage
#   --bundle          Bundled .deb + bundled .rpm
#   --all             Всё возможное на этой системе
#
# Служебные:
#   --rebuild         Принудительно пересобрать бинарник
#   --clean           Очистить dist/ перед сборкой
#   --clean-only      Только очистить dist/
#   --no-menu         Не показывать меню (для скриптов)
#   --yes, -y         Неинтерактивный режим
#   -h, --help        Показать справку
# =============================================

set -e

# ---- Корень проекта ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# === Настройки ===
PACKAGE_NAME="iptvplayer"
ICON_NAME="${PACKAGE_NAME}.svg"
BUNDLE_PREFIX="/opt/${PACKAGE_NAME}"

DEB_ARCH=""
RPM_ARCH=""
APPIMAGE_ARCH=""
DISTRO=""

# METAINFO_NAME
if [ ! -f "$SCRIPT_DIR/METAINFO_NAME" ]; then
    echo "[!] Файл $SCRIPT_DIR/METAINFO_NAME не найден."
    exit 1
fi
METAINFO_NAME="$(tr -d '\n\r' < "$SCRIPT_DIR/METAINFO_NAME" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
if [ -z "$METAINFO_NAME" ]; then
    echo "[!] Файл $SCRIPT_DIR/METAINFO_NAME пуст."
    exit 1
fi

BUILD_RELEASE_SCRIPT="$SCRIPT_DIR/build-release.sh"
OUTPUT_DIR="$SCRIPT_DIR/dist"
STAGING_DIR="$SCRIPT_DIR/pkg-staging"
APPDIR="$SCRIPT_DIR/${PACKAGE_NAME}.AppDir"
FORCE_REBUILD=false
DO_CLEAN=false
CLEAN_ONLY=false

# === Неинтерактивный режим ===
NON_INTERACTIVE=false
if [[ -n "${CI:-}" ]] || [[ -n "${GITHUB_ACTIONS:-}" ]] || [[ ! -t 0 ]]; then
    NON_INTERACTIVE=true
fi

ask() {
    local prompt="$1"
    local di="${2:-n}"
    local dni="${3:-$di}"
    if [[ "$NON_INTERACTIVE" == true ]]; then
        echo "[i] Неинтерактивный режим: '${prompt}' → ${dni} (авто)"
        [[ "$dni" == "y" ]]
        return
    fi
    local reply=""
    read -p "$prompt " -n 1 -r reply || true
    echo
    [[ -z "$reply" ]] && reply="$di"
    [[ "$reply" =~ ^[Yy]$ ]]
}

# === Определение архитектуры ===
detect_arch() {
    local machine
    machine="$(uname -m)"
    case "$machine" in
        x86_64|amd64) DEB_ARCH="amd64";  RPM_ARCH="x86_64";  APPIMAGE_ARCH="x86_64" ;;
        aarch64|arm64) DEB_ARCH="arm64"; RPM_ARCH="aarch64"; APPIMAGE_ARCH="aarch64" ;;
        armv7l|armhf)  DEB_ARCH="armhf"; RPM_ARCH="armv7hl"; APPIMAGE_ARCH="armhf" ;;
        i686|i386)     DEB_ARCH="i386";  RPM_ARCH="i686";    APPIMAGE_ARCH="i686" ;;
        *) echo "[!] Неизвестная архитектура: $machine" >&2; exit 1 ;;
    esac
    echo "[i] Архитектура: $machine → deb=$DEB_ARCH, rpm=$RPM_ARCH, appimage=$APPIMAGE_ARCH"
}

# === Определение дистрибутива ===
detect_distro() {
    if [ -n "${DISTRO:-}" ]; then
        echo "[i] DISTRO задан извне: $DISTRO"
        return 0
    fi
    if [ -r /etc/os-release ]; then
        . /etc/os-release
        local id="${ID:-unknown}"
        local ver="${VERSION_ID:-}"
        if [ -n "$ver" ]; then DISTRO="${id}-${ver}"; else DISTRO="$id"; fi
    else
        DISTRO="unknown"
    fi
    echo "[i] DISTRO определён локально: $DISTRO"
}

# Пакетный менеджер текущей системы
detect_pkgmgr() {
    if [ -n "${DISTRO:-}" ]; then
        case "$DISTRO" in
            ubuntu-*|debian-*|linuxmint-*|pop-*) echo "deb" ;;
            fedora-*|rocky-*|rhel-*|centos-*|almalinux-*|opensuse*|sles*) echo "rpm" ;;
            arch|arch-*|manjaro*|endeavouros*) echo "arch" ;;
            *) echo "unknown" ;;
        esac
    else
        echo "unknown"
    fi
}

# ---- Чтение версий из install/ ----
read_versions_from_install() {
    local INSTALL_DIR="$SCRIPT_DIR/install"
    local VERSION_FILE_PATH="$INSTALL_DIR/VERSION"
    local VERSION_FULL_PATH="$INSTALL_DIR/VERSION_FULL"
    local VERSION_FILE_NAME_PATH="$INSTALL_DIR/VERSION_FILE"

    if [ ! -f "$VERSION_FILE_PATH" ] || [ ! -f "$VERSION_FULL_PATH" ] || [ ! -f "$VERSION_FILE_NAME_PATH" ]; then
        echo "[ERROR] Файлы версий не найдены в $INSTALL_DIR." >&2
        return 1
    fi
    local VERSION VERSION_FULL VERSION_FILE
    VERSION=$(tr -d '\n\r' < "$VERSION_FILE_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VERSION_FULL=$(tr -d '\n\r' < "$VERSION_FULL_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VERSION_FILE=$(tr -d '\n\r' < "$VERSION_FILE_NAME_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    [ -z "$VERSION" ] || [ -z "$VERSION_FULL" ] || [ -z "$VERSION_FILE" ] && return 1
    printf '%s\n%s\n%s\n' "$VERSION_FULL" "$VERSION_FILE" "$VERSION"
}

# === Проверка зависимостей ===
check_deps() {
    local need_native_deb=$1
    local need_native_rpm=$2
    local need_native_arch=$3
    local need_bundle_deb=$4
    local need_bundle_rpm=$5
    local need_appimage=$6

    local required=()
    local optional=()

    for tool in gpg tar; do
        command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
    done

    if [[ "$need_native_deb" == true || "$need_bundle_deb" == true ]]; then
        for tool in dpkg-deb; do
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
        done
    fi
    if [[ "$need_native_deb" == true ]]; then
        command -v dpkg-shlibdeps >/dev/null 2>&1 || required+=("dpkg-shlibdeps")
    fi
    if [[ "$need_native_rpm" == true || "$need_bundle_rpm" == true ]]; then
        for tool in rpmbuild rpm; do
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
        done
    fi
    if [[ "$need_native_arch" == true ]]; then
        command -v makepkg >/dev/null 2>&1 || required+=("makepkg")
    fi
    if [[ "$need_appimage" == true || "$need_bundle_deb" == true || "$need_bundle_rpm" == true ]]; then
        command -v wget >/dev/null 2>&1 || required+=("wget")
    fi

    if [[ "$need_native_deb" == true || "$need_bundle_deb" == true ]] && ! command -v debsigs >/dev/null 2>&1; then
        optional+=("debsigs")
    fi
    if [[ "$need_appimage" == true ]] && ! command -v zsyncmake >/dev/null 2>&1; then
        optional+=("zsyncmake")
    fi

    if [ ${#required[@]} -ne 0 ]; then
        echo "[!] Не хватает обязательных инструментов: ${required[*]}"
        exit 1
    fi
    if [ ${#optional[@]} -ne 0 ]; then
        echo "[i] Опциональные инструменты не найдены: ${optional[*]}"
        for tool in "${optional[@]}"; do
            case "$tool" in
                debsigs)   echo "    → .deb не будет подписан (sudo apt install debsigs)" ;;
                zsyncmake) echo "    → .zsync не будет сгенерирован (sudo apt install zsync)" ;;
            esac
        done
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
    local BIN_PATH="$SCRIPT_DIR/install/bin/$PACKAGE_NAME"
    if [ -f "$BIN_PATH" ] && [ "$FORCE_REBUILD" = false ]; then
        echo "[+] Бинарник уже собран: $BIN_PATH"
        if ask "Использовать существующий? [Y/n]:" y; then
            echo "[+] Используем существующий бинарник."
            return 0
        fi
        FORCE_REBUILD=true
    fi
    echo "[+] Сборка через $BUILD_RELEASE_SCRIPT..."
    [ ! -f "$BUILD_RELEASE_SCRIPT" ] && { echo "[!] $BUILD_RELEASE_SCRIPT не найден."; exit 1; }
    "$BUILD_RELEASE_SCRIPT" --type release --prefix "$SCRIPT_DIR/install" --yes
    echo "[+] Бинарник собран."
}

# === Подготовка STAGING_DIR для нативных пакетов ===
prepare_staging() {
    local BIN_PATH="$SCRIPT_DIR/install/bin/$PACKAGE_NAME"
    if [ ! -d "$SCRIPT_DIR/install/bin" ] || [ ! -f "$BIN_PATH" ]; then
        echo "[!] Бинарник не найден."; exit 1
    fi

    mkdir -p "$STAGING_DIR/usr/bin"
    cp "$BIN_PATH" "$STAGING_DIR/usr/bin/"

    mkdir -p "$STAGING_DIR/usr/share/$PACKAGE_NAME"
    cp -r "$SCRIPT_DIR/install/share/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/$PACKAGE_NAME/" 2>/dev/null || true

    mkdir -p "$STAGING_DIR/usr/share/applications"
    cat > "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME %F
Icon=${ICON_NAME%.svg}
Type=Application
Categories=AudioVideo;
Comment=IPTV Playlist Player
Terminal=false
StartupNotify=true
MimeType=video/mp4;video/x-matroska;video/avi;video/mpeg;video/quicktime;video/x-msvideo;video/x-flv;video/ogg;video/webm;application/x-mpegURL;audio/x-mpegurl;audio/x-scpls;application/xspf+xml;application/vnd.apple.mpegurl;
EOF

    mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
    cp "$SCRIPT_DIR/install/share/$PACKAGE_NAME/icons/$ICON_NAME" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" 2>/dev/null || true

    if [ -f "$SCRIPT_DIR/install/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/metainfo"
        cp "$SCRIPT_DIR/install/share/metainfo/$METAINFO_NAME" "$STAGING_DIR/usr/share/metainfo/"
    fi

    if [ -d "$SCRIPT_DIR/install/share/doc/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME"
        cp -r "$SCRIPT_DIR/install/share/doc/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME/" 2>/dev/null || true
    fi
    if [ -d "$SCRIPT_DIR/install/share/licenses/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME"
        cp -r "$SCRIPT_DIR/install/share/licenses/$PACKAGE_NAME/"* "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME/" 2>/dev/null || true
    fi
}

# === Очистка ===
cleanup() {
    echo "[+] Очистка временных каталогов..."
    rm -rf "$STAGING_DIR" "$APPDIR" "$SCRIPT_DIR/pkg-rpm"
}
trap cleanup EXIT

# =============================================================================
#                         НАПОЛНЕНИЕ APPDIR (linuxdeploy)
# =============================================================================
# Один раз наполняем $APPDIR. Результат переиспользуется для AppImage и
# для bundled-пакетов.
populate_appdir() {
    if [[ "$APPIMAGE_ARCH" != "x86_64" && "$APPIMAGE_ARCH" != "aarch64" ]]; then
        echo "[!] Bundled/AppImage не поддерживаются для '$APPIMAGE_ARCH'."
        return 1
    fi

    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" \
             "$APPDIR/usr/share/icons/hicolor/scalable/apps"

    # Готовим staging, если его нет
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        prepare_staging
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
        wget -q --show-progress \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" \
            -O "$LINUXDEPLOY"
        chmod +x "$LINUXDEPLOY"
    fi
    if [ ! -f "$GTK_PLUGIN" ]; then
        echo "[+] Скачивание GTK-плагина (скрипт)..."
        wget -q --show-progress \
            "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" \
            -O "$GTK_PLUGIN"
        chmod +x "$GTK_PLUGIN"
    fi

    echo "[+] Наполнение AppDir через linuxdeploy..."
    if APPIMAGE_EXTRACT_AND_RUN=1 DEPLOY_GTK_VERSION=3 ARCH="$APPIMAGE_ARCH" "$LINUXDEPLOY" \
        --appdir="$APPDIR" \
        --plugin gtk \
        --desktop-file="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop"; then
        echo "[✓] AppDir наполнен."
    else
        echo "[!] Ошибка linuxdeploy."
        exit 1
    fi
}

# =============================================================================
#                              APPI MAGE
# =============================================================================
build_appimage() {
    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}.AppImage"

    echo "[+] Создание AppImage ($APPIMAGE_ARCH)..."

    # Наполнить AppDir, если ещё не наполнен
    if [ ! -d "$APPDIR/usr/lib" ]; then
        populate_appdir || return 1
    fi

    local LINUXDEPLOY="$SCRIPT_DIR/linuxdeploy-${APPIMAGE_ARCH}.AppImage"

    echo "[+] Упаковка AppDir в AppImage..."
    if APPIMAGE_EXTRACT_AND_RUN=1 ARCH="$APPIMAGE_ARCH" "$LINUXDEPLOY" \
        --appdir="$APPDIR" \
        --output=appimage; then
        echo "[✓] AppImage собран."
    else
        echo "[!] Ошибка упаковки AppImage."; exit 1
    fi

    local found
    found=$(find "$SCRIPT_DIR" -maxdepth 1 -name "*-${APPIMAGE_ARCH}.AppImage" ! -name "linuxdeploy*" -print -quit)
    if [ -n "$found" ]; then
        mv "$found" "$appimage_file"
        echo "[✓] AppImage: $appimage_file"
    else
        echo "[!] AppImage не найден."; exit 1
    fi

    if command -v zsyncmake >/dev/null; then
        zsyncmake "$appimage_file" -o "$(basename "$appimage_file" .AppImage).zsync"
    fi
}

# =============================================================================
#                       BUNDLED: staging + .deb + .rpm
# =============================================================================
build_bundled_stage() {
    # Наполнить AppDir, если ещё не наполнен
    if [ ! -d "$APPDIR/usr/lib" ]; then
        populate_appdir || return 1
    fi

    rm -rf "$STAGING_DIR"
    mkdir -p "$STAGING_DIR/opt" \
             "$STAGING_DIR/usr/bin" \
             "$STAGING_DIR/usr/share/applications" \
             "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"

    # Весь AppDir целиком — в /opt/iptvplayer
    cp -a "$APPDIR" "$STAGING_DIR${BUNDLE_PREFIX}"

    # Wrapper в /usr/bin
    cat > "$STAGING_DIR/usr/bin/$PACKAGE_NAME" <<EOF
#!/bin/bash
exec ${BUNDLE_PREFIX}/AppRun "\$@"
EOF
    chmod 755 "$STAGING_DIR/usr/bin/$PACKAGE_NAME"

    # .desktop в системный каталог
    cat > "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME %F
Icon=${ICON_NAME%.svg}
Type=Application
Categories=AudioVideo;
Comment=IPTV Playlist Player
Terminal=false
StartupNotify=true
MimeType=video/mp4;video/x-matroska;video/avi;video/mpeg;video/quicktime;video/x-msvideo;video/x-flv;video/ogg;video/webm;application/x-mpegURL;audio/x-mpegurl;audio/x-scpls;application/xspf+xml;application/vnd.apple.mpegurl;
EOF

    cp "$APPDIR/$ICON_NAME" "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" 2>/dev/null || true

    # metainfo
    if [ -f "$APPDIR/usr/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/metainfo"
        cp "$APPDIR/usr/share/metainfo/$METAINFO_NAME" "$STAGING_DIR/usr/share/metainfo/"
    fi
}

# ---- Bundled .deb ----
build_deb_bundled() {
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${DEB_ARCH}.deb"
    echo "[+] Создание bundled .deb..."
    build_bundled_stage || return 1

    mkdir -p "$STAGING_DIR/DEBIAN"
    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: network
Priority: optional
Architecture: $DEB_ARCH
Depends: libc6 (>= 2.39), libstdc++6, libgcc-s1, libgl1, libegl1
Maintainer: ijurij <mnisjil@duck.com>
Homepage: https://github.com/i-jurij/$PACKAGE_NAME
Description: IPTV Playlist Player (bundled)
 Self-contained build with all libraries in $BUNDLE_PREFIX.
EOF

    cat > "$STAGING_DIR/DEBIAN/postinst" << 'EOF'
#!/bin/bash
set -e
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
if [ -x /usr/bin/update-desktop-database ]; then
    /usr/bin/update-desktop-database -q || true
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/postinst"

    cat > "$STAGING_DIR/DEBIAN/prerm" << EOF
#!/bin/bash
set -e
if [ \$1 = "remove" ] || [ \$1 = "purge" ]; then
    rm -f "/usr/share/applications/$PACKAGE_NAME.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/prerm"

    chmod -R 755 "$STAGING_DIR/usr" 2>/dev/null || true
    chmod 755 "$STAGING_DIR/DEBIAN"
    dpkg-deb -Zxz --build --root-owner-group "$STAGING_DIR" "$deb_file"
    echo "[✓] Bundled .deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
}

# ---- Bundled .rpm ----
build_rpm_bundled() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${release}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$SCRIPT_DIR/pkg-rpm"
    echo "[+] Создание bundled .rpm..."
    build_bundled_stage || return 1

    mkdir -p "$SPEC_DIR/SOURCES"
    tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
        --transform="s,^,$PACKAGE_NAME-$VERSION/," \
        -C "$STAGING_DIR" .

    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir $SPEC_DIR
%define _binary_payload w2.xzdio
AutoReqProv: no
Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $release
Summary:        IPTV Playlist Player (bundled)
License:        MIT
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      $RPM_ARCH

Requires:       glibc >= 2.39
Requires:       libstdc++
Requires:       libgcc
Requires:       mesa-libGL
Requires:       mesa-libEGL

%description
Self-contained build with all libraries in $BUNDLE_PREFIX.

%prep
%setup -q

%build
# already built

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
tar -xzf %{_sourcedir}/%{SOURCE0} -C $RPM_BUILD_ROOT --strip-components=1

%files
${BUNDLE_PREFIX}/
/usr/bin/$PACKAGE_NAME
/usr/share/applications/$PACKAGE_NAME.desktop
/usr/share/icons/hicolor/scalable/apps/$ICON_NAME
/usr/share/metainfo/$METAINFO_NAME

%post
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
if [ -x /usr/bin/update-desktop-database ]; then
    /usr/bin/update-desktop-database -q || true
fi

%preun
if [ \$1 = 0 ]; then
    rm -f "/usr/share/applications/$PACKAGE_NAME.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
    rm -f "/usr/bin/$PACKAGE_NAME"
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$release
- Initial build
EOF

    rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"
    mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"
    echo "[✓] Bundled .rpm: $rpm_file"
}

# =============================================================================
#                         НАТИВНЫЕ .deb / .rpm / .pkg.tar.zst
# =============================================================================
detect_deb_depends() {
    local bin_path="$1"
    local cached="$SCRIPT_DIR/install/DEB_DEPENDS"
    if [ -f "$cached" ]; then
        local d; d=$(tr -d '\n\r' < "$cached")
        [ -n "$d" ] && { echo "$d"; return 0; }
    fi
    local tmp_dir; tmp_dir=$(mktemp -d)
    mkdir -p "$tmp_dir/debian"
    cat > "$tmp_dir/debian/control" << EOF
Source: $PACKAGE_NAME
Package: $PACKAGE_NAME
Architecture: any
EOF
    local output
    output=$(cd "$tmp_dir" && dpkg-shlibdeps -O "$bin_path" 2>&1) || true
    rm -rf "$tmp_dir"
    echo "$output" | sed -n 's/^shlibs:Depends=//p' | tr -d '\n\r'
}

build_deb_native() {
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_native_${DEB_ARCH}.deb"
    echo "[+] Создание нативного .deb..."
    [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ] && prepare_staging
    mkdir -p "$STAGING_DIR/DEBIAN"

    local depends
    depends=$(detect_deb_depends "$STAGING_DIR/usr/bin/$PACKAGE_NAME")
    [ -z "$depends" ] && { echo "[!] не удалось определить Depends"; exit 1; }
    echo "[+] Depends: $depends"

    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: network
Priority: optional
Architecture: $DEB_ARCH
Depends: $depends
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

    cat > "$STAGING_DIR/DEBIAN/prerm" << EOF
#!/bin/bash
set -e
if [ \$1 = "remove" ] || [ \$1 = "purge" ]; then
    rm -f "/usr/share/applications/$PACKAGE_NAME.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/prerm"

    chmod -R 755 "$STAGING_DIR/usr"
    chmod 755 "$STAGING_DIR/DEBIAN"
    dpkg-deb -Zxz --build --root-owner-group "$STAGING_DIR" "$deb_file"
    echo "[✓] Нативный .deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
}

build_rpm_native() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${release}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$SCRIPT_DIR/pkg-rpm"
    echo "[+] Создание нативного .rpm..."
    [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ] && prepare_staging

    mkdir -p "$SPEC_DIR/SOURCES"
    cd "$STAGING_DIR" && tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
        --transform="s,^,$PACKAGE_NAME-$VERSION/," . && cd - > /dev/null

    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir $SPEC_DIR
Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $release
Summary:        IPTV Playlist Player
License:        MIT
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      $RPM_ARCH

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
%{_bindir}/$PACKAGE_NAME
%{_datadir}/$PACKAGE_NAME/
%{_datadir}/applications/$PACKAGE_NAME.desktop
%{_datadir}/icons/hicolor/scalable/apps/$ICON_NAME
%{_datadir}/metainfo/$METAINFO_NAME
%{_datadir}/doc/$PACKAGE_NAME/copyright
%{_datadir}/licenses/$PACKAGE_NAME/LICENSE

%post
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$release
- Initial build
EOF

    rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"
    mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"
    echo "[✓] Нативный .rpm: $rpm_file"
}

build_pkg_arch() {
    # На не-Arch системах этот путь недоступен (меню его не предлагает).
    # Явный вызов --native-arch на Ubuntu/Debian — ошибка пользователя.
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg не найден. Сборка .pkg.tar.zst возможна только на Arch/Manjaro."
        return 1
    fi

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-1-${APPIMAGE_ARCH}.pkg.tar.zst"
    echo "[+] Создание нативного .pkg.tar.zst через makepkg..."

    [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ] && prepare_staging

    local workdir; workdir="$(mktemp -d)"
    cd "$workdir"

    cat > PKGBUILD <<EOF
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=1
pkgdesc="IPTV Playlist Player"
arch=('$APPIMAGE_ARCH')
url="https://github.com/i-jurij/$PACKAGE_NAME"
license=('MIT')
depends=('mpv' 'gtk3' 'curl' 'expat' 'zlib')

package() {
    cp -a "$STAGING_DIR/usr" "\$pkgdir/"
}
EOF

    makepkg -f --nodeps --nocheck

    local built
    built=$(find "$workdir" -maxdepth 1 -name "*.pkg.tar.zst" -print -quit)
    if [ -n "$built" ]; then
        mv "$built" "$pkg_file"
        echo "[✓] Нативный .pkg.tar.zst: $pkg_file"
    else
        echo "[!] makepkg не создал пакет"
        cd "$SCRIPT_DIR"; rm -rf "$workdir"; return 1
    fi

    cd "$SCRIPT_DIR"
    rm -rf "$workdir"
}

# =============================================================================
#                               ПОДПИСЬ
# =============================================================================
sign_files() {
    local dist_dir="$OUTPUT_DIR"
    local checksum_basename="checksums.txt"
    local signature_basename="${checksum_basename}.asc"
    local checksum_file="$dist_dir/$checksum_basename"
    local signature_file="$dist_dir/$signature_basename"

    local can_sign=false
    if ! command -v gpg >/dev/null 2>&1; then
        echo "[!] gpg не установлен."
    elif [ -z "${GPG_KEY_ID:-}" ]; then
        if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
            echo "::warning::GPG_KEY_ID не задан."
        else
            echo "[!] GPG_KEY_ID не задан."
        fi
    else
        can_sign=true
    fi

    if [ "$can_sign" = true ]; then
        if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -x "$GPG_WRAPPER_DIR/gpg" ]; then
            export PATH="$GPG_WRAPPER_DIR:$PATH"
        fi
        echo "[+] Подпись пакетов (ключ: $GPG_KEY_ID)..."

        # .deb через debsigs
        if command -v debsigs >/dev/null 2>&1; then
            for file in "$dist_dir"/*.deb; do
                [ -f "$file" ] || continue
                debsigs --sign=origin --default-key="$GPG_KEY_ID" "$file" \
                  || gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" || true
            done
        fi

        # .rpm через rpm --addsign
        if command -v rpm >/dev/null 2>&1; then
            touch "$HOME/.rpmmacros"
            grep -q "^%_gpg_name" "$HOME/.rpmmacros" \
              && sed -i "s|^%_gpg_name.*|%_gpg_name $GPG_KEY_ID|" "$HOME/.rpmmacros" \
              || echo "%_gpg_name $GPG_KEY_ID" >> "$HOME/.rpmmacros"
            grep -q "^%_signature" "$HOME/.rpmmacros" || echo "%_signature gpg" >> "$HOME/.rpmmacros"

            if [ -n "${GPG_WRAPPER_DIR:-}" ]; then
                grep -q "^%__gpg_sign_cmd" "$HOME/.rpmmacros" || cat >> "$HOME/.rpmmacros" <<'EOF'
%__gpg gpg
%__gpg_check_password_cmd /bin/true
%__gpg_sign_cmd %{__gpg} --batch --pinentry-mode loopback --passphrase '' -u "%{_gpg_name}" -sbo %{__signature_filename} %{__plaintext_filename}
EOF
            fi

            for file in "$dist_dir"/*.rpm; do
                [ -f "$file" ] || continue
                rpm --addsign "$file" 2>/dev/null \
                  || gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" || true
            done
        fi

        # AppImage (detached)
        for file in "$dist_dir"/*.AppImage; do
            [ -f "$file" ] || continue
            gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" || true
        done
    fi

    # checksums
    echo "[+] Генерация checksums.txt..."
    rm -f "$checksum_file" "$signature_file"
    (
        cd "$dist_dir" || exit 1
        shopt -s nullglob
        files=()
        for f in *; do
            case "$f" in "$checksum_basename"|"$signature_basename") continue ;; esac
            files+=("$f")
        done
        (( ${#files[@]} > 0 )) && sha256sum "${files[@]}"
    ) > "$checksum_file" || true

    if [ "$can_sign" = true ]; then
        gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" \
            --output "$signature_file" "$checksum_file" || true
    fi
}

# =============================================================================
#                              МЕНЮ / СПРАВКА
# =============================================================================
show_help() {
    cat << EOF
Использование: ./build-package.sh [ОПЦИИ]

Нативные:
  --native-deb      .deb из системных библиотек (Ubuntu/Debian)
  --native-rpm      .rpm из системных библиотек (Fedora/Rocky/RHEL/openSUSE)
  --native-arch     .pkg.tar.zst (Arch/Manjaro)
  --appimage        AppImage (bundled, работает везде)

Bundled (обычно только для CI):
  --bundle-deb      bundled .deb (всё в /opt/iptvplayer)
  --bundle-rpm      bundled .rpm

Комбинированные:
  --native          все нативные, доступные здесь
  --native-appimage нативные + AppImage
  --bundle          bundled .deb + bundled .rpm
  --all             всё возможное на этой системе

Служебные:
  --rebuild         пересобрать бинарник
  --clean           очистить dist/
  --clean-only      только очистить dist/
  --no-menu         не показывать меню
  --yes, -y         неинтерактивный
  -h, --help        эта справка
EOF
}

# === Интерактивное меню ===
show_menu() {
    local pkgmgr="$1"
    echo ""
    echo "Обнаружена система: $DISTRO ($(uname -m))"
    echo ""
    echo "Выберите, что собрать:"

    local options=()
    case "$pkgmgr" in
        deb) options+=("Нативный .deb (системные библиотеки)") ;;
        rpm) options+=("Нативный .rpm (системные библиотеки)") ;;
        arch) options+=("Нативный .pkg.tar.zst (системные библиотеки)") ;;
    esac
    options+=("AppImage (bundled, работает везде)")
    options+=("Всё возможное на этой системе")

    local i=1
    for opt in "${options[@]}"; do
        echo "  $i) $opt"
        i=$((i+1))
    done
    echo "  0) Отмена"
    echo ""
    read -p "Введите номер: " choice

    case "$choice" in
        1)
            case "$pkgmgr" in
                deb)  BUILD_NATIVE_DEB=true ;;
                rpm)  BUILD_NATIVE_RPM=true ;;
                arch) BUILD_NATIVE_ARCH=true ;;
                *)    BUILD_APPIMAGE=true ;;
            esac
            ;;
        2)
            case "$pkgmgr" in
                deb|rpm|arch) BUILD_APPIMAGE=true ;;
                *)            BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true; BUILD_APPIMAGE=true ;;
            esac
            ;;
        3)
            BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true; BUILD_APPIMAGE=true
            ;;
        0) exit 0 ;;
        *) echo "Неверный выбор"; exit 1 ;;
    esac
}

# =============================================================================
#                              MAIN
# =============================================================================
main() {
    BUILD_NATIVE_DEB=false
    BUILD_NATIVE_RPM=false
    BUILD_NATIVE_ARCH=false
    BUILD_BUNDLE_DEB=false
    BUILD_BUNDLE_RPM=false
    BUILD_APPIMAGE=false
    SHOW_MENU=true

    while [[ $# -gt 0 ]]; do
        case "$1" in
            --native-deb)    BUILD_NATIVE_DEB=true ;;
            --native-rpm)    BUILD_NATIVE_RPM=true ;;
            --native-arch)   BUILD_NATIVE_ARCH=true ;;
            --bundle-deb)    BUILD_BUNDLE_DEB=true ;;
            --bundle-rpm)    BUILD_BUNDLE_RPM=true ;;
            --appimage)      BUILD_APPIMAGE=true ;;
            --native)        BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true ;;
            --native-appimage) BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true; BUILD_APPIMAGE=true ;;
            --bundle)        BUILD_BUNDLE_DEB=true; BUILD_BUNDLE_RPM=true ;;
            --all)           BUILD_NATIVE_DEB=true; BUILD_NATIVE_RPM=true; BUILD_NATIVE_ARCH=true; BUILD_BUNDLE_DEB=true; BUILD_BUNDLE_RPM=true; BUILD_APPIMAGE=true ;;
            --rebuild)       FORCE_REBUILD=true ;;
            --clean)         DO_CLEAN=true ;;
            --clean-only)    DO_CLEAN=true; CLEAN_ONLY=true ;;
            --no-menu)       SHOW_MENU=false ;;
            --yes|-y)        NON_INTERACTIVE=true; SHOW_MENU=false ;;
            -h|--help)       show_help; exit 0 ;;
            *) echo "Неизвестный аргумент: $1"; show_help; exit 1 ;;
        esac
        shift
    done

    if [[ "$CLEAN_ONLY" == true ]]; then
        echo "[+] Очистка $OUTPUT_DIR..."
        rm -rf "$OUTPUT_DIR"/*
        mkdir -p "$OUTPUT_DIR"
        exit 0
    fi

    detect_arch
    detect_distro

    local pkgmgr; pkgmgr="$(detect_pkgmgr)"
    echo "[i] Пакетный менеджер: $pkgmgr"

    # Если ничего не выбрано и меню разрешено — показать меню
    if [[ "$BUILD_NATIVE_DEB" == false && "$BUILD_NATIVE_RPM" == false && \
          "$BUILD_NATIVE_ARCH" == false && "$BUILD_BUNDLE_DEB" == false && \
          "$BUILD_BUNDLE_RPM" == false && "$BUILD_APPIMAGE" == false ]]; then
        if [[ "$SHOW_MENU" == true && "$NON_INTERACTIVE" == false ]]; then
            show_menu "$pkgmgr"
        else
            # В CI или --no-menu без флагов — по умолчанию bundle + appimage
            BUILD_BUNDLE_DEB=true
            BUILD_BUNDLE_RPM=true
            BUILD_APPIMAGE=true
        fi
    fi

    # Bundled требует AppDir — если запрошены bundle и AppImage, populate один раз
    check_deps "$BUILD_NATIVE_DEB" "$BUILD_NATIVE_RPM" "$BUILD_NATIVE_ARCH" \
               "$BUILD_BUNDLE_DEB" "$BUILD_BUNDLE_RPM" "$BUILD_APPIMAGE"
    setup_dirs
    build_binary

    { read -r VERSION_DISPLAY; read -r VERSION_FILE; read -r VERSION; } \
        < <(read_versions_from_install)
    [ -z "$VERSION_DISPLAY" ] || [ -z "$VERSION_FILE" ] || [ -z "$VERSION" ] && {
        echo "[ERROR] Не удалось прочитать версии."; exit 1
    }

    echo "=== $PACKAGE_NAME:$VERSION (файл: $VERSION_FILE, arch: $DEB_ARCH/$RPM_ARCH/$APPIMAGE_ARCH, distro: $DISTRO) ==="

    if [[ "$BUILD_NATIVE_DEB" == true ]]; then
        if [[ "$pkgmgr" == "deb" ]]; then
            prepare_staging; build_deb_native
        else
            echo "[!] --native-deb недоступен на $DISTRO — пропускаем."
        fi
    fi
    if [[ "$BUILD_NATIVE_RPM" == true ]]; then
        if [[ "$pkgmgr" == "rpm" ]]; then
            prepare_staging; build_rpm_native
        else
            echo "[!] --native-rpm недоступен на $DISTRO — пропускаем."
        fi
    fi
    if [[ "$BUILD_NATIVE_ARCH" == true ]]; then
        if [[ "$pkgmgr" == "arch" ]]; then
            prepare_staging; build_pkg_arch
        else
            echo "[!] --native-arch недоступен на $DISTRO — пропускаем."
        fi
    fi

    if [[ "$BUILD_BUNDLE_DEB" == true ]]; then build_deb_bundled; fi
    if [[ "$BUILD_BUNDLE_RPM" == true ]]; then build_rpm_bundled; fi
    if [[ "$BUILD_APPIMAGE" == true ]]; then build_appimage; fi

    sign_files
    #cleanup

    echo ""
    echo "🎉 Готово! Артефакты в '$OUTPUT_DIR':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"