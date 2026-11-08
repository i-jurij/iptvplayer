#!/bin/sh
# =============================================================================
# make.sh - single entry point: --deps | --build | --package | --clean
# =============================================================================

. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/bootstrap-bash.sh" || exit 1

if [ -z "${BASH_VERSION:-}" ]; then
    echo "[!] bootstrap-bash.sh did not hand over control to bash." >&2
    exit 1
fi

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

source "$SCRIPT_DIR/detect.sh"
source "$SCRIPT_DIR/lib.sh"
source "$SCRIPT_DIR/cmd/deps.sh"
source "$SCRIPT_DIR/cmd/build.sh"
source "$SCRIPT_DIR/cmd/package.sh"
source "$SCRIPT_DIR/cmd/clean.sh"

# -----------------------------------------------------------------------------
# Global state
# -----------------------------------------------------------------------------
NON_INTERACTIVE=false
if [ -n "${CI:-}" ] || [ -n "${GITHUB_ACTIONS:-}" ] || [ ! -t 0 ]; then
    NON_INTERACTIVE=true
fi

PACKAGE_NAME="iptvplayer"
ICON_NAME="${PACKAGE_NAME}.svg"

_METAINFO_FILE="$PROJECT_ROOT/METAINFO_NAME"
if [ ! -f "$_METAINFO_FILE" ]; then
    echo "[!] File $_METAINFO_FILE not found" >&2
    exit 1
fi
METAINFO_NAME="$(tr -d '\n\r' < "$_METAINFO_FILE" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
if [ -z "$METAINFO_NAME" ]; then
    echo "[!] METAINFO_NAME is empty" >&2
    exit 1
fi

OUTPUT_DIR="$PROJECT_ROOT/dist"
STAGING_DIR="$PROJECT_ROOT/pkg-staging"
APPDIR="$PROJECT_ROOT/dist/.AppDir"

# -----------------------------------------------------------------------------
# Defaults
# -----------------------------------------------------------------------------
OPT_CMD=""
OPT_REBUILD=false
OPT_SKIP_SYSTEM=false
OPT_WITH_THIRD_PARTY=false
OPT_BUILD_TYPE=""
OPT_LOG=false
OPT_NATIVE=false
OPT_APPIMAGE=false
OPT_SHARUN=false
OPT_FLATPAK=false
OPT_NO_MENU=false

# -----------------------------------------------------------------------------
# Help
# -----------------------------------------------------------------------------
show_help() {
    cat << 'EOF'
Usage: ./scripts/make.sh <command> [options]

Commands:
  --deps              Install system packages and build third_party
                        options: --yes, --skip-system, --rebuild
  --build             Build binary
                        options: --yes, --rebuild, --type <release|debug>, --log
  --package           Package binary
                        options: --yes, --rebuild, --native, --appimage,
                                 --sharun, --flatpak, --no-menu
  --clean             Remove staging artifacts and installed packages
                        options: --with-third-party
  -h, --help          Show this help

Global options:
  --yes, -y           Noninteractive mode

Environment variables:
  DISTRO              Override distro detection (e.g. debian-12)
  DEB_ARCH            .deb arch for cross-packaging (e.g. arm64)
  RPM_ARCH            .rpm arch for cross-packaging (e.g. aarch64)
  APPIMAGE_ARCH       AppImage arch (x86_64 or aarch64 only)
  CC / CXX            Cross compilers
  CFLAGS / CXXFLAGS   Cross-compilation flags
  GPG_KEY_ID          Sign packages with this key
  CI, GITHUB_ACTIONS  Auto-enable noninteractive mode
  CMAKE_CXX_STANDARD  C++ standard (default 20)
  WX_VERSION          wxWidgets version (default 3.3.2)
  WXSQLITE3_VERSION   wxSQLite3 version (default 5.0.1)
  VOID_PACKAGES_DIR   Void Linux void-packages clone (default ~/void-packages)
  NIX_SYSTEM          Nix system (default from uname -m)
  FLATPAK_RUNTIME     Flatpak runtime (default org.gnome.Platform)
  FLATPAK_RUNTIME_VERSION  Flatpak runtime version (default 47)
  QUICK_SHARUN_REF    quick-sharun git ref (default main)

Examples:
  ./scripts/make.sh --deps
  ./scripts/make.sh --build --type debug --log
  ./scripts/make.sh --package --native
  ./scripts/make.sh --package --native --appimage
  ./scripts/make.sh --clean --with-third-party

Native build inside CI:
  ./scripts/make.sh --package --native --yes
EOF
}

# -----------------------------------------------------------------------------
# Parse argv
# -----------------------------------------------------------------------------
while [ $# -gt 0 ]; do
    case "$1" in
        --deps|--build|--package|--clean)
            if [ -n "$OPT_CMD" ]; then
                error "Multiple commands not allowed (already: --$OPT_CMD)"
            fi
            OPT_CMD="${1#--}"
            shift ;;
        --yes|-y)
            NON_INTERACTIVE=true
            shift ;;
        --rebuild)
            OPT_REBUILD=true
            shift ;;
        --skip-system)
            OPT_SKIP_SYSTEM=true
            shift ;;
        --with-third-party)
            OPT_WITH_THIRD_PARTY=true
            shift ;;
        --type)
            if [ -z "${2:-}" ]; then
                error "--type requires argument (release|debug)"
            fi
            case "$2" in
                release|debug) OPT_BUILD_TYPE="$2" ;;
                *) error "Invalid --type: $2 (release|debug)" ;;
            esac
            shift 2 ;;
        --log)
            OPT_LOG=true
            shift ;;
        --native)   OPT_NATIVE=true;   shift ;;
        --appimage) OPT_APPIMAGE=true; shift ;;
        --sharun)   OPT_SHARUN=true;   shift ;;
        --flatpak)  OPT_FLATPAK=true;  shift ;;
        --no-menu)  OPT_NO_MENU=true;  shift ;;
        -h|--help)
            show_help
            exit 0 ;;
        *)
            error "Unknown argument: $1 (use --help)" ;;
    esac
done

# -----------------------------------------------------------------------------
# Validate argv
# -----------------------------------------------------------------------------
if [ -z "$OPT_CMD" ]; then
    error "No command specified (--deps, --build, --package, --clean). Use --help."
fi

if [ "$OPT_SKIP_SYSTEM" = true ] && [ "$OPT_CMD" != "deps" ]; then
    error "--skip-system is only valid with --deps"
fi

if [ "$OPT_WITH_THIRD_PARTY" = true ] && [ "$OPT_CMD" != "clean" ]; then
    error "--with-third-party is only valid with --clean"
fi

if [ -n "$OPT_BUILD_TYPE" ] && [ "$OPT_CMD" != "build" ]; then
    error "--type is only valid with --build"
fi

if [ "$OPT_LOG" = true ] && [ "$OPT_CMD" != "build" ]; then
    error "--log is only valid with --build"
fi

if { [ "$OPT_NATIVE" = true ] || [ "$OPT_APPIMAGE" = true ] \
  || [ "$OPT_SHARUN" = true ] || [ "$OPT_FLATPAK" = true ] \
  || [ "$OPT_NO_MENU" = true ]; } && [ "$OPT_CMD" != "package" ]; then
    error "Format flags (--native/--appimage/--sharun/--flatpak/--no-menu) are only valid with --package"
fi

if [ "$OPT_REBUILD" = true ] && [ "$OPT_CMD" = "clean" ]; then
    error "--rebuild is not valid with --clean (use --with-third-party to remove third_party)"
fi

# -----------------------------------------------------------------------------
# detect_arch (skipped for --clean)
# -----------------------------------------------------------------------------
if [ "$OPT_CMD" != "clean" ]; then
    detect_arch
    detect_distro
    if [ -z "$DISTRO_ADAPTER" ]; then
        error "No distros/*.sh matched DISTRO=$DISTRO"
    fi
    detect_sudo
    system_prepare

    _IPTV_CLEANUP_DONE=0
    _run_cleanup_once() {
        if [ "$_IPTV_CLEANUP_DONE" = 1 ]; then
            return 0
        fi
        _IPTV_CLEANUP_DONE=1
        deps_cleanup_after_success || true
    }
    trap _run_cleanup_once EXIT
fi

# -----------------------------------------------------------------------------
# Dispatch
# -----------------------------------------------------------------------------
case "$OPT_CMD" in
    deps)    deps_run ;;
    build)   bin_run ;;
    package) package_run ;;
    clean)   clean_run ;;
    *)       error "Internal error: unknown OPT_CMD=$OPT_CMD" ;;
esac