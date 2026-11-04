#!/usr/bin/env bash
# =============================================================================
# cmd/clean.sh - --clean command. Local development only (not called in CI).
# =============================================================================
# Removes: staging artifacts, pkg-* directories, installed packages from
# .iptvplayer-deps-installed. Does NOT remove build-*/install/dist (that
# is --rebuild's job).
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "cmd/clean.sh is a library, do not run directly." >&2
    exit 1
fi

clean_run() {
    log "Cleaning staging artifacts"
    rm -rf "$PROJECT_ROOT"/pkg-rpm*
    rm -rf "$PROJECT_ROOT/pkg-apk" "$PROJECT_ROOT/pkg-slackbuild"
    rm -rf "$PROJECT_ROOT/pkg-xbps" "$PROJECT_ROOT/pkg-gentoo" "$PROJECT_ROOT/pkg-nix"
    rm -rf "$PROJECT_ROOT/pkg-staging"
    rm -rf "$PROJECT_ROOT/dist/.AppDir"
    rm -rf "$PROJECT_ROOT/AppDir"
    rm -f  "$PROJECT_ROOT/compile_commands.json"
    log "Staging artifacts removed"

    if [ "$OPT_WITH_THIRD_PARTY" = true ]; then
        rm -rf "$PROJECT_ROOT/third_party"
        log "third_party removed"
    fi

    local state="$PROJECT_ROOT/.iptvplayer-deps-installed"
    if [ ! -f "$state" ]; then
        log "No installed packages state - nothing to remove"
        rm -f "$PROJECT_ROOT/.iptvplayer-deps-failed"
        return 0
    fi

    detect_distro

    if [ "$(id -u)" != "0" ]; then
        if command -v sudo >/dev/null 2>&1; then
            SUDO="sudo"
        else
            warn "Cannot remove packages: sudo is not installed. Packages and state left intact."
            return 1
        fi
    else
        SUDO=""
    fi

    local -a pkgs=()
    local _p
    while IFS= read -r _p; do
        [ -z "$_p" ] && continue
        case "$_p" in \#*) continue ;; esac
        pkgs+=("$_p")
    done < "$state"

    if [ ${#pkgs[@]} -eq 0 ]; then
        rm -f "$state" "$PROJECT_ROOT/.iptvplayer-deps-failed"
        log "State empty - removed"
        return 0
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
    warn "Remove manually: ${PKGMGR:-} ${COM_REMOVE:-} ${OPTS_REMOVE:-} ${failed[*]}"

    printf '%s\n' "${failed[@]}" > "$state"
    return 1
}