#!/usr/bin/env bash
# =============================================================================
# build-rpm-common.sh – общий движок сборки .rpm
# =============================================================================
# Библиотека. Сорсится из build-package.sh до адаптеров build-native-rpm*.sh.
# Адаптер заполняет переменные RPM_* и вызывает build_rpm_common.
#
#   RPM_SPEC_DIR         (обяз.) каталог rpmbuild (_topdir)
#   RPM_RELEASE          (обяз.) Release: в spec
#   RPM_FILE_RELEASE     (опц.)  release в имени файла; default = RPM_RELEASE
#   RPM_GROUP            (опц.)  Group:; пусто = строка не пишется
#   RPM_CHANGELOG_TAG    (опц.)  хвост %changelog
#   RPM_POST_BODY        (опц.)  тело %post; default = update-icon-caches
#   RPM_EXTRA_SPEC       (опц.)  доп. директивы spec
#   RPM_OUTPUT_SUFFIX    (опц.)  суффикс имени .rpm; default = $DISTRO
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-rpm-common.sh — библиотека, не запускается напрямую." >&2
    exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

_default_rpm_post_body() {
    cat << 'EOF'
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
EOF
}

build_rpm_common() {
    : "${RPM_SPEC_DIR:?RPM_SPEC_DIR не задан адаптером}"
    : "${RPM_RELEASE:?RPM_RELEASE не задан адаптером}"
    : "${PROJECT_ROOT:?PROJECT_ROOT не задан}"
    : "${STAGING_DIR:?STAGING_DIR не задан}"
    : "${OUTPUT_DIR:?OUTPUT_DIR не задан}"
    : "${PACKAGE_NAME:?PACKAGE_NAME не задан}"
    : "${RPM_ARCH:?RPM_ARCH не задан}"
    : "${VERSION:?VERSION не задан}"
    : "${DISTRO:?DISTRO не задан}"

    ensure_tools "сборки .rpm" \
        "rpmbuild:rpm-build" \
        "rpm:rpm"

    local file_release="${RPM_FILE_RELEASE:-$RPM_RELEASE}"
    local changelog_tag="${RPM_CHANGELOG_TAG:-}"
    local output_suffix="${RPM_OUTPUT_SUFFIX:-$DISTRO}"
    local extra_spec="${RPM_EXTRA_SPEC:-}"

    local group_line=""
    [ -n "${RPM_GROUP:-}" ] && group_line="Group:          ${RPM_GROUP}"

    local post_body
    if [ -n "${RPM_POST_BODY:-}" ]; then
        post_body="$RPM_POST_BODY"
    else
        post_body="$(_default_rpm_post_body)"
    fi

    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${file_release}.${output_suffix}.${RPM_ARCH}.rpm"

    echo "[+] Создание нативного .rpm (release=$RPM_RELEASE, dist=$output_suffix)..."
    [ -n "${RPM_GROUP:-}" ] && echo "[i] Group: $RPM_GROUP"

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_rpm_common: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    # Файлы считаем ДО создания _spec_dir и установки RETURN-trap:
    # rpm_files_block — простая функция без побочек, но если она когда-то
    # начнёт возвращать код != 0 в подоболочке, RETURN-trap уже сработает
    # и удалит mktemp-каталог — до того как в него что-то запишут.
    local files_block
    files_block="$(rpm_files_block)"
    if [ -z "$files_block" ]; then
        echo "[!] build_rpm_common: rpm_files_block вернул пусто" >&2
        return 1
    fi

    # RPM brp-strip (шаг постобработки rpmbuild) не умеет работать с
    # не-ASCII в RPM_BUILD_ROOT: путь с кириллицей доходит до /usr/bin/strip
    # в octal-escape форме (\320\227\320\260...) и strip не находит файл —
    # "No such file". Остальные шаги (%prep/%build/%install) при этом
    # проходят, потому что bash разворачивает $'...' корректно.
    # Обходим: если _topdir содержит не-ASCII, переносим его в
    # ASCII-безопасный mktemp-каталог. Проверка LC_ALL=C grep '^[ -~]*$'
    # ловит любой байт вне printable ASCII (включая UTF-8 кириллицу).
    local _spec_dir="$RPM_SPEC_DIR"
    if ! printf '%s' "$_spec_dir" | LC_ALL=C grep -q '^[ -~]*$'; then
        local _ascii_topdir
        _ascii_topdir="$(mktemp -d -t iptvplayer-rpmbuild.XXXXXX)" || {
            echo "[!] не удалось создать ASCII-каталог для rpmbuild" >&2
            return 1
        }
        _spec_dir="$_ascii_topdir"
        echo "[i] Не-ASCII в PROJECT_ROOT — rpmbuild выполняется в $_spec_dir" >&2
        # shellcheck disable=SC2064
        # RETURN-trap: rm + снятие самого trap. Иначе он сработает на
        # выходе из любой следующей функции с уже несуществующим путём.
        trap "rm -rf -- '$_ascii_topdir'; trap - RETURN" RETURN
    fi

    rm -rf "$_spec_dir"
    mkdir -p "$_spec_dir"/{SOURCES,BUILD,RPMS,SRPMS,BUILDROOT}

    # Не используем --transform: на OpenMandriva tar — это bsdtar
    # (libarchive), который --transform не поддерживает (только -s).
    # Портативный путь: копируем staging во временный каталог с именем
    # $PACKAGE_NAME-$VERSION и упаковываем его. Работает и с GNU tar,
    # и с bsdtar, и с busybox tar.
    local _tarball_tmp
    _tarball_tmp="$(mktemp -d -t iptvplayer-rpm-src.XXXXXX)" || {
        echo "[!] build_rpm_common: mktemp для tar-источника упал" >&2
        return 1
    }
    if ! mkdir -p "$_tarball_tmp/${PACKAGE_NAME}-${VERSION}" \
         || ! cp -a "$STAGING_DIR/." "$_tarball_tmp/${PACKAGE_NAME}-${VERSION}/"; then
        rm -rf -- "$_tarball_tmp"
        echo "[!] build_rpm_common: подготовка tar-источника упала" >&2
        return 1
    fi

    if ! tar -czf "$_spec_dir/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
             -C "$_tarball_tmp" "${PACKAGE_NAME}-${VERSION}"; then
        rm -rf -- "$_tarball_tmp"
        echo "[!] build_rpm_common: упаковка staging в tar.gz упала" >&2
        return 1
    fi
    rm -rf -- "$_tarball_tmp"

    local changelog_suffix=""
    [ -n "$changelog_tag" ] && changelog_suffix=" $changelog_tag"

    cat > "$_spec_dir/${PACKAGE_NAME}.spec" << EOF
%define debug_package %{nil}
%define _topdir $_spec_dir

Name:           $PACKAGE_NAME
Version:        $VERSION
Release:        $RPM_RELEASE
Summary:        IPTV Playlist Player
License:        MIT
$group_line
URL:            https://github.com/i-jurij/$PACKAGE_NAME
Source0:        %{name}-%{version}.tar.gz
BuildArch:      $RPM_ARCH
$extra_spec

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
$post_body

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$RPM_RELEASE
- Initial build${changelog_suffix}
EOF

    if ! rpmbuild -bb --define "_topdir $_spec_dir" \
            "$_spec_dir/${PACKAGE_NAME}.spec"; then
        echo "[!] build_rpm_common: rpmbuild упал" >&2
        return 1
    fi

    local built
    built="$(find "$_spec_dir/RPMS" -name '*.rpm' -type f -print -quit 2>/dev/null)"
    if [ -z "$built" ]; then
        echo "[!] build_rpm_common: rpmbuild не создал .rpm в $_spec_dir/RPMS" >&2
        return 1
    fi
    if ! mv "$built" "$rpm_file"; then
        echo "[!] build_rpm_common: не удалось переместить $built → $rpm_file" >&2
        return 1
    fi

    echo "[✓] Нативный .rpm: $rpm_file"
    return 0
}