#!/bin/bash
# =============================================
# build-package.sh – Сборка пакетов .deb, .rpm, .AppImage
#
# Использование:
#   ./build-package.sh [ОПЦИИ]
#
# Опции:
#   --deb         Собрать только .deb
#   --rpm         Собрать только .rpm
#   --appimage    Собрать только AppImage
#   --all         Собрать все типы пакетов (по умолчанию)
#   --rebuild     Принудительно пересобрать бинарник (даже если уже есть)
#   --clean       Очистить каталог dist/ перед сборкой
#   --clean-only  Только очистить dist/ и завершить работу
#   --yes, -y     Неинтерактивный режим (авто-ответы, для CI)
#   -h, --help    Показать эту справку
#
# Версия:
#   Определяется CMake при сборке бинарника (из корневого VERSION + git-хеш).
#   После сборки бинарника версия для пакетов читается из install/.
#
# Архитектура:
#   Определяется автоматически через `uname -m`. Поддерживаются
#   x86_64/amd64, aarch64/arm64, armv7l/armhf, i686/i386.
#   Для AppImage доступны только x86_64 и aarch64 (linuxdeploy не
#   публикует armhf/i686 сборки).
#
# Примеры:
#   ./build-package.sh --deb                # собрать .deb
#   ./build-package.sh --all                # собрать все пакеты
#   ./build-package.sh --rebuild --deb      # пересобрать бинарник и .deb
#   ./build-package.sh --clean --all        # очистить dist и собрать всё
#   ./build-package.sh --clean-only         # только очистить dist
#   ./build-package.sh -h                   # показать эту справку
#   ./build-package.sh --yes --all          # неинтерактивная сборка (CI)
# =============================================

set -e

# ---- Определение корня проекта ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# === Настройки ===
PACKAGE_NAME="iptvplayer"
ICON_NAME="${PACKAGE_NAME}.svg"

# === Архитектура (заполняется в detect_arch) ===
DEB_ARCH=""
RPM_ARCH=""
APPIMAGE_ARCH=""

# AppStream-метаданные: имя файла хранится в корневом METAINFO_NAME
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

# Спросить пользователя. Возвращает 0 для "да", 1 для "нет".
# $1 — вопрос
# $2 — default в интерактивном режиме ("y"/"n")
# $3 — default в неинтерактивном режиме (по умолчанию совпадает с $2)
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
# Заполняет DEB_ARCH, RPM_ARCH, APPIMAGE_ARCH по `uname -m`.
detect_arch() {
    local machine
    machine="$(uname -m)"
    case "$machine" in
        x86_64|amd64)
            DEB_ARCH="amd64"
            RPM_ARCH="x86_64"
            APPIMAGE_ARCH="x86_64"
            ;;
        aarch64|arm64)
            DEB_ARCH="arm64"
            RPM_ARCH="aarch64"
            APPIMAGE_ARCH="aarch64"
            ;;
        armv7l|armhf)
            DEB_ARCH="armhf"
            RPM_ARCH="armv7hl"
            APPIMAGE_ARCH="armhf"
            ;;
        i686|i386)
            DEB_ARCH="i386"
            RPM_ARCH="i686"
            APPIMAGE_ARCH="i686"
            ;;
        *)
            echo "[!] Неизвестная архитектура: $machine" >&2
            echo "    Поддерживаются: x86_64, aarch64, armv7l, i686." >&2
            exit 1
            ;;
    esac
    echo "[i] Архитектура: $machine → deb=$DEB_ARCH, rpm=$RPM_ARCH, appimage=$APPIMAGE_ARCH"
}

# ---- Чтение версий из install/ ----
read_versions_from_install() {
    local INSTALL_DIR="$SCRIPT_DIR/install"
    local VERSION_FILE_PATH="$INSTALL_DIR/VERSION"
    local VERSION_FULL_PATH="$INSTALL_DIR/VERSION_FULL"
    local VERSION_FILE_NAME_PATH="$INSTALL_DIR/VERSION_FILE"

    if [ ! -f "$VERSION_FILE_PATH" ] || [ ! -f "$VERSION_FULL_PATH" ] || [ ! -f "$VERSION_FILE_NAME_PATH" ]; then
        echo "[ERROR] Файлы версий не найдены в $INSTALL_DIR." >&2
        echo "Убедитесь, что бинарник собран и установлен (./build-release.sh)." >&2
        return 1
    fi

    local VERSION VERSION_FULL VERSION_FILE
    VERSION=$(tr -d '\n\r' < "$VERSION_FILE_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VERSION_FULL=$(tr -d '\n\r' < "$VERSION_FULL_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VERSION_FILE=$(tr -d '\n\r' < "$VERSION_FILE_NAME_PATH" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')

    if [ -z "$VERSION" ] || [ -z "$VERSION_FULL" ] || [ -z "$VERSION_FILE" ]; then
        echo "[ERROR] Один из файлов версий пуст." >&2
        return 1
    fi

    printf '%s\n%s\n%s\n' "$VERSION_FULL" "$VERSION_FILE" "$VERSION"
}

# === Проверка зависимостей ===
# Проверяет только те инструменты, которые реально нужны для
# запрошенных типов пакетов. Иначе RPM-контейнеры падают на
# отсутствии dpkg-*, а DEB — на отсутствии rpmbuild.
check_deps() {
    local need_deb=$1
    local need_rpm=$2
    local need_appimage=$3

    local required=()
    local optional=()

    # --- Общие ---
    for tool in gpg; do
        command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
    done

    # --- .deb ---
    if [[ "$need_deb" == true ]]; then
        for tool in dpkg-deb dpkg-shlibdeps; do
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
        done
    fi

    # --- .rpm ---
    if [[ "$need_rpm" == true ]]; then
        for tool in rpmbuild rpm tar; do
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
        done
    fi

    # --- AppImage ---
    if [[ "$need_appimage" == true ]]; then
        # linuxdeploy скачивается через wget
        for tool in wget; do
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool")
        done
    fi

    # --- Опциональные (только для релевантных типов) ---
    if [[ "$need_deb" == true ]] && ! command -v debsigs >/dev/null 2>&1; then
        optional+=("debsigs")
    fi
    if [[ "$need_appimage" == true ]] && ! command -v zsyncmake >/dev/null 2>&1; then
        optional+=("zsyncmake")
    fi

    if [ ${#required[@]} -ne 0 ]; then
        echo "[!] Не хватает обязательных инструментов: ${required[*]}"
        echo "Установите их вручную или через пакетный менеджер."
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
    if [ ! -f "$BUILD_RELEASE_SCRIPT" ]; then
        echo "[!] $BUILD_RELEASE_SCRIPT не найден. Запустите из корня проекта."
        exit 1
    fi

    # --yes: build-release.sh — вложенный скрипт, все решения уже приняты здесь.
    "$BUILD_RELEASE_SCRIPT" --type release --prefix "$SCRIPT_DIR/install" --yes
    echo "[+] Бинарник собран."
}

# === Подготовка STAGING_DIR ===
prepare_staging() {
    local BIN_PATH="$SCRIPT_DIR/install/bin/$PACKAGE_NAME"
    if [ ! -d "$SCRIPT_DIR/install/bin" ] || [ ! -f "$BIN_PATH" ]; then
        echo "[!] Бинарник не найден. Запустите сборку или укажите --rebuild."
        exit 1
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

    # Копирование AppStream metadata (metainfo.xml)
    if [ -f "$SCRIPT_DIR/install/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/metainfo"
        cp "$SCRIPT_DIR/install/share/metainfo/$METAINFO_NAME" "$STAGING_DIR/usr/share/metainfo/"
        echo "[+] AppStream metadata скопирован."
    else
        echo "[!] $METAINFO_NAME не найден в $SCRIPT_DIR/install/share/metainfo/"
    fi

    # Лицензия — в стандартные места для .deb и .rpm
    if [ -d "$SCRIPT_DIR/install/share/doc/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME"
        cp -r "$SCRIPT_DIR/install/share/doc/$PACKAGE_NAME/"* \
              "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME/" 2>/dev/null || true
    fi
    if [ -d "$SCRIPT_DIR/install/share/licenses/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME"
        cp -r "$SCRIPT_DIR/install/share/licenses/$PACKAGE_NAME/"* \
              "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME/" 2>/dev/null || true
    fi
}

# === Очистка временных каталогов ===
cleanup() {
    echo "[+] Очистка временных каталогов..."
    rm -rf "$STAGING_DIR" "$APPDIR" "$SCRIPT_DIR/pkg-rpm"
}

# Удаляем временные каталоги при любом завершении скрипта
trap cleanup EXIT

# === Автоопределение зависимостей .deb через dpkg-shlibdeps ===
# Возвращает строку для поля Depends: (без префикса "shlibs:Depends=").
# Требует пакет dpkg-dev.
detect_deb_depends() {
    local bin_path="$1"
    local tmp_dir
    tmp_dir=$(mktemp -d)

    # dpkg-shlibdeps требует наличие debian/control в рабочей директории.
    mkdir -p "$tmp_dir/debian"
    cat > "$tmp_dir/debian/control" << EOF
Source: $PACKAGE_NAME
Package: $PACKAGE_NAME
Architecture: any
EOF

    local output
    output=$(cd "$tmp_dir" && dpkg-shlibdeps -O "$bin_path" 2>/dev/null) || true
    rm -rf "$tmp_dir"

    # Извлекаем значение после "shlibs:Depends=" и убираем переносы строк
    echo "$output" | sed -n 's/^shlibs:Depends=//p' | tr -d '\n\r'
}

# === Сборка .deb ===
build_deb() {
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION_FILE}_${DEB_ARCH}.deb"
    echo "[+] Создание .deb (архитектура: $DEB_ARCH)..."
    mkdir -p "$STAGING_DIR/DEBIAN"

    # Определяем зависимости через dpkg-shlibdeps
    echo "[+] Определение зависимостей .deb..."
    local depends
    depends=$(detect_deb_depends "$STAGING_DIR/usr/bin/$PACKAGE_NAME")

    if [ -z "$depends" ]; then
        echo "[!] Не удалось определить зависимости .deb."
        echo "    Убедитесь, что установлен dpkg-dev: sudo apt install dpkg-dev"
        exit 1
    fi
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
    if [ -x /usr/bin/update-icon-caches ]; then
        /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
    fi
fi
EOF
    chmod 755 "$STAGING_DIR/DEBIAN/prerm"

    chmod -R 755 "$STAGING_DIR/usr"
    chmod 755 "$STAGING_DIR/DEBIAN"
    dpkg-deb -Zxz --build --root-owner-group "$STAGING_DIR" "$deb_file"
    echo "[✓] .deb создан: $deb_file"

    rm -rf "$STAGING_DIR/DEBIAN"
}

# === Сборка .rpm ===
build_rpm() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION_FILE}-${release}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$SCRIPT_DIR/pkg-rpm"

    echo "[+] Создание .rpm (архитектура: $RPM_ARCH)..."

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

# Макросы вместо жёстких путей — чтобы rpmbuild подхватил ELF-зависимости
# через AutoReqProv (Requires: libmpv, libcurl, gtk3, ...).
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

%preun
if [ \$1 = 0 ]; then
    rm -f "%{_datadir}/applications/$PACKAGE_NAME.desktop"
    rm -f "%{_datadir}/icons/hicolor/scalable/apps/$ICON_NAME"
    rm -f "%{_bindir}/$PACKAGE_NAME"
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
    echo "[✓] .rpm создан: $rpm_file"
}

# === Создание AppImage ===
build_appimage() {
    # linuxdeploy публикует бинарники только для x86_64 и aarch64.
    if [[ "$APPIMAGE_ARCH" != "x86_64" && "$APPIMAGE_ARCH" != "aarch64" ]]; then
        echo "[!] AppImage не поддерживается для архитектуры '$APPIMAGE_ARCH'."
        echo "    linuxdeploy доступен только для x86_64 и aarch64 — пропускаем."
        return 0
    fi

    local appimage_file="$OUTPUT_DIR/${PACKAGE_NAME}-linux-${APPIMAGE_ARCH}-${VERSION_FILE}.AppImage"

    echo "[+] Создание AppImage (архитектура: $APPIMAGE_ARCH)..."

    # Подготовка AppDir
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/scalable/apps"
    cp "$STAGING_DIR/usr/bin/$PACKAGE_NAME" "$APPDIR/usr/bin/"
    cp -r "$STAGING_DIR/usr/share/$PACKAGE_NAME" "$APPDIR/usr/share/"
    cp "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" "$APPDIR/usr/share/applications/"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" "$APPDIR/$ICON_NAME"
    cp "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

    # AppStream metadata — linuxdeploy сам подхватит из usr/share/metainfo/
    if [ -f "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$APPDIR/usr/share/metainfo"
        cp "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" "$APPDIR/usr/share/metainfo/"
    fi

    local LINUXDEPLOY="$SCRIPT_DIR/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
    local GTK_PLUGIN="$SCRIPT_DIR/linuxdeploy-plugin-gtk.sh"

    # Скачиваем linuxdeploy (если отсутствует)
    if [ ! -f "$LINUXDEPLOY" ]; then
        echo "[+] Скачивание linuxdeploy ($APPIMAGE_ARCH)..."
        wget -q --show-progress \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" \
            -O "$LINUXDEPLOY"
        chmod +x "$LINUXDEPLOY"
    fi

    # Скачиваем GTK-плагин (скрипт) (если отсутствует)
    if [ ! -f "$GTK_PLUGIN" ]; then
        echo "[+] Скачивание GTK-плагина (скрипт)..."
        wget -q --show-progress "https://raw.githubusercontent.com/linuxdeploy/linuxdeploy-plugin-gtk/master/linuxdeploy-plugin-gtk.sh" -O "$GTK_PLUGIN"
        chmod +x "$GTK_PLUGIN"
    fi

    echo "[+] Запуск linuxdeploy с GTK-плагином..."
    if ARCH="$APPIMAGE_ARCH" "$LINUXDEPLOY" --appdir="$APPDIR" \
    --plugin gtk \
    --desktop-file="$APPDIR/usr/share/applications/$PACKAGE_NAME.desktop" \
    --output=appimage; then
        echo "[✓] linuxdeploy завершился успешно."
    else
        echo "[!] Ошибка при создании AppImage."
        echo "Для отладки запустите вручную:"
        echo "    ARCH=$APPIMAGE_ARCH $LINUXDEPLOY --appdir=$APPDIR --plugin gtk --output=appimage"
        exit 1
    fi

    # ---- Ищем созданный AppImage (исключая linuxdeploy) ----
    # linuxdeploy генерирует имя из Name= в .desktop (пробелы → подчёркивания),
    # поэтому конкретное имя заранее неизвестно. Полагаемся на find.
    local found_appimage=""
    found_appimage=$(find "$SCRIPT_DIR" -maxdepth 1 -name "*-${APPIMAGE_ARCH}.AppImage" ! -name "linuxdeploy*" -print -quit)

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

# ---- Подпись пакетов и контрольные суммы ----
sign_files() {
    local dist_dir="$OUTPUT_DIR"
    local checksum_basename="checksums.txt"
    local signature_basename="${checksum_basename}.asc"
    local checksum_file="$dist_dir/$checksum_basename"
    local signature_file="$dist_dir/$signature_basename"

    # Определяем, можем ли подписывать. Это НЕ влияет на генерацию checksums.
    local can_sign=false
    if ! command -v gpg >/dev/null 2>&1; then
        echo "[!] gpg не установлен — пакеты будут опубликованы без подписи."
    elif [ -z "${GPG_KEY_ID:-}" ]; then
        if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
            echo "::warning::GPG_KEY_ID не задан — артефакты будут опубликованы без подписи."
        else
            echo "[!] GPG_KEY_ID не задан — артефакты будут опубликованы без подписи."
        fi
    else
        can_sign=true
    fi

    # ---------- Подпись пакетов ----------
    if [ "$can_sign" = true ]; then
        # Если workflow подготовил wrapper — подложим его первым в PATH,
        # чтобы debsigs и rpm --addsign использовали именно его.
        if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -x "$GPG_WRAPPER_DIR/gpg" ]; then
            export PATH="$GPG_WRAPPER_DIR:$PATH"
            echo "[+] Используется GPG-wrapper: $GPG_WRAPPER_DIR/gpg"
        fi

        echo "[+] Подпись пакетов (ключ: $GPG_KEY_ID)..."

        # --- Подпись .deb через debsigs ---
        if command -v debsigs >/dev/null 2>&1; then
            local deb_found=false
            for file in "$dist_dir"/*.deb; do
                [ -f "$file" ] || continue
                deb_found=true
                echo "    debsigs: $(basename "$file")"
                if debsigs --sign=origin --default-key="$GPG_KEY_ID" "$file"; then
                    echo "    ✓ Подписан: $(basename "$file")"
                else
                    echo "    ⚠ debsigs не смог подписать $(basename "$file") — создаём отсоединённую подпись .deb.asc"
                    gpg --yes --detach-sign --armor \
                        --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" \
                        || echo "    ⚠ Не удалось создать даже отсоединённую подпись"
                fi
            done
            if [ "$deb_found" = false ]; then
                echo "[!] .deb файлы не найдены в $dist_dir"
            fi
        else
            echo "[!] debsigs не установлен — .deb не подписан."
            echo "    Установите: sudo apt install debsigs"
            for file in "$dist_dir"/*.deb; do
                [ -f "$file" ] || continue
                gpg --yes --detach-sign --armor \
                    --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" \
                    || echo "    ⚠ Не удалось подписать $(basename "$file")"
            done
        fi

        # --- Подпись .rpm ---
        if command -v rpm >/dev/null 2>&1; then
            if [ -f "$HOME/.rpmmacros" ] && grep -q "^%_gpg_name" "$HOME/.rpmmacros"; then
                sed -i "s|^%_gpg_name.*|%_gpg_name $GPG_KEY_ID|" "$HOME/.rpmmacros"
                echo "[+] Обновлён ~/.rpmmacros: %_gpg_name $GPG_KEY_ID"
            else
                echo "[+] Настройка ~/.rpmmacros (ключ: $GPG_KEY_ID)..."
                cat >> "$HOME/.rpmmacros" << EOF
%_signature gpg
%_gpg_name $GPG_KEY_ID
EOF
            fi

            for file in "$dist_dir"/*.rpm; do
                [ -f "$file" ] || continue
                echo "    rpm --addsign: $(basename "$file")"
                if rpm --addsign "$file"; then
                    echo "    ✓ Подписан: $(basename "$file")"
                else
                    echo "    ⚠ rpm --addsign не смог подписать $(basename "$file") — создаём .rpm.asc"
                    gpg --yes --detach-sign --armor \
                        --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" \
                        || echo "    ⚠ Не удалось создать даже отсоединённую подпись .rpm.asc"
                fi
            done
        else
            echo "[!] rpm не установлен — .rpm не подписан."
        fi

        # --- Подпись AppImage (отсоединённая) ---
        for file in "$dist_dir"/*.AppImage; do
            [ -f "$file" ] || continue
            echo "    gpg --detach-sign: $(basename "$file")"
            gpg --yes --detach-sign --armor \
                --local-user "$GPG_KEY_ID" --output "$file.asc" "$file" \
                || echo "    ⚠ Не удалось подписать $(basename "$file")"
        done
    fi

    # ---------- Генерация checksums.txt (ВСЕГДА) ----------
    # Идёт после подписи, чтобы .asc-файлы попали в контрольные суммы.
    echo "[+] Генерация checksums.txt..."
    rm -f "$checksum_file" "$signature_file"

    (
        cd "$dist_dir" || exit 1
        shopt -s nullglob
        files=()
        for f in *; do
            case "$f" in
                "$checksum_basename"|"$signature_basename") continue ;;
            esac
            files+=("$f")
        done
        if (( ${#files[@]} > 0 )); then
            sha256sum "${files[@]}"
        fi
    ) > "$checksum_file" || true

    # ---------- Подпись checksums.txt (если можем) ----------
    if [ "$can_sign" = true ]; then
        echo "[+] Подпись checksums.txt..."
        gpg --yes --detach-sign --armor --local-user "$GPG_KEY_ID" \
            --output "$signature_file" "$checksum_file" \
            || echo "    ⚠ Не удалось подписать checksums.txt"
        echo "[✓] Подпись checksums.txt создана: $signature_file"
    fi
}

# === Вывод справки ===
show_help() {
    cat << EOF
Использование: ./build-package.sh [ОПЦИИ]

Сборка пакетов .deb, .rpm, .AppImage для iptvplayer.

Опции:
  --deb           Собрать только .deb
  --rpm           Собрать только .rpm
  --appimage      Собрать только AppImage
  --all           Собрать все типы (по умолчанию)
  --rebuild       Принудительно пересобрать бинарник (даже если уже есть)
  --clean         Очистить каталог dist/ перед сборкой
  --clean-only    Только очистить dist/ и завершить работу
  --yes, -y       Неинтерактивный режим (авто-ответы, для CI)
  -h, --help      Показать эту справку

Примеры:
  ./build-package.sh --deb                # собрать .deb
  ./build-package.sh --all                # собрать все пакеты
  ./build-package.sh --rebuild --deb      # пересобрать бинарник и .deb
  ./build-package.sh --clean --all        # очистить dist и собрать всё
  ./build-package.sh --clean-only         # только очистить dist
  ./build-package.sh -h                   # показать эту справку
  ./build-package.sh --yes --all          # неинтерактивная сборка (CI)

Примечания:
  - Если не указан тип пакета, собираются все три.
  - Архитектура определяется автоматически через uname -m.
  - Версия определяется CMake при сборке бинарника (VERSION + git-хеш)
    и затем читается из install/.
  - Все артефакты сохраняются в каталог dist/.
  - Временные файлы удаляются автоматически после сборки.
  - Для сборки требуются: cmake, git, dpkg-deb, dpkg-shlibdeps,
    rpmbuild, wget, tar.
EOF
}

# === Главная функция ===
main() {
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
            --yes|-y) NON_INTERACTIVE=true ;;
            -h|--help) show_help; exit 0 ;;
            *)
                echo "Неизвестный аргумент: $1"
                show_help
                exit 1
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

    # ---- Подготовка каталогов ----
    check_deps "$BUILD_DEB" "$BUILD_RPM" "$BUILD_APPIMAGE"
    detect_arch
    setup_dirs

    # ---- Сборка бинарника (если требуется) ----
    build_binary

    # ---- Чтение версий из install/ ----
    # Получаем три значения: VERSION_FULL, VERSION_FILE, VERSION
    # (каждый read читает одну строку)
    { read -r VERSION_DISPLAY; read -r VERSION_FILE; read -r VERSION; } \
        < <(read_versions_from_install)

    if [[ -z "$VERSION_DISPLAY" || -z "$VERSION_FILE" || -z "$VERSION" ]]; then
        echo "[ERROR] Не удалось прочитать версии из install/." >&2
        echo "  VERSION_DISPLAY='$VERSION_DISPLAY'" >&2
        echo "  VERSION_FILE='$VERSION_FILE'" >&2
        echo "  VERSION='$VERSION'" >&2
        echo "Проверьте install/VERSION, install/VERSION_FULL, install/VERSION_FILE." >&2
        exit 1
    fi

    echo "=== Сборка пакетов для $PACKAGE_NAME:$VERSION (файл: $VERSION_FILE, arch: $DEB_ARCH/$RPM_ARCH/$APPIMAGE_ARCH) ==="
    echo ""

    # ---- Подготовка staging и сборка пакетов ----
    prepare_staging

    if [[ "$BUILD_DEB" == true ]]; then build_deb; fi
    if [[ "$BUILD_RPM" == true ]]; then build_rpm; fi
    if [[ "$BUILD_APPIMAGE" == true ]]; then build_appimage; fi

    sign_files

    cleanup

    echo ""
    echo "🎉 Готово! Артефакты в '$OUTPUT_DIR':"
    ls -la "$OUTPUT_DIR/"
}

main "$@"