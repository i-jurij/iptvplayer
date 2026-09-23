#!/bin/bash
# =============================================================================
# build-native.sh – нативные .deb / .rpm / .pkg.tar.zst из системных библиотек
# =============================================================================
#
# Библиотека, не запускается напрямую.
# Сорсится из build-package.sh (см. scripts/common.sh для общего окружения).
#
# Требует установленных переменных окружения:
#   PROJECT_ROOT, STAGING_DIR, OUTPUT_DIR
#   PACKAGE_NAME, ICON_NAME, METAINFO_NAME
#   VERSION, DEB_ARCH, RPM_ARCH, APPIMAGE_ARCH, DISTRO
# Все они выставляются в build-package.sh до вызова этих функций.
#
# Функции:
#   build_deb_native  — .deb из системных библиотек
#   build_rpm_native  — .rpm из системных библиотек
#   build_pkg_arch    — .pkg.tar.zst через makepkg (только на Arch)
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

source "$SCRIPT_DIR/common.sh"

# ---- Нативный .deb ----
build_deb_native() {
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${DISTRO}_${DEB_ARCH}.deb"
    echo "[+] Создание нативного .deb..."
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_deb_native: не удалось подготовить staging" >&2
            return 1
        fi
    fi
    mkdir -p "$STAGING_DIR/DEBIAN"

    local depends
    depends=$(detect_deb_depends "$STAGING_DIR/usr/bin/$PACKAGE_NAME")
    if [ -z "$depends" ]; then
        echo "[!] не удалось определить Depends" >&2
        return 1
    fi
    echo "[+] Depends: $depends"

    cat > "$STAGING_DIR/DEBIAN/control" << EOF
Package: $PACKAGE_NAME
Version: $VERSION
Section: video
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
    if ! dpkg-deb -Zxz --build --root-owner-group "$STAGING_DIR" "$deb_file"; then
        echo "[!] нативный .deb: dpkg-deb упал" >&2
        return 1
    fi
    echo "[✓] Нативный .deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
    return 0
}

# ---- Нативный .rpm ----
build_rpm_native() {
    local release="1"
    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${release}.${DISTRO}.${RPM_ARCH}.rpm"
    local SPEC_DIR="$PROJECT_ROOT/pkg-rpm"
    echo "[+] Создание нативного .rpm..."
    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_rpm_native: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    mkdir -p "$SPEC_DIR/SOURCES"
    cd "$STAGING_DIR" && tar -czf "$SPEC_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
        --transform="flags=r;s,^,$PACKAGE_NAME-$VERSION/," . && cd - > /dev/null

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

    if ! rpmbuild -bb --define "_topdir $SPEC_DIR" "$SPEC_DIR/${PACKAGE_NAME}.spec"; then
        echo "[!] нативный .rpm: rpmbuild упал" >&2
        return 1
    fi
    if ! { mv "$SPEC_DIR/RPMS/"*/*.rpm "$rpm_file" 2>/dev/null \
        || mv "$SPEC_DIR/RPMS/${RPM_ARCH}/"*.rpm "$rpm_file"; }; then
        echo "[!] нативный .rpm: не найден собранный .rpm" >&2
        return 1
    fi
    echo "[✓] Нативный .rpm: $rpm_file"
    return 0
}

# ---- Нативный .pkg.tar.zst ARCH ----
build_pkg_arch() {
    # На не-Arch системах makepkg нет. Явный вызов --native-arch
    # на Ubuntu/Debian — ошибка пользователя.
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg не найден. Сборка .pkg.tar.zst возможна только на Arch/Manjaro."
        return 1
    fi

    # Пустой STAGING_DIR опаснее отсутствующего: проверки вида
    # "$STAGING_DIR/usr/bin/$PACKAGE_NAME" без него схлопываются в
    # /usr/bin/... и функция может случайно работать с системными
    # файлами вместо staging.
    if [ -z "$STAGING_DIR" ] || [ ! -d "$STAGING_DIR" ]; then
        echo "[!] STAGING_DIR не установлен или не существует: ${STAGING_DIR:-<empty>}" >&2
        return 1
    fi

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-1-${DISTRO}-${APPIMAGE_ARCH}.pkg.tar.zst"
    echo "[+] Создание нативного .pkg.tar.zst через makepkg..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_pkg_arch: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    # actions/upload-artifact теряет exec-бит.
    chmod +x "$STAGING_DIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # Автоопределение зависимостей: ldd -> .so -> pacman -Fq -> пакет-владелец.
    local BIN="$STAGING_DIR/usr/bin/$PACKAGE_NAME"
    [ -x "$BIN" ] || { echo "[!] $BIN не исполняем" >&2; return 1; }

    # Файловая БД pacman (нужна для pacman -Fq) синхронизируется
    # командой 'pacman -Fy', которая требует root. В CI мы root —
    # вызываем напрямую. Локально — повышаем привилегии через
    # sudo/doas/run0/pkexec. Если ни одно не сработало — падаем:
    # без БД автоопределение даст пустой результат, а собирать
    # пакет с выдуманными зависимостями хуже, чем не собрать вовсе.
    if [ "$(id -u)" -eq 0 ]; then
        echo "[i] Синхронизация файловой БД pacman (pacman -Fy)..."
        if ! timeout 120 pacman -Fy >/dev/null; then
            echo "[!] 'pacman -Fy' завершился с ошибкой" >&2
            return 1
        fi
    else
        local _sudo="" _c
        for _c in sudo doas run0 pkexec; do
            if command -v "$_c" >/dev/null 2>&1; then
                _sudo="$_c"
                break
            fi
        done
        if [ -z "$_sudo" ]; then
            echo "[!] Для 'pacman -Fy' нужен root, но ни sudo, ни doas, ни run0, ни pkexec не найдены." >&2
            echo "[!] Установите sudo или запустите сборку от root." >&2
            return 1
        fi
        echo "[i] Синхронизация файловой БД pacman через '$_sudo pacman -Fy'..."
        # stderr не глушим — пользователь увидит запрос пароля
        # (sudo/doas/run0) или сообщение об отказе в правах.
        if ! timeout 300 "$_sudo" pacman -Fy >/dev/null; then
            echo "[!] '$_sudo pacman -Fy' завершился с ошибкой" >&2
            return 1
        fi
    fi

    # Временные файлы и workdir. trap RETURN срабатывает на любом
    # выходе из функции, включая ранние return 1. workdir остаётся
    # только если keep_workdir=1 — это для отладки упавшего makepkg.
    local libs_tmp pkgs_tmp workdir keep_workdir=0
    libs_tmp=$(mktemp) || { echo "[!] mktemp для libs_tmp упал" >&2; return 1; }
    pkgs_tmp=$(mktemp) || { echo "[!] mktemp для pkgs_tmp упал" >&2; return 1; }
    workdir=$(mktemp -d) || { echo "[!] mktemp для workdir упал" >&2; return 1; }
    # shellcheck disable=SC2064
    trap 'rm -f "$libs_tmp" "$pkgs_tmp"; [ "$keep_workdir" = 1 ] || rm -rf "$workdir"' RETURN

    ldd "$BIN" 2>/dev/null \
      | awk '
          /=>/ && $3 ~ /^\//  { print $3 }
          /^\t\//              { print $1 }
        ' \
      | sort -u > "$libs_tmp"

    : > "$pkgs_tmp"
    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        # pacman -Fq не находит владельца для библиотек вне пакетов
        # (собрано вручную, /usr/local/lib). Это норма, не ошибка.
        pacman -Fq "$lib" 2>/dev/null \
          | awk -F/ '{print $2}' >> "$pkgs_tmp" || true
    done < "$libs_tmp"

    # Конфликтующие пары: несколько пакетов предоставляют один soname
    # (jack2/pipewire-jack для libjack.so.0). Их нельзя ставить
    # одновременно, но оба попадают в вывод pacman -Fq. Решение:
    # для каждого пакета находим виртуальное имя, которое он
    # предоставляет через Provides, и пишем его в depends. Если
    # виртуального имени нет — оставляем имя пакета.
    #
    # mesa-amber — legacy-пакет, конфликтует с mesa на уровне файлов.
    # Виртуального имени у него нет, поэтому фильтруем отдельно.
    local raw_deps
    raw_deps=$(sort -u "$pkgs_tmp" \
                | grep -vxE 'glibc|gcc-libs|mesa-amber' \
                | grep -v '^$')

    local auto_deps=""
    local _owner _virtual _seen=" "
    while IFS= read -r _owner; do
        [ -z "$_owner" ] && continue
        _virtual=$(pacman -Si "$_owner" 2>/dev/null \
                    | awk -F': ' '/^Provides/{print $2}' \
                    | tr ' ' '\n' \
                    | grep -vx "$_owner" \
                    | grep -vx 'None' \
                    | head -n1)
        if [ -z "$_virtual" ]; then
            _virtual="$_owner"
        fi
        case "$_seen" in
            *" $_virtual "*) continue ;;
        esac
        _seen="$_seen$_virtual "
        auto_deps="$auto_deps $_virtual"
    done <<EOF
$raw_deps
EOF
    auto_deps=$(printf '%s\n' $auto_deps | sort -u | tr '\n' ' ')

    if [ -z "${auto_deps// }" ]; then
        echo "[!] Автоопределение зависимостей не дало ни одного пакета." >&2
        echo "[!] Это значит, что 'pacman -Fq' не смог найти владельцев ни одной библиотеки." >&2
        echo "[!] Проверьте: sudo pacman -Fy && pacman -Fq /usr/lib/libgtk-3.so.0" >&2
        return 1
    fi

    # extra_deps не нужен: mesa и gdk-pixbuf2 приходят из ldd,
    # librsvg в Arch не предоставляет .so-загрузчик для gdk-pixbuf
    # (SVG идёт через glycin, который тянется с gtk3).
    local all_deps
    all_deps=$(printf '%s\n' $auto_deps | sort -u | tr '\n' ' ')

    echo "[i] auto-detected: $auto_deps"
    echo "[i] final:         $all_deps"

    cp -a "$STAGING_DIR/usr" "$workdir/staging_usr"

    # depends=('a' 'b' 'c') — через printf, чтобы не собирать строку руками.
    local -a deps_array=()
    local d
    for d in $all_deps; do
        deps_array+=("$d")
    done

    {
        cat <<EOF
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=1
pkgdesc="IPTV Playlist Player"
arch=('$APPIMAGE_ARCH')
url="https://github.com/i-jurij/$PACKAGE_NAME"
license=('MIT')
# !debug: без отдельного -debug пакета с символами — он не нужен
# в релизе и удваивает размер артефактов.
options=('!debug')
EOF
        printf "depends=("
        printf "'%s' " "${deps_array[@]}"
        printf ")\n"
        cat <<EOF

package() {
    install -d "\$pkgdir/usr"
    cp -a "\$startdir/staging_usr/." "\$pkgdir/usr/"
}
EOF
    } > "$workdir/PKGBUILD"

    # makepkg отказывается работать от root. Если мы root — создаём
    # (идемпотентно) пользователя builder и отдаём ему workdir.
    local makepkg_cmd
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m builder 2>/dev/null || true
        chown -R builder:builder "$workdir"
        makepkg_cmd="runuser -u builder -- makepkg -f --nodeps --nocheck"
    else
        makepkg_cmd="makepkg -f --nodeps --nocheck"
    fi

    if ! ( cd "$workdir" && $makepkg_cmd ); then
        keep_workdir=1
        echo "[!] makepkg упал. Логи и PKGBUILD: $workdir" >&2
        echo "[!] Для отладки: cd $workdir && cat PKGBUILD" >&2
        return 1
    fi

    # Фильтруем debug-пакет — даже при options=('!debug') подстрахуемся.
    local built
    built=$(find "$workdir" -maxdepth 1 \
              -name '*.pkg.tar.zst' \
              ! -name '*-debug-*' \
              -print -quit)
    if [ -n "$built" ]; then
        mv "$built" "$pkg_file"
        echo "[✓] Нативный .pkg.tar.zst: $pkg_file"
    else
        keep_workdir=1
        echo "[!] makepkg не создал пакет. Содержимое: $workdir" >&2
        return 1
    fi

    return 0
}