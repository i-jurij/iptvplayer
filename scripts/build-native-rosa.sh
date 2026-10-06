#!/usr/bin/env bash
# =============================================================================
# build-native-rosa.sh – нативный .rpm для ROSA Linux
# =============================================================================
#
# Библиотека, не запускается напрямую. Сорсится из build-package.sh.
#
# Особенности ROSA:
#   * Пакетный менеджер — dnf (начиная с платформы 2021.1).
#   * Нумерация релиза — rosa2021.1 (ROSA_OS_PLATFORM из /etc/os-release).
#   * Префикс lib64 для 64-битных библиотек в зависимостях (lib64gtk+3.0-devel).
#   * Макросы RPM (_bindir, _datadir, _licensedir) совпадают с Fedora.
#
# Требует переменных окружения (выставляет build-package.sh):
#   PROJECT_ROOT, SCRIPT_DIR
#   STAGING_DIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME
#   VERSION, RPM_ARCH, DISTRO
#
# Функции:
#   build_rpm_rosa — .rpm из системных библиотек для ROSA Linux
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-rosa.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"
# ---- Нативный .rpm для ROSA Linux ----
build_rpm_rosa() {
    # Определяем платформенный тег ROSA для суффикса Release.
    local rosa_platform=""
    if [ -r /etc/os-release ]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        rosa_platform="${ROSA_OS_PLATFORM:-}"
        if [ -z "$rosa_platform" ] \
           && [ -n "${ID:-}" ] && [ -n "${VERSION_ID:-}" ]; then
            rosa_platform="${ID}${VERSION_ID}"
        fi
    fi

    # Имя файла — единый паттерн со всеми RPM-вариантами:
    #   ${PACKAGE_NAME}-${VERSION}-${RELEASE}.${DISTRO}.${RPM_ARCH}.rpm
    # На диске — короткий Release (1), как у native и ALT.
    # В spec — расширенный (1.<platform>), если платформа определилась.
    local file_release="1"
    local spec_release="1"
    [ -n "$rosa_platform" ] && spec_release="1.${rosa_platform}"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${file_release}.${DISTRO}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$PROJECT_ROOT/pkg-rpm-rosa"

    echo "[+] Создание нативного .rpm для ROSA Linux (${rosa_platform:-без платформенного суффикса})..."

    # Стадия staging — подготовка файловой раскладки пакета.
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_rpm_rosa: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    # Чистим предыдущий прогон.
    rm -rf "$SPEC_DIR"
    mkdir -p "$SPEC_DIR/SOURCES" "$SPEC_DIR/BUILD" \
             "$SPEC_DIR/RPMS"   "$SPEC_DIR/SRPMS" "$SPEC_DIR/BUILDROOT"

    # Исходник для rpmbuild — tar.gz с уже собранными файлами.
    ( cd "$STAGING_DIR" \
      && tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
            --transform="flags=r;s,^,$PACKAGE_NAME-$VERSION/," . )

    local files_block
    files_block=$(rpm_files_block)

    # -------------------------------------------------------------------------
    # Spec-файл. ROSA использует Group (обязательное поле), как в Mandriva,
    # но для современных сборок Group опционален, поэтому включаем для
    # совместимости. Release: 1.rosa2021.1 — конвенция ROSA.
    # -------------------------------------------------------------------------
    cat > "$SPEC_DIR/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir $SPEC_DIR

Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $spec_release
Summary:        IPTV Playlist Player
License:        MIT
Group:          Video
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      $RPM_ARCH

%description
A simple player for M3U playlists with GUI.

%prep
%setup -q

%build
# Бинарник уже собран — на этой стадии ничего не делаем.

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
tar -xzf %{SOURCE0} -C %{buildroot} --strip-components=1

%files
$files_block

%post
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$spec_release
- Initial build for ROSA Linux
EOF

    if ! rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"; then
        echo "[!] нативный .rpm (ROSA): rpmbuild упал" >&2
        return 1
    fi

    # Ищем собранный пакет.
    if ! { mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"; }; then
        echo "[!] нативный .rpm (ROSA): не найден собранный .rpm" >&2
        return 1
    fi

    echo "[✓] Нативный .rpm (ROSA): $rpm_file"
    return 0
}