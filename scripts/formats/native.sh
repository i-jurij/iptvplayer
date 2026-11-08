#!/usr/bin/env bash
# =============================================================================
# formats/native.sh - native package dispatcher.
# =============================================================================
# Thin wrapper around distro_build from distros/<name>.sh.
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "formats/native.sh is a library, do not run directly." >&2
    exit 1
fi

build_native() {
    section "Native package ($PACK_FORMAT)"
    if ! declare -F distro_build >/dev/null 2>&1; then
        error "distros/${DISTRO_ADAPTER}.sh does not define distro_build"
    fi
    log "Building native package ($PACK_FORMAT) for $DISTRO"
    distro_build
}