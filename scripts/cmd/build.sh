#!/usr/bin/env bash
# =============================================================================
# cmd/build.sh - --build command and binary build helpers.
# =============================================================================
# Public functions:
#   bin_run            - full --build run
#   bin_resolve_paths  - compute BUILD_DIR, INSTALL_DIR, EXE_PATH, CMAKE_*
#   bin_ensure_binary  - cmake configure + build + install + strip + verify
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "cmd/build.sh is a library, do not run directly." >&2
    exit 1
fi

# -----------------------------------------------------------------------------
# bin_run - public (--build)
# -----------------------------------------------------------------------------
bin_run() {
    if [ "$OPT_LOG" = true ]; then
        setup_log_fifo
        log "Log: $LOG_FILE"
    fi

    if ! command -v cmake >/dev/null 2>&1; then
        warn "cmake not found in PATH (will be installed by deps)"
    fi

    local make_cmd="make"
    if command -v ninja >/dev/null 2>&1; then
        make_cmd="ninja"
    fi
    log "Use generator: $make_cmd"

    bin_resolve_paths
    cd "$PROJECT_ROOT" || return 1

    if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
        log "Noninteractive: skipping dependency setup"
    else
        deps_ensure_for_build
    fi

    local rebuild=true
    if [ "$OPT_REBUILD" = true ]; then
        rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGING_DIR" "$APPDIR"
        rm -rf "$PROJECT_ROOT/AppDir"
        log "--rebuild: removed $BUILD_DIR, $INSTALL_DIR, staging, AppDir"
    elif [ -x "$EXE_PATH" ]; then
        if [[ "${NON_INTERACTIVE:-false}" == true ]]; then
            log "Noninteractive: binary exists, using it"
            rebuild=false
        elif ask "Binary exists ($EXE_PATH). Rebuild? [y/N]:" n; then
            rm -rf "$BUILD_DIR" "$INSTALL_DIR" "$STAGING_DIR" "$APPDIR"
            rm -rf "$PROJECT_ROOT/AppDir"
            log "Rebuilding binary"
        else
            log "Using existing binary"
            rebuild=false
        fi
    fi

    if [ "$rebuild" = false ]; then
        log "Build not required"
        echo "Run: cd $INSTALL_DIR/bin && ./$PACKAGE_NAME"
        return 0
    fi

    bin_ensure_binary || return 1

    cp "$BUILD_DIR/compile_commands.json" "$PROJECT_ROOT/" 2>/dev/null || true

    log "Build complete"
    echo "Run: cd $INSTALL_DIR/bin && ./$PACKAGE_NAME"
    return 0
}

# -----------------------------------------------------------------------------
# bin_resolve_paths - public helper
# -----------------------------------------------------------------------------
bin_resolve_paths() {
    local type="${OPT_BUILD_TYPE:-release}"
    case "$type" in
        release) CMAKE_BUILD_TYPE="Release" ;;
        debug)   CMAKE_BUILD_TYPE="Debug" ;;
        *)       error "Invalid build type: $type (release|debug)" ;;
    esac
    BUILD_DIR="$PROJECT_ROOT/build-$type"
    INSTALL_DIR="$PROJECT_ROOT/install"
    EXE_PATH="$INSTALL_DIR/bin/$PACKAGE_NAME"
    : "${CMAKE_CXX_STANDARD:=20}"
}

# -----------------------------------------------------------------------------
# bin_ensure_binary - public helper
# -----------------------------------------------------------------------------
bin_ensure_binary() {
    if [ -z "${BUILD_DIR:-}" ] || [ -z "${INSTALL_DIR:-}" ] || [ -z "${EXE_PATH:-}" ]; then
        bin_resolve_paths
    fi

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR" || return 1

    : "${CMAKE_CXX_STANDARD:=20}"
    log "C++ standard: $CMAKE_CXX_STANDARD"

    if ! cmake "$PROJECT_ROOT" \
        -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_CXX_STANDARD="$CMAKE_CXX_STANDARD" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON; then
        error "CMake configure failed"
    fi

    if ! cmake --build . --config "$CMAKE_BUILD_TYPE" --target all -j"$(nproc)"; then
        error "Build failed"
    fi

    if ! cmake --build . --config "$CMAKE_BUILD_TYPE" --target install; then
        error "Install failed"
    fi

    if [ "$CMAKE_BUILD_TYPE" = "Release" ] || [ "$CMAKE_BUILD_TYPE" = "MinSizeRel" ]; then
        log "Stripping debug symbols"
        strip --strip-all \
            -R .comment -R .note -R .note.ABI-tag \
            "$EXE_PATH" 2>/dev/null || warn "strip failed"
    fi

    if [ ! -x "$EXE_PATH" ]; then
        error "Executable not found: $EXE_PATH"
    fi

    cd "$PROJECT_ROOT" || return 1
    log "Binary: $EXE_PATH"
    return 0
}

# -----------------------------------------------------------------------------
# setup_log_fifo - private
# -----------------------------------------------------------------------------
setup_log_fifo() {
    LOG_FILE="$PROJECT_ROOT/build_$(date +%Y%m%d_%H%M%S).log"

    local _log_fifo
    _log_fifo="$(mktemp -u -t iptvplayer-log.XXXXXX)"
    if ! mkfifo "$_log_fifo" 2>/dev/null; then
        warn "Failed to create FIFO - log disabled"
        rm -f "$_log_fifo"
        LOG_FILE=""
        return 1
    fi

    exec 3>&2
    tee -a "$LOG_FILE" < "$_log_fifo" >&3 &
    _LOG_TEE_PID=$!
    exec > "$_log_fifo" 2>&1

    # shellcheck disable=SC2064
    trap 'exec 1>&- 2>&- 3>&-; { wait "$_LOG_TEE_PID"; } 2>/dev/null || true; rm -f "$_log_fifo"' EXIT

    return 0
}