#!/usr/bin/env bash
# =============================================================================
# cmd/deps.sh - --deps command and shared dependency helpers.
# =============================================================================
# Public functions:
#   deps_run                    - full --deps run (setup_repos + system + tools
#                                 + third_party + cleanup)
#   deps_ensure_for_build       - prepare everything needed for build
#                                 (interactive: install; non-interactive: check)
#   deps_cleanup_after_success  - remove packages recorded in
#                                 .iptvplayer-deps-installed (always returns 0)
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "cmd/deps.sh is a library, do not run directly." >&2
    exit 1
fi

# -----------------------------------------------------------------------------
# deps_run - public (--deps)
# -----------------------------------------------------------------------------
deps_run() {
    # detect_distro / detect_sudo / system_prepare (update + upgrade +
    # distro_setup_repos) already ran once in make.sh.

    if [ "$OPT_SKIP_SYSTEM" = true ]; then
        log "Skipping system packages (--skip-system)"
        log "Skipping build tools check (--skip-system)"
    else
        deps_ensure_system
        deps_ensure_tools
    fi

    if [ "$OPT_REBUILD" = true ]; then
        rm -rf "$PROJECT_ROOT/third_party/wx" "$PROJECT_ROOT/third_party/wxsqlite3"
        log "Removed third_party (--rebuild)"
    fi

    deps_ensure_third_party
    return 0
}

# -----------------------------------------------------------------------------
# deps_ensure_for_build - public (called by bin_run, package_run)
# -----------------------------------------------------------------------------
deps_ensure_for_build() {
    # detect_distro / detect_sudo / system_prepare (update + upgrade +
    # distro_setup_repos) already ran once in make.sh.
    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        deps_check_prerequisites
        return 0
    fi

    deps_ensure_system
    deps_ensure_tools
    deps_ensure_third_party
    log "Build prerequisites ensured"
    return 0
}

# -----------------------------------------------------------------------------
# deps_cleanup_after_success - public (called at the end of a successful run)
# Always returns 0. Never blocks a successful command.
# -----------------------------------------------------------------------------
deps_cleanup_after_success() {
    if [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ]; then
        return 0
    fi
    if [ "${IPTVPLAYER_CLEANUP_DEPS:-1}" = "0" ]; then
        log "IPTVPLAYER_CLEANUP_DEPS=0: keeping build dependencies"
        return 0
    fi

    local state="$PROJECT_ROOT/.iptvplayer-deps-installed"
    [ -f "$state" ] || return 0

    local -a pkgs=()
    local _p
    while IFS= read -r _p; do
        [ -z "$_p" ] && continue
        case "$_p" in \#*) continue ;; esac
        pkgs+=("$_p")
    done < "$state"

    if [ ${#pkgs[@]} -eq 0 ]; then
        rm -f "$state" "$PROJECT_ROOT/.iptvplayer-deps-failed"
        return 0
    fi

    # Avoid detect_sudo (it exits); we must keep going on a successful command.
    if [ "$(id -u)" != "0" ]; then
        if ! command -v sudo >/dev/null 2>&1; then
            warn "Cannot remove packages: sudo is not installed. Packages and state left intact."
            return 0
        fi
        SUDO="sudo"
    else
        SUDO=""
    fi

    log "Removing installed packages (${#pkgs[@]} items)"

    if deps_remove "${pkgs[@]}" >/dev/null 2>&1; then
        rm -f "$state" "$PROJECT_ROOT/.iptvplayer-deps-failed"
        log "Packages removed (${#pkgs[@]} items)"
        return 0
    fi

    warn "Batch removal failed - trying one by one"
    local -a failed=()
    for _p in "${pkgs[@]}"; do
        deps_remove "$_p" >/dev/null 2>&1 || failed+=("$_p")
    done

    if [ ${#failed[@]} -eq 0 ]; then
        rm -f "$state" "$PROJECT_ROOT/.iptvplayer-deps-failed"
        log "All packages removed"
        return 0
    fi

    local fail_file="$PROJECT_ROOT/.iptvplayer-deps-failed"
    {
        echo "# Auto remove failed for these packages."
        echo "# Remove manually, e.g.:"
        echo "#   sudo dnf remove <pkg1> <pkg2>       (Fedora/RHEL/RedOS/ROSA)"
        echo "#   sudo apt-get remove <pkg1> <pkg2>   (Debian/Ubuntu/ALT)"
        echo "#   sudo zypper remove <pkg1> <pkg2>    (openSUSE/SLES)"
        echo "#   sudo pacman -R <pkg1> <pkg2>        (Arch/CachyOS/Manjaro)"
        echo "#   sudo apk del <pkg1> <pkg2>          (Alpine)"
        echo "# After successful removal - delete this file."
        printf '%s\n' "${failed[@]}"
    } > "$fail_file"

    warn "Failed to remove ${#failed[@]} of ${#pkgs[@]} packages:"
    printf '  - %s\n' "${failed[@]}" >&2
    warn "List saved: $fail_file"

    printf '%s\n' "${failed[@]}" > "$state"
    return 0
}

# -----------------------------------------------------------------------------
# Private helpers
# -----------------------------------------------------------------------------

deps_ensure_system() {
    if [ -z "${DEPS:-}" ]; then
        log "DEPS is empty for $DISTRO, skipping system packages"
        return 0
    fi

    local -a missing=() missing_labels=()
    local label pkgs pkg found

    while IFS=: read -r label pkgs; do
        [ -z "$label" ] && continue
        found=false
        # shellcheck disable=SC2086
        for pkg in $pkgs; do
            if deps_is_installed "$pkg"; then found=true; break; fi
        done
        if [ "$found" = true ]; then
            log "OK: $label"
        else
            warn "MISSING: $label ($pkgs)"
            missing_labels+=("$label")
            # shellcheck disable=SC2206
            missing+=($pkgs)
        fi
    done <<< "$DEPS"

    if [ ${#missing[@]} -eq 0 ]; then
        log "All required packages already installed"
        return 0
    fi

    echo "Installation required (${missing_labels[*]}):"
    printf '  - %s\n' "${missing[@]}"

    local response=""
    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        response="y"
    else
        read -p "Continue installation? [Y/n] " -r response || true
        [ -z "$response" ] && response="y"
    fi
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        warn "Continuing without installing missing packages."
        warn "If a package is genuinely missing, build will fail."
        return 0
    fi

    log "Installing packages: ${missing[*]}"
    if ! deps_install "${missing[@]}"; then
        error "Failed to install: ${missing[*]}"
    fi
    hash -r

    local state="$PROJECT_ROOT/.iptvplayer-deps-installed"
    touch "$state"
    printf '%s\n' "${missing[@]}" >> "$state"
    sort -u "$state" -o "$state"

    log "Installed: ${missing[*]}"
    return 0
}

deps_ensure_tools() {
    local -a _missing=()
    command -v cmake      >/dev/null 2>&1 || _missing+=(cmake)
    command -v autoreconf >/dev/null 2>&1 || _missing+=(autoreconf)
    command -v git        >/dev/null 2>&1 || log "git not found - VERSION will not include commit hash"

    [ ${#_missing[@]} -eq 0 ] && return 0

    if [ "${OPT_SKIP_SYSTEM:-false}" = true ]; then
        error "Missing tools: ${_missing[*]}. Cannot install (--skip-system). Install manually."
    fi

    local -a _to_install=()
    local _t
    for _t in "${_missing[@]}"; do
        case "$_t" in
            autoreconf) _to_install+=("autoconf" "automake" "libtool") ;;
            *)          _to_install+=("$_t") ;;
        esac
    done

    warn "Missing required tools: ${_missing[*]}"
    local response=""
    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        response="y"
    else
        read -p "Install now (${_to_install[*]})? [Y/n] " -r response || true
        [ -z "$response" ] && response="y"
    fi
    if [[ ! "$response" =~ ^[Yy]$ ]]; then
        error "Cannot build without ${_missing[*]}."
    fi

    log "Installing packages: ${_to_install[*]}"
    if ! deps_install "${_to_install[@]}"; then
        error "Failed to install ${_missing[*]}"
    fi
    hash -r

    local _still=()
    for _t in "${_missing[@]}"; do
        command -v "$_t" >/dev/null 2>&1 || _still+=("$_t")
    done
    if [ ${#_still[@]} -gt 0 ]; then
        error "After installation ${_still[*]} is still not in PATH."
    fi
    log "Installed: ${_to_install[*]}"
    return 0
}

deps_check_prerequisites() {
    local -a _missing=()
    local label pkgs pkg found

    if [ -n "${DEPS:-}" ]; then
        while IFS=: read -r label pkgs; do
            [ -z "$label" ] && continue
            found=false
            # shellcheck disable=SC2086
            for pkg in $pkgs; do
                if deps_is_installed "$pkg"; then found=true; break; fi
            done
            [ "$found" = true ] || _missing+=("$label ($pkgs)")
        done <<< "$DEPS"
    fi

    command -v cmake      >/dev/null 2>&1 || _missing+=(cmake)
    command -v autoreconf >/dev/null 2>&1 || _missing+=(autoreconf)
    command -v git        >/dev/null 2>&1 || log "git not found - VERSION will not include commit hash"

    local wx_version="${WX_VERSION:-3.3.2}"
    [ -f "$PROJECT_ROOT/third_party/wx/install/bin/wx-config" ] \
        || _missing+=(wxWidgets)
    if ! ls "$PROJECT_ROOT/third_party/wxsqlite3/install/lib/libwxcode_gtk3u_wxsqlite3-${wx_version%.*}.a" >/dev/null 2>&1; then
        _missing+=(wxSQLite3)
    fi

    if [ ${#_missing[@]} -eq 0 ]; then
        log "All build prerequisites present"
        return 0
    fi

    error "Missing build prerequisites (noninteractive): ${_missing[*]}."
}

deps_ensure_third_party() {
    mkdir -p "$PROJECT_ROOT/third_party"
    cd "$PROJECT_ROOT" || return 1
    deps_ensure_wx
    deps_ensure_wxsqlite3
    log "third_party ready"
    return 0
}

deps_ensure_wx() {
    local wx_version="${WX_VERSION:-3.3.2}"
    local wx_dir="$PROJECT_ROOT/third_party/wx"
    local wx_artifact="$wx_dir/install/lib/libwx_gtk3u_core-${wx_version%.*}.a"

    if [ -d "$wx_dir" ] && [ -f "$wx_artifact" ]; then
        if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
            log "wxWidgets already built, skipping"
            return 0
        fi
        if ask "Rebuild wxWidgets? [y/N]:" n; then
            rm -rf "$wx_dir"
        else
            log "Using existing wxWidgets"
            return 0
        fi
    elif [ -d "$wx_dir" ] && [ ! -f "$wx_artifact" ]; then
        log "wxWidgets: directory exists but artifact missing - rebuild"
        rm -rf "$wx_dir"
    fi

    mkdir -p "$wx_dir"
    cd "$wx_dir" || return 1

    local wx_archive="wxWidgets-${wx_version}.tar.bz2"
    local wx_url="https://github.com/wxWidgets/wxWidgets/releases/download/v${wx_version}/${wx_archive}"

    if ! download "$wx_url" "$wx_archive"; then
        error "Failed to download wxWidgets"
    fi

    tar xf "$wx_archive" && rm "$wx_archive" || error "Failed to unpack wxWidgets"
    mv "wxWidgets-${wx_version}" src || error "wxWidgets: source directory not found"
    mkdir -p build install
    cd build || return 1

    export CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-std=c++20"
    if ! cmake ../src \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DwxBUILD_SHARED=OFF \
        -DwxUSE_GLCANVAS=ON \
        -DwxUSE_LIBWEBP=builtin \
        -DwxUSE_LIBJPEG=builtin \
        -DwxUSE_SVG=ON \
        -DwxUSE_REGEX=OFF \
        -DwxUSE_MEDIACTRL=OFF \
        -DwxUSE_GSTREAMER=OFF \
        -DwxUSE_WEBKIT=OFF \
        -DwxUSE_WEBVIEW_WEBKIT=OFF \
        -DwxUSE_LIBTIFF=OFF \
        -DwxUSE_GIF=OFF \
        -DwxUSE_ACCESSIBILITY=OFF \
        -DwxUSE_HELP=OFF \
        -DwxUSE_HTML=OFF \
        -DwxUSE_STC=OFF \
        -DwxUSE_MS_HTML_HELP=OFF \
        -DwxUSE_WXHTML_HELP=OFF \
        -DwxUSE_XRC=OFF \
        -DwxUSE_XML=OFF \
        -DwxUSE_NET=OFF \
        -DwxUSE_RICHTEXT=OFF \
        -DwxUSE_LIBSDL=OFF \
        -DwxUSE_PRINTING_ARCHITECTURE=OFF \
        -DwxBUILD_SAMPLES=OFF \
        -DwxBUILD_DEMOS=OFF \
        -DwxBUILD_TESTS=OFF \
        -DwxBUILD_BENCHMARKS=OFF \
        -DCMAKE_INSTALL_PREFIX=../install; then
        error "wxWidgets cmake configure failed"
    fi

    if ! cmake --build . --target install -j"$(nproc)"; then
        error "wxWidgets build failed"
    fi

    if [ ! -f "$wx_dir/install/bin/wx-config" ]; then
        error "wx-config not found in $wx_dir/install/bin. wxWidgets build failed."
    fi

    log "wxWidgets installed: $wx_dir/install"
    cd "$PROJECT_ROOT" || return 1
    return 0
}

deps_ensure_wxsqlite3() {
    local wx_version="${WX_VERSION:-3.3.2}"
    local wxsqlite3_version="${WXSQLITE3_VERSION:-5.0.1}"
    local wxsqlite3_dir="$PROJECT_ROOT/third_party/wxsqlite3"
    local wxsqlite3_artifact="$wxsqlite3_dir/install/lib/libwxcode_gtk3u_wxsqlite3-${wx_version%.*}.a"

    if [ -d "$wxsqlite3_dir" ] && [ -f "$wxsqlite3_artifact" ]; then
        if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
            log "wxSQLite3 already built, skipping"
            return 0
        fi
        if ask "Rebuild wxSQLite3? [y/N]:" n; then
            rm -rf "$wxsqlite3_dir"
        else
            log "Using existing wxSQLite3"
            return 0
        fi
    elif [ -d "$wxsqlite3_dir" ] && [ ! -f "$wxsqlite3_artifact" ]; then
        log "wxSQLite3: directory exists but artifact missing - rebuild"
        rm -rf "$wxsqlite3_dir"
    fi

    mkdir -p "$wxsqlite3_dir"
    cd "$wxsqlite3_dir" || return 1

    local wxsqlite3_archive="v${wxsqlite3_version}.tar.gz"
    local -a wxsqlite3_urls=(
        "https://codeload.github.com/utelle/wxsqlite3/tar.gz/refs/tags/v${wxsqlite3_version}"
        "https://github.com/utelle/wxsqlite3/archive/refs/tags/${wxsqlite3_archive}"
    )

    DOWNLOAD_UA="Mozilla/5.0"
    if ! download_multi "$wxsqlite3_archive" "${wxsqlite3_urls[@]}"; then
        unset DOWNLOAD_UA
        error "Failed to download wxSQLite3"
    fi
    unset DOWNLOAD_UA

    tar xf "$wxsqlite3_archive" && rm "$wxsqlite3_archive" || error "Failed to unpack wxSQLite3"

    if [ -d "wxsqlite3-${wxsqlite3_version}" ]; then
        mv "wxsqlite3-${wxsqlite3_version}" src
    elif [ -d "wxSQLite3-${wxsqlite3_version}" ]; then
        mv "wxSQLite3-${wxsqlite3_version}" src
    else
        error "wxSQLite3: unpacked directory not found"
    fi

    cd src || return 1
    log "Running autoreconf for wxSQLite3 (this can take a while)..."
    if ! autoreconf -fi; then
        error "wxSQLite3 autoreconf failed"
    fi

    local wx_root_abs="$PROJECT_ROOT/third_party/wx/install"
    mkdir -p ../build
    cd ../build || return 1

    export CXXFLAGS="${CXXFLAGS:+$CXXFLAGS }-std=c++20"
    if ! ../src/configure \
        --prefix="$wxsqlite3_dir/install" \
        --with-wx-config="$wx_root_abs/bin/wx-config" \
        --disable-shared \
        --enable-static \
        --without-sqlcipher \
        --without-aes128cbc \
        --without-ascon128 \
        --without-aegis; then
        error "wxSQLite3 configure failed"
    fi

    local lib_target
    lib_target=$(grep -E '^[[:space:]]*lib.*_LTLIBRARIES' Makefile 2>/dev/null \
                 | sed 's/.*=//' | awk '{print $1}' | head -1 | xargs || true)
    if [ -z "$lib_target" ]; then
        lib_target=$(find . -maxdepth 1 -type f -name "*.la" -printf "%f\n" | head -1 || true)
    fi
    if [ -z "$lib_target" ]; then
        error "wxSQLite3: failed to determine library target from Makefile"
    fi

    if ! make -j"$(nproc)" "$lib_target" || ! make install-exec; then
        error "wxSQLite3 make failed"
    fi

    if [ ! -d "$wxsqlite3_dir/install/include/wx" ]; then
        log "wxSQLite3: copying headers manually"
        mkdir -p "$wxsqlite3_dir/install/include/wx"
        cp -r "$wxsqlite3_dir/src/include/wx/"* "$wxsqlite3_dir/install/include/wx/"
    fi

    log "wxSQLite3 installed: $wxsqlite3_dir/install"
    cd "$PROJECT_ROOT" || return 1
    return 0
}