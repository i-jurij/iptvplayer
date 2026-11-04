# distro-patterns: arch* manjaro* endeavouros* cachyos*
#
# Arch Linux / Manjaro / EndeavourOS / CachyOS.
# Full makepkg build algorithm lives here.

PKGMGR=pacman
PKGMGR_CHECK=pacman
COM_CHECK="-Q"
COM_UPDATE="-Sy --noconfirm"
COM_INSTALL="-S --noconfirm --needed"
COM_REMOVE="-Rs --noconfirm"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
CHECK_CHAIN=("-Q|" "-Qg|")

DEPS="
build-essential:base-devel
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
pkg-config:pkg-config
wget:wget
curl:curl
gettext:gettext
rapidjson-dev:rapidjson
gtk3-dev:gtk3
rsvg-common:librsvg
x11-dev:libx11
x11-xcb-dev:libxcb
gl-dev:mesa
png-dev:libpng
jpeg-dev:libjpeg-turbo
webp-dev:libwebp
zlib-dev:zlib
expat-dev:expat
freetype-dev:freetype2
"

REQUIRED_TOOLS=("makepkg:pacman" "tar:tar" "readelf:binutils")

PACK_FORMAT=arch
NATIVE_LABEL="Native .pkg.tar.zst"

distro_build() {
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg not found. Building .pkg.tar.zst requires Arch/CachyOS/Manjaro." >&2
        return 1
    fi

    if [ -z "${STAGING_DIR:-}" ] || [ ! -d "$STAGING_DIR" ]; then
        echo "[!] STAGING_DIR not set or missing: ${STAGING_DIR:-<empty>}" >&2
        return 1
    fi

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-1-${DISTRO}-${APPIMAGE_ARCH}.pkg.tar.zst"
    log "Building native .pkg.tar.zst via makepkg..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] arch: failed to prepare staging" >&2
            return 1
        fi
    fi

    chmod +x "$STAGING_DIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    local BIN="$STAGING_DIR/usr/bin/$PACKAGE_NAME"
    if [ ! -x "$BIN" ]; then
        echo "[!] $BIN is not executable" >&2
        return 1
    fi

    if [ "$(id -u)" -eq 0 ]; then
        log "Syncing pacman file database (pacman -Fy)..."
        if ! timeout 120 pacman -Fy >/dev/null; then
            echo "[!] 'pacman -Fy' failed" >&2
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
            echo "[!] 'pacman -Fy' requires root, but no sudo/doas/run0/pkexec found." >&2
            echo "[!] Install sudo or run the build as root." >&2
            return 1
        fi
        log "Syncing pacman file database via '$_sudo pacman -Fy'..."
        if ! timeout 300 "$_sudo" pacman -Fy >/dev/null; then
            echo "[!] '$_sudo pacman -Fy' failed" >&2
            return 1
        fi
    fi

    # Single temp root; one RETURN trap cleans everything and removes
    # itself, so it fires exactly once.
    #
    # The shell RETURN trap fires on the NEXT function return after
    # installation. In this function that is the `return 0` at the very
    # bottom (see note there). Do NOT add log()/warn()/error() calls
    # between trap installation and the mv below - any function return
    # would fire the trap prematurely and wipe $workdir while still in
    # use.
    #
    # keep_workdir=1 (set on failure paths) preserves the whole tree
    # for post-mortem: PKGBUILD, staging_usr, makepkg logs.
    #
    # chmod 0755 lets the unprivileged builder user traverse $tmproot
    # into $workdir (which is chowned to builder below). The libs/pkgs/ldd
    # files stay root-owned and contain no sensitive data.
    local tmproot keep_workdir=0
    tmproot=$(mktemp -d) || { echo "[!] mktemp -d failed" >&2; return 1; }
    chmod 0755 "$tmproot"
    # shellcheck disable=SC2064
    trap '[ "$keep_workdir" = 1 ] || rm -rf "$tmproot"; trap - RETURN' RETURN

    local libs_tmp="$tmproot/libs"
    local pkgs_tmp="$tmproot/pkgs"
    local ldd_tmp="$tmproot/ldd"
    local workdir="$tmproot/work"
    mkdir -p "$workdir" || { echo "[!] failed to create $workdir" >&2; return 1; }

    readelf -d "$BIN" 2>/dev/null \
      | awk '/NEEDED/ {gsub(/[][]/, ""); print $NF}' \
      | sort -u > "$libs_tmp"

    ldd "$BIN" 2>/dev/null > "$ldd_tmp" || true

    : > "$pkgs_tmp"
    local lib libpath
    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        libpath=$(awk -v L="$lib" '$1 == L && $2 == "=>" { print $3; exit }' "$ldd_tmp")
        [ -z "$libpath" ] && continue
        [ ! -e "$libpath" ] && continue
        pacman -F "$libpath" 2>/dev/null \
          | awk '/ is owned by / {
                     pkg = $(NF-1)
                     sub(/^[^\/]+\//, "", pkg)
                     print pkg
                 }' >> "$pkgs_tmp" || true
    done < "$libs_tmp"

    local raw_deps
    raw_deps=$(sort -u "$pkgs_tmp" \
                | grep -vxE 'glibc|gcc-libs|gcc15-libs|libgcc|libstdc\+\+|mesa-amber' \
                | grep -v '^$')

    local auto_deps=""
    local _owner _virtual _seen=" "
    local _provides
    while IFS= read -r _owner; do
        [ -z "$_owner" ] && continue
        _provides=$(pacman -Si "$_owner" 2>/dev/null \
                    | awk -F': ' '/^Provides/{print $2}' \
                    | tr ' ' '\n' \
                    | grep -Fvx "$_owner" \
                    | grep -Fvx 'None')
        _virtual=$(printf '%s\n' "$_provides" | grep -v '=' | grep -v '^$' | head -n1)
        [ -z "$_virtual" ] && _virtual="$_owner"

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
        echo "[!] Dependency auto-detection returned nothing." >&2
        echo "[!] Check: sudo pacman -Fy && pacman -Fq /usr/lib/libgtk-3.so.0" >&2
        return 1
    fi

    log "auto-detected: $auto_deps"
    log "final:         $auto_deps"

    cp -a "$STAGING_DIR/usr" "$workdir/staging_usr"

    local PKG_ARCH="$APPIMAGE_ARCH"
    case "$PKG_ARCH" in
        armhf) PKG_ARCH=armv7h ;;
    esac

    local -a deps_array=()
    local d
    for d in $auto_deps; do deps_array+=("$d"); done

    {
        cat <<EOF
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=1
pkgdesc="IPTV Playlist Player"
arch=('$PKG_ARCH')
url="https://github.com/i-jurij/$PACKAGE_NAME"
license=('MIT')
options=('!debug')
EOF
        printf "depends=("
        local _depstr="" _d
        for _d in "${deps_array[@]}"; do
            _depstr="${_depstr:+$_depstr }'$_d'"
        done
        printf "%s)\n" "$_depstr"
        cat <<EOF

package() {
    install -d "\$pkgdir/usr"
    cp -a "\$startdir/staging_usr/." "\$pkgdir/usr/"
}
EOF
    } > "$workdir/PKGBUILD"

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
        echo "[!] makepkg failed. Logs and PKGBUILD: $workdir" >&2
        echo "[!] Debug: cd $workdir && cat PKGBUILD" >&2
        return 1
    fi

    local built
    built=$(find "$workdir" -maxdepth 1 -name '*.pkg.tar.zst' ! -name '*-debug-*' -print -quit)
    if [ -z "$built" ]; then
        keep_workdir=1
        echo "[!] makepkg did not produce a package. Contents: $workdir" >&2
        return 1
    fi

    mv "$built" "$pkg_file"
    # echo is a builtin - it does not trigger the RETURN trap. The
    # `return 0` below fires the trap, which removes $tmproot
    # (keep_workdir=0) and then removes itself.
    echo "[INFO] .pkg.tar.zst: $pkg_file"
    return 0
}