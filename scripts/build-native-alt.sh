#!/usr/bin/env bash
# =============================================================================
# build-native-alt.sh – нативный .rpm для ALT Linux
# =============================================================================
#
# Библиотека, не запускается напрямую. Сорсится из build-package.sh.
#
# Особенности ALT:
#   * Пакетный менеджер — apt-rpm (APT поверх RPM-базы), но упаковка
#     стандартная: rpmbuild + spec. Хасер/этажи здесь не нужны —
#     мы пакуем уже собранные файлы, а не собираем из исходников.
#   * Нумерация релиза — alt1, alt2, ...  (см. ALT Packaging Rules).
#   * Макросы RPM (_bindir, _datadir, _licensedir) совпадают с Fedora,
#     поэтому %files-секция идентична нативному rpm-варианту.
#
# Требует переменных окружения (выставляет build-package.sh):
#   PROJECT_ROOT, SCRIPT_DIR
#   STAGING_DIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME
#   VERSION, RPM_ARCH, DISTRO
#
# Функции:
#   build_rpm_alt — .rpm из системных библиотек для ALT Linux
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-alt.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"
# ---- Нативный .rpm для ALT Linux ----
build_rpm_alt() {
    local release="alt1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${release}.${DISTRO}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$PROJECT_ROOT/pkg-rpm-alt"

    echo "[+] Создание нативного .rpm для ALT Linux..."

    # Стадия staging — подготовка файловой раскладки пакета.
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_rpm_alt: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    # Чистим предыдущий прогон, чтобы %setup и rpmbuild-каталоги
    # стартовали с нуля (иначе старые BUILD/RPMS могут подмешаться).
    rm -rf "$SPEC_DIR"
    mkdir -p "$SPEC_DIR/SOURCES" "$SPEC_DIR/BUILD" \
             "$SPEC_DIR/RPMS"   "$SPEC_DIR/SRPMS" "$SPEC_DIR/BUILDROOT"

    # Исходник для rpmbuild — tar.gz с уже собранными файлами.
    # Тот же приём, что и в build_rpm_native: собираем staging как
    # "исходник", а %install просто распаковывает его в buildroot.
    ( cd "$STAGING_DIR" \
      && tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
            --transform="flags=r;s,^,$PACKAGE_NAME-$VERSION/," . )

    local files_block
    files_block=$(rpm_files_block)

    # -------------------------------------------------------------------------
    # Spec-файл. Отличия от Fedora-варианта:
    #   * Release: alt1 — ALT-конвенция нумерации сборок.
    #   * BuildArch: явно указываем.
    #   * debug_package отключён — исходников нет, символов не будет.
    # -------------------------------------------------------------------------
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
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$release
- Initial build for ALT Linux
EOF

    if ! rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"; then
        echo "[!] нативный .rpm (ALT): rpmbuild упал" >&2
        return 1
    fi

    # Ищем собранный пакет. Как и в rpm-варианте, путь зависит от того,
    # как rpmbuild разложит RPMS/<arch>/ — сначала общий glob, потом точный.
    if ! { mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"; }; then
        echo "[!] нативный .rpm (ALT): не найден собранный .rpm" >&2
        return 1
    fi

    echo "[✓] Нативный .rpm (ALT): $rpm_file"
    return 0
}