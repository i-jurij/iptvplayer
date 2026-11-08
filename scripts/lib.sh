#!/usr/bin/env bash
# =============================================================================
# lib.sh - utilities: ask, download, deps_*, pack_rpm, pack_deb,
#          prepare_staging, check_deps, ensure_tools, setup_dirs, sign_files.
# =============================================================================
# Library. Sourced from make.sh after detect.sh.
# Does not know distribution names. Distribution-specific data comes from
# distros/<name>.sh via PKGMGR, PKGMGR_CHECK, COM_*, OPTS_*, CHECK_CHAIN,
# REQUIRED_TOOLS, RPM_*, DEB_*.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "lib.sh is a library, do not run directly." >&2
    exit 1
fi

: "${SCRIPT_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)}"
: "${PROJECT_ROOT:=$(cd "$SCRIPT_DIR/.." && pwd)}"

# -----------------------------------------------------------------------------
# ask - prompt y/n with separate defaults for interactive / non-interactive.
# Returns 0 for yes, 1 for no.
# -----------------------------------------------------------------------------
ask() {
    local prompt="$1"
    local di="${2:-n}"
    local dni="${3:-$di}"
    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        log "Non interactive: '$prompt' -> $dni"
        [[ "$dni" == "y" ]]
        return
    fi
    local reply=""
    read -p "$prompt " -n 1 -r reply || true
    echo
    [[ -z "$reply" ]] && reply="$di"
    [[ "$reply" =~ ^[Yy]$ ]]
}

# -----------------------------------------------------------------------------
# download / download_multi - download with retries and backoff.
# -----------------------------------------------------------------------------
if command -v wget >/dev/null 2>&1; then
    DOWNLOADER="wget"
elif command -v curl >/dev/null 2>&1; then
    DOWNLOADER="curl"
else
    DOWNLOADER=""
fi

# Run "$@" with stderr teed to both $log and the terminal (via FIFO).
_run_tee_stderr() {
    local log="$1"; shift
    local _dir _fifo _tee_pid _rc
    _dir="$(mktemp -d -t iptv-dl.XXXXXX)" || return 1
    _fifo="$_dir/stderr.fifo"
    if ! mkfifo "$_fifo"; then
        rm -rf "$_dir"
        return 1
    fi
    tee -a "$log" < "$_fifo" >&2 &
    _tee_pid=$!
    "$@" 2>"$_fifo"
    _rc=$?
    wait "$_tee_pid" 2>/dev/null || true
    rm -rf "$_dir"
    return $_rc
}

download() {
    local url="$1"
    local out="$2"
    local ua="${3:-}"
    local tries=5 i
    local log
    log="$(mktemp -t download.XXXXXX)"

    if [[ -z "$DOWNLOADER" ]]; then
        error "wget or curl is required to download $url"
    fi

    local -a progress=()
    if [[ -t 2 ]]; then
        if [[ "$DOWNLOADER" == "wget" ]]; then
            progress=(--show-progress)
        else
            progress=(--progress-bar)
        fi
    fi

    for ((i = 1; i <= tries; i++)); do
        : > "$log"
        if [[ "$DOWNLOADER" == "wget" ]]; then
            local -a args=(--tries=1 --timeout=30 --read-timeout=60 "${progress[@]}")
            [[ -n "$ua" ]] && args+=(--header="User-Agent: $ua")
            if _run_tee_stderr "$log" wget "${args[@]}" "$url" -O "$out"; then
                rm -f "$log"
                return 0
            fi
        else
            local -a args=(-L -f --connect-timeout 30 --retry 0 "${progress[@]}")
            [[ -n "$ua" ]] && args+=(-H "User-Agent: $ua")
            if _run_tee_stderr "$log" curl "${args[@]}" -o "$out" "$url"; then
                rm -f "$log"
                return 0
            fi
        fi
        warn "Download failed ($i/$tries): $url"
        if ((i == tries)) && [[ -s "$log" ]]; then
            echo "--- Downloader log ---" >&2
            cat "$log" >&2
            echo "------------------------" >&2
        fi
        if ((i < tries)); then
            local wait=$((5 * (2 ** (i - 1))))
            (( wait > 30 )) && wait=30
            sleep "$wait"
        fi
    done
    rm -f "$log"
    return 1
}

download_multi() {
    local out="$1"; shift
    local ua="${DOWNLOAD_UA:-}"
    local url
    for url in "$@"; do
        if download "$url" "$out" "$ua"; then
            return 0
        fi
        warn "Next URL: $url"
    done
    return 1
}

# -----------------------------------------------------------------------------
# read_versions_from_install - read install/VERSION* into stdout (3 lines).
# Returns: VERSION_FULL, VERSION_FILE, VERSION (one per line).
# -----------------------------------------------------------------------------
read_versions_from_install() {
    local d="$PROJECT_ROOT/install"
    local vf="$d/VERSION" vfull="$d/VERSION_FULL" vfile="$d/VERSION_FILE"

    if [ ! -f "$vf" ] || [ ! -f "$vfull" ] || [ ! -f "$vfile" ]; then
        echo "[!] Version files missing in $d" >&2
        return 1
    fi
    local V VF VFILE
    V=$(tr -d '\n\r' < "$vf" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VF=$(tr -d '\n\r' < "$vfull" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    VFILE=$(tr -d '\n\r' < "$vfile" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    if [ -z "$V" ] || [ -z "$VF" ] || [ -z "$VFILE" ]; then
        return 1
    fi
    printf '%s\n%s\n%s\n' "$VF" "$VFILE" "$V"
}

# -----------------------------------------------------------------------------
# write_desktop_file - generate .desktop file at $1.
# Comment[ru]/[uk] are data consumed by the desktop environment, not
# console output - Cyrillic is allowed there.
# -----------------------------------------------------------------------------
write_desktop_file() {
    local target="$1"
    mkdir -p "$(dirname "$target")"
    cat > "$target" << EOF
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE_NAME %F
Icon=${ICON_NAME%.svg}
Type=Application
Categories=AudioVideo;
Comment=IPTV Playlist Player
Comment[de]=IPTV-Playlist-Player
Comment[es]=Reproductor de listas IPTV
Comment[fr]=Lecteur de listes IPTV
Comment[pt]=Reprodutor de listas IPTV
Comment[ru]=Плеер для плейлистов IPTV
Comment[uk]=Плеєр для плейлистів IPTV
Terminal=false
StartupNotify=true
StartupWMClass=$PACKAGE_NAME
MimeType=video/mp4;video/x-matroska;video/avi;video/mpeg;video/quicktime;video/x-msvideo;video/x-flv;video/ogg;video/webm;application/x-mpegURL;audio/x-mpegurl;audio/x-scpls;application/xspf+xml;application/vnd.apple.mpegurl;
EOF
}

# -----------------------------------------------------------------------------
# prepare_staging - copy install/ into $STAGING_DIR/usr/.
# Idempotent. Returns 1 if install/bin/$PACKAGE_NAME is missing.
# -----------------------------------------------------------------------------
prepare_staging() {
    local bin="$PROJECT_ROOT/install/bin/$PACKAGE_NAME"
    if [ ! -f "$bin" ]; then
        echo "[!] Binary not found: $bin" >&2
        return 1
    fi

    mkdir -p "$STAGING_DIR/usr/bin"
    cp "$bin" "$STAGING_DIR/usr/bin/"

    mkdir -p "$STAGING_DIR/usr/share/$PACKAGE_NAME"
    cp -r "$PROJECT_ROOT/install/share/$PACKAGE_NAME/"* \
          "$STAGING_DIR/usr/share/$PACKAGE_NAME/" 2>/dev/null || true

    write_desktop_file "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop"

    mkdir -p "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps"
    cp "$PROJECT_ROOT/install/share/$PACKAGE_NAME/icons/$ICON_NAME" \
       "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" 2>/dev/null || true

    if [ -f "$PROJECT_ROOT/install/share/metainfo/$METAINFO_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/metainfo"
        cp "$PROJECT_ROOT/install/share/metainfo/$METAINFO_NAME" "$STAGING_DIR/usr/share/metainfo/"
    fi

    if [ -d "$PROJECT_ROOT/install/share/doc/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME"
        cp -r "$PROJECT_ROOT/install/share/doc/$PACKAGE_NAME/"* \
              "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME/" 2>/dev/null || true
    fi

    if [ -d "$PROJECT_ROOT/install/share/licenses/$PACKAGE_NAME" ]; then
        mkdir -p "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME"
        cp -r "$PROJECT_ROOT/install/share/licenses/$PACKAGE_NAME/"* \
              "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME/" 2>/dev/null || true
    fi

    if [ -d "$PROJECT_ROOT/install/share/locale" ]; then
        mkdir -p "$STAGING_DIR/usr/share/locale"
        if ! cp -r "$PROJECT_ROOT/install/share/locale/." "$STAGING_DIR/usr/share/locale/"; then
            echo "[!] Failed to copy locales from install/" >&2
            return 1
        fi
    fi

    return 0
}

# -----------------------------------------------------------------------------
# rpm_files_block - print %files body (without %files header) based on staging.
# -----------------------------------------------------------------------------
rpm_files_block() {
    local entries=()
    local lang lang_dir

    [ -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ] \
        && entries+=("%{_bindir}/$PACKAGE_NAME")
    [ -d "$STAGING_DIR/usr/share/$PACKAGE_NAME" ] \
        && entries+=("%{_datadir}/$PACKAGE_NAME/")
    [ -f "$STAGING_DIR/usr/share/applications/$PACKAGE_NAME.desktop" ] \
        && entries+=("%{_datadir}/applications/$PACKAGE_NAME.desktop")
    [ -f "$STAGING_DIR/usr/share/icons/hicolor/scalable/apps/$ICON_NAME" ] \
        && entries+=("%{_datadir}/icons/hicolor/scalable/apps/$ICON_NAME")
    [ -f "$STAGING_DIR/usr/share/metainfo/$METAINFO_NAME" ] \
        && entries+=("%{_datadir}/metainfo/$METAINFO_NAME")
    [ -d "$STAGING_DIR/usr/share/doc/$PACKAGE_NAME" ] \
        && entries+=("%{_datadir}/doc/$PACKAGE_NAME")
    [ -d "$STAGING_DIR/usr/share/licenses/$PACKAGE_NAME" ] \
        && entries+=("%{_datadir}/licenses/$PACKAGE_NAME")

    if [ -d "$STAGING_DIR/usr/share/locale" ]; then
        for lang_dir in "$STAGING_DIR/usr/share/locale"/*/; do
            [ -d "$lang_dir" ] || continue
            lang="$(basename "$lang_dir")"
            [ -f "$lang_dir/LC_MESSAGES/$PACKAGE_NAME.mo" ] || continue
            entries+=("%lang(${lang}) %{_datadir}/locale/${lang}/LC_MESSAGES/$PACKAGE_NAME.mo")
        done
    fi

    printf '%s\n' "${entries[@]}"
}

# -----------------------------------------------------------------------------
# detect_deb_depends - run dpkg-shlibdeps on the binary, print Depends string.
# -----------------------------------------------------------------------------
detect_deb_depends() {
    local bin_path="$1"
    local tmp
    tmp=$(mktemp -d)
    mkdir -p "$tmp/debian"
    cat > "$tmp/debian/control" << EOF
Source: $PACKAGE_NAME
Package: $PACKAGE_NAME
Architecture: any
EOF
    local out
    out=$(cd "$tmp" && dpkg-shlibdeps -O "$bin_path" 2>&1) || true
    rm -rf "$tmp"
    echo "$out" | sed -n 's/^shlibs:Depends=//p' | tr -d '\n\r'
}

# -----------------------------------------------------------------------------
# pack_rpm - build .rpm. Input (from distro):
#   RPM_SPEC_DIR, RPM_RELEASE, RPM_FILE_RELEASE, RPM_GROUP, RPM_CHANGELOG_TAG,
#   RPM_POST_BODY, RPM_EXTRA_SPEC, RPM_OUTPUT_SUFFIX, RPM_DESC_EXTRA
# -----------------------------------------------------------------------------
pack_rpm() {
    : "${RPM_SPEC_DIR:?RPM_SPEC_DIR not set by distro}"
    : "${RPM_RELEASE:?RPM_RELEASE not set by distro}"
    : "${PROJECT_ROOT:?}" "${STAGING_DIR:?}" "${OUTPUT_DIR:?}"
    : "${PACKAGE_NAME:?}" "${RPM_ARCH:?}" "${VERSION:?}" "${DISTRO:?}"

    ensure_tools "rpm packaging" "rpmbuild:rpm-build" "rpm:rpm"

    local file_release="${RPM_FILE_RELEASE:-$RPM_RELEASE}"
    local changelog_tag="${RPM_CHANGELOG_TAG:-}"
    local output_suffix="${RPM_OUTPUT_SUFFIX:-$DISTRO}"
    local extra_spec="${RPM_EXTRA_SPEC:-}"
    local desc_extra="${RPM_DESC_EXTRA:-}"
    if [ -z "$desc_extra" ]; then
        desc_extra="

For H.264/H.265 video the system needs libavcodec and/or openh264, openh265
with patent-encumbered codecs from your distribution's multimedia repository
(RPM Fusion on RHEL-compatible systems, Packman on openSUSE, Tainted on Mageia,
restricted on OpenMandriva)."
    fi

    local group_line=""
    [ -n "${RPM_GROUP:-}" ] && group_line="Group:          ${RPM_GROUP}"

    local rpm_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${file_release}.${output_suffix}.${RPM_ARCH}.rpm"
    log "Creating .rpm (release=$RPM_RELEASE, dist=$output_suffix)..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            return 1
        fi
    fi

    local files_block
    files_block="$(rpm_files_block)"
    if [ -z "$files_block" ]; then
        echo "[!] pack_rpm: rpm_files_block returned empty" >&2
        return 1
    fi

    local _spec_dir="$RPM_SPEC_DIR"
    if ! printf '%s' "$_spec_dir" | LC_ALL=C grep -q '^[ -~]*$'; then
        local _ascii
        _ascii="$(mktemp -d -t iptvplayer-rpmbuild.XXXXXX)" || {
            echo "[!] Failed to create ASCII-safe rpmbuild directory" >&2
            return 1
        }
        _spec_dir="$_ascii"
        log "Non-ASCII in PROJECT_ROOT - rpmbuild in $_spec_dir"
        # shellcheck disable=SC2064
        trap "rm -rf -- '$_ascii'; trap - RETURN" RETURN
    fi

    rm -rf "$_spec_dir"
    mkdir -p "$_spec_dir"/{SOURCES,BUILD,RPMS,SRPMS,BUILDROOT}

    local _tar_tmp
    _tar_tmp="$(mktemp -d -t iptvplayer-rpm-src.XXXXXX)" || {
        echo "[!] mktemp for tar source failed" >&2
        return 1
    }
    if ! mkdir -p "$_tar_tmp/${PACKAGE_NAME}-${VERSION}" \
         || ! cp -a "$STAGING_DIR/." "$_tar_tmp/${PACKAGE_NAME}-${VERSION}/"; then
        rm -rf -- "$_tar_tmp"
        echo "[!] Failed to prepare tar source" >&2
        return 1
    fi

    if ! tar -czf "$_spec_dir/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
             -C "$_tar_tmp" "${PACKAGE_NAME}-${VERSION}"; then
        rm -rf -- "$_tar_tmp"
        echo "[!] Failed to pack staging into tar.gz" >&2
        return 1
    fi
    rm -rf -- "$_tar_tmp"

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
A simple player for playlists with GUI.${desc_extra}

%prep
%setup -q

%build
:

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
tar -xzf %{SOURCE0} -C %{buildroot} --strip-components=1

%files
$files_block

%changelog
* $(LC_TIME=en_US.UTF-8 date +"%a %b %d %Y") ijurij <mnisjil@duck.com> - $VERSION-$RPM_RELEASE
- Initial build${changelog_suffix}
EOF

    if ! rpmbuild -bb --define "_topdir $_spec_dir" \
            "$_spec_dir/${PACKAGE_NAME}.spec"; then
        echo "[!] pack_rpm: rpmbuild failed" >&2
        return 1
    fi

    local built
    built="$(find "$_spec_dir/RPMS" -name '*.rpm' -type f -print -quit 2>/dev/null)"
    if [ -z "$built" ]; then
        echo "[!] pack_rpm: rpmbuild did not produce .rpm in $_spec_dir/RPMS" >&2
        return 1
    fi
    if ! mv "$built" "$rpm_file"; then
        echo "[!] pack_rpm: failed to move $built -> $rpm_file" >&2
        return 1
    fi

    log ".rpm: $rpm_file"
    return 0
}

# -----------------------------------------------------------------------------
# pack_deb - build .deb. Input (from distro): DEB_OUTPUT_SUFFIX (optional).
# -----------------------------------------------------------------------------
pack_deb() {
    : "${PROJECT_ROOT:?}" "${STAGING_DIR:?}" "${OUTPUT_DIR:?}"
    : "${PACKAGE_NAME:?}" "${VERSION:?}" "${DEB_ARCH:?}" "${DISTRO:?}"

    ensure_tools "deb packaging" "dpkg-deb:dpkg" "dpkg-shlibdeps:dpkg-dev"

    local suffix="${DEB_OUTPUT_SUFFIX:-$DISTRO}"
    local deb_file="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_${suffix}_${DEB_ARCH}.deb"

    log "Creating .deb..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            return 1
        fi
    fi
    mkdir -p "$STAGING_DIR/DEBIAN"

    local depends
    depends=$(detect_deb_depends "$STAGING_DIR/usr/bin/$PACKAGE_NAME")
    if [ -z "$depends" ]; then
        echo "[!] pack_deb: could not determine Depends" >&2
        return 1
    fi
    log "Depends: $depends"

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
 A simple player for playlists with GUI.
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
        echo "[!] pack_deb: dpkg-deb failed" >&2
        return 1
    fi

    log ".deb: $deb_file"
    rm -rf "$STAGING_DIR/DEBIAN"
    return 0
}

# -----------------------------------------------------------------------------
# deps_is_installed / deps_update / deps_install / deps_remove
# Generic wrappers around PKGMGR / COM_* / OPTS_* / CHECK_CHAIN.
# PKGMGR empty means "no package manager" (nix, slackware): operations are
# silent no-ops that return 0.
# -----------------------------------------------------------------------------

deps_is_installed() {
    [ -n "${PKGMGR:-}" ] || return 0
    local _bin="${PKGMGR_CHECK:-$PKGMGR}"
    local _entry _com _opts
    if [ -v CHECK_CHAIN ] && [ ${#CHECK_CHAIN[@]} -gt 0 ]; then
        for _entry in "${CHECK_CHAIN[@]}"; do
            _com="${_entry%%|*}"
            _opts="${_entry#*|}"
            # shellcheck disable=SC2086
            $_bin $_com $_opts "$1" >/dev/null 2>&1 && return 0
        done
        return 1
    fi
    # No CHECK_CHAIN: fall back to a single COM_CHECK/OPTS_CHECK pair.
    # shellcheck disable=SC2086
    $_bin ${COM_CHECK:-} ${OPTS_CHECK:-} "$1" >/dev/null 2>&1
}

deps_update() {
    [ -n "${PKGMGR:-}" ] || return 0
    [ -n "${COM_UPDATE:-}" ] || return 0
    # shellcheck disable=SC2086
    $SUDO $PKGMGR $COM_UPDATE $OPTS_UPDATE
}

deps_upgrade() {
    [ -n "${PKGMGR:-}" ] || return 0
    [ -n "${COM_UPGRADE:-}" ] || return 0
    # shellcheck disable=SC2086
    $SUDO $PKGMGR $COM_UPGRADE ${OPTS_UPGRADE:-}
}

_DEPS_UPGRADED=0
deps_upgrade_with_prompt() {
    [ "${_DEPS_UPGRADED:-0}" = 1 ] && return 0
    _DEPS_UPGRADED=1
    [ -n "${COM_UPGRADE:-}" ] || return 0

    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        if [ "${IPTVPLAYER_UPGRADE:-0}" = "1" ]; then
            log "Upgrading installed packages (IPTVPLAYER_UPGRADE=1)..."
            deps_upgrade || warn "deps_upgrade failed, continuing"
        else
            log "Noninteractive: skipping system upgrade (IPTVPLAYER_UPGRADE=1 to enable)"
        fi
        return 0
    fi

    if ask "Update installed packages before installing build deps? [Y/n]:" y; then
        log "Upgrading installed packages (this can take a while)..."
        deps_upgrade || warn "deps_upgrade failed, continuing"
        warn "If kernel/systemd/glibc were upgraded, reboot before building."
    else
        warn "Skipping system upgrade - version conflicts are possible."
    fi
    return 0
}

# Called once from make.sh before dispatch. Skipped in CI (workflow YAML
# installs packages) and with --skip-system.
system_prepare() {
    [ "${OPT_SKIP_SYSTEM:-false}" = true ] && return 0
    if [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ]; then
        log "CI detected: system prepare skipped (workflow installs packages)"
        return 0
    fi
    [ -n "${PKGMGR:-}" ] || return 0

    log "Updating package lists (this can take a while)..."
    deps_update || warn "deps_update failed, continuing"
    deps_upgrade_with_prompt

    if declare -F distro_setup_repos >/dev/null 2>&1; then
        distro_setup_repos
    fi
    return 0
}

deps_install() {
    [ -n "${PKGMGR:-}" ] || return 0
    # shellcheck disable=SC2086
    $SUDO $PKGMGR $COM_INSTALL $OPTS_INSTALL "$@"
}

deps_remove() {
    [ -n "${PKGMGR:-}" ] || return 0
    # shellcheck disable=SC2086
    $SUDO ${PKGMGR_REMOVE:-$PKGMGR} $COM_REMOVE $OPTS_REMOVE "$@"
}

# -----------------------------------------------------------------------------
# check_deps - warn about missing packaging tools. Does not install.
# -----------------------------------------------------------------------------
check_deps() {
    local need_native="$1"
    local need_appimage="$2"
    local need_sharun="$3"
    local need_flatpak="$4"

    local required=() optional=()

    local pair tool pkg
    for pair in "tar:tar" "readelf:binutils"; do
        tool="${pair%%:*}"; pkg="${pair#*:}"
        command -v "$tool" >/dev/null 2>&1 || required+=("$tool ($pkg)")
    done

    if [ "$need_native" = true ] && [ -v REQUIRED_TOOLS ] && [ ${#REQUIRED_TOOLS[@]} -gt 0 ]; then
        local _rt
        for _rt in "${REQUIRED_TOOLS[@]}"; do
            tool="${_rt%%:*}"; pkg="${_rt#*:}"
            command -v "$tool" >/dev/null 2>&1 || required+=("$tool ($pkg)")
        done
    fi

    if [ -n "${GPG_KEY_ID:-}" ]; then
        command -v gpg >/dev/null 2>&1 || required+=("gpg")
    fi

    if [ -d "$PROJECT_ROOT/po" ] && ls "$PROJECT_ROOT"/po/*.po >/dev/null 2>&1; then
        command -v msgfmt  >/dev/null 2>&1 || required+=("msgfmt (gettext)")
        command -v msgmerge >/dev/null 2>&1 || required+=("msgmerge (gettext)")
    fi

    if [ "$need_appimage" = true ] || [ "$need_sharun" = true ]; then
        command -v wget >/dev/null 2>&1 || required+=("wget")
    fi
    if [ "$need_sharun" = true ]; then
        command -v patchelf >/dev/null 2>&1 || required+=("patchelf")
    fi
    if [ "$need_flatpak" = true ]; then
        command -v flatpak-builder >/dev/null 2>&1 || required+=("flatpak-builder")
        command -v flatpak         >/dev/null 2>&1 || required+=("flatpak")
    fi

    if [ "$need_appimage" = true ] && ! command -v zsyncmake >/dev/null 2>&1; then
        optional+=("zsyncmake")
    fi

    if [ ${#required[@]} -ne 0 ]; then
        if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
            error "Missing required tools: ${required[*]}. CI image must install them in advance."
        fi
        warn "Missing required tools: ${required[*]}"
        warn "Auto install on build step."
    fi

    if [ ${#optional[@]} -ne 0 ]; then
        log "Missing optional tools: ${optional[*]}. Install them if needed."
    fi
}

# -----------------------------------------------------------------------------
# ensure_tools - check and install packaging tools.
# Format of args: "tool:package".
# -----------------------------------------------------------------------------
ensure_tools() {
    local desc="$1"; shift
    local -a missing_tools=() missing_pkgs=()
    local pair tool pkg
    for pair in "$@"; do
        tool="${pair%%:*}"; pkg="${pair#*:}"
        if ! command -v "$tool" >/dev/null 2>&1; then
            missing_tools+=("$tool")
            missing_pkgs+=("$pkg")
        fi
    done
    [ ${#missing_tools[@]} -eq 0 ] && return 0

    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        error "Missing required tools for $desc: ${missing_tools[*]}. CI image must install them in advance."
    fi
    if [ -z "${PKGMGR:-}" ]; then
        error "Missing tools for $desc: ${missing_tools[*]}. No package manager configured for $DISTRO."
    fi

    warn "Missing tools for $desc: ${missing_tools[*]}"
    if ! ask "Install (${missing_pkgs[*]})? [Y/n]:" y; then
        error "Cannot continue without ${missing_tools[*]}."
    fi

    log "Installing packages: ${missing_pkgs[*]}"
    if ! deps_install "${missing_pkgs[@]}"; then
        error "Failed to install ${missing_pkgs[*]}."
    fi
    hash -r

    local _state="${PROJECT_ROOT}/.iptvplayer-deps-installed"
    touch "$_state"
    printf '%s\n' "${missing_pkgs[@]}" >> "$_state"
    sort -u "$_state" -o "$_state"

    local t
    for t in "${missing_tools[@]}"; do
        command -v "$t" >/dev/null 2>&1 || error "After installation $t is still not in PATH."
    done
    log "Installed: ${missing_pkgs[*]}"
}

# -----------------------------------------------------------------------------
# setup_dirs - reset staging and AppDir, ensure OUTPUT_DIR exists.
# Does not touch install/ (that is --rebuild's job).
# -----------------------------------------------------------------------------
setup_dirs() {
    rm -rf "$STAGING_DIR" "$APPDIR"
    mkdir -p "$OUTPUT_DIR"
    mkdir -p "$STAGING_DIR"
}

# -----------------------------------------------------------------------------
# sign_files - sign artifacts (if GPG_KEY_ID) and always write checksums.txt.
# Individual signing failures become warnings, not errors.
# -----------------------------------------------------------------------------
sign_files() {
    local dist_dir="$OUTPUT_DIR"
    local checksum_basename="checksums.txt"
    local signature_basename="${checksum_basename}.asc"
    local checksum_file="$dist_dir/$checksum_basename"
    local signature_file="$dist_dir/$signature_basename"

    local can_sign=false
    if ! command -v gpg >/dev/null 2>&1; then
        warn "gpg not installed"
    elif [ -z "${GPG_KEY_ID:-}" ]; then
        if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
            echo "::warning::GPG_KEY_ID is not set." >&2
        else
            warn "GPG_KEY_ID is not set"
        fi
    else
        can_sign=true
    fi

    if [ "$can_sign" = true ]; then
        local gpg_sign="gpg"
        if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -x "$GPG_WRAPPER_DIR/gpg" ]; then
            gpg_sign="$GPG_WRAPPER_DIR/gpg"
        fi

        local gpg_real="/usr/bin/gpg"
        [ -x "$gpg_real" ] || gpg_real="$(command -v gpg)"

        log "Signing packages (key: $GPG_KEY_ID)..."

        # --- .deb: inline via debsigs + detached .asc ---
        local file
        for file in "$dist_dir"/*.deb; do
            [ -f "$file" ] || continue
            if command -v debsigs >/dev/null 2>&1; then
                if debsigs --sign=origin --default-key="$GPG_KEY_ID" "$file"; then
                    if ! debsigs --verify "$file" >/dev/null 2>&1; then
                        warn "debsigs did not confirm $(basename "$file") - .deb will ship without inline signature"
                    fi
                else
                    warn "debsigs failed on $(basename "$file") - .deb will ship without inline signature"
                fi
            else
                warn "debsigs not installed - .deb will only have detached .asc"
            fi

            "$gpg_sign" --yes --detach-sign --armor \
                --local-user "$GPG_KEY_ID" \
                --output "$file.asc" "$file" \
              || warn "detached .asc not created for $(basename "$file")"
        done

        # --- .rpm: inline via rpm --addsign + detached .asc ---
        if command -v rpm >/dev/null 2>&1; then
            local passphrase_file=""
            if [ -n "${GPG_WRAPPER_DIR:-}" ] && [ -f "$GPG_WRAPPER_DIR/pass" ]; then
                passphrase_file="$GPG_WRAPPER_DIR/pass"
            fi

            local sign_cmd='%{__gpg} -u "%{_gpg_name}" -sbo %{__signature_filename} %{__plaintext_filename}'
            if [ -n "$passphrase_file" ]; then
                sign_cmd="%{__gpg} --batch --no-tty --pinentry-mode loopback --passphrase-file $passphrase_file -u \"%{_gpg_name}\" -sbo %{__signature_filename} %{__plaintext_filename}"
            fi

            for file in "$dist_dir"/*.rpm; do
                [ -f "$file" ] || continue
                if rpm --addsign \
                    --define "_gpg_name $GPG_KEY_ID" \
                    --define "_signature gpg" \
                    --define "__gpg $gpg_real" \
                    --define "__gpg_check_password_cmd /bin/true" \
                    --define "__gpg_sign_cmd $sign_cmd" \
                    "$file"; then
                    if rpm --checksig "$file" 2>&1 | grep -q 'signatures OK'; then
                        log "rpm --checksig OK: $(basename "$file")"
                    else
                        warn "rpm --checksig did NOT confirm $(basename "$file")"
                    fi
                else
                    warn "rpm --addsign failed on $(basename "$file")"
                fi

                "$gpg_sign" --yes --detach-sign --armor \
                    --local-user "$GPG_KEY_ID" \
                    --output "$file.asc" "$file" \
                  || warn "detached .asc not created for $(basename "$file")"
            done
        fi

        # --- .AppImage: detached .asc only ---
        for file in "$dist_dir"/*.AppImage; do
            [ -f "$file" ] || continue
            "$gpg_sign" --yes --detach-sign --armor \
                --local-user "$GPG_KEY_ID" \
                --output "$file.asc" "$file" \
              || warn "detached .asc not created for $(basename "$file")"
        done
    fi

    log "Generating checksums.txt"
    rm -f "$checksum_file" "$signature_file"
    (
        cd "$dist_dir" || exit 1
        shopt -s nullglob
        local -a files=()
        for f in *; do
            case "$f" in "$checksum_basename"|"$signature_basename") continue ;; esac
            [ -f "$f" ] || continue
            files+=("$f")
        done
        (( ${#files[@]} > 0 )) && sha256sum "${files[@]}"
    ) > "$checksum_file" || true

    if [ "$can_sign" = true ]; then
        "$gpg_sign" --yes --detach-sign --armor \
            --local-user "$GPG_KEY_ID" \
            --output "$signature_file" "$checksum_file" || true
    fi
}