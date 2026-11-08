# distro-patterns: slackware*
#
# Slackware - .txz via SlackBuild + makepkg (pkgtools).
# No package manager; user installs dependencies manually.

PKGMGR=""
PKGMGR_CHECK=""
COM_CHECK=""
COM_UPDATE=""
COM_INSTALL=""
COM_REMOVE=""
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE=""
OPTS_UPGRADE=""
CHECK_CHAIN=()

DEPS=""

REQUIRED_TOOLS=("makepkg:pkgtools" "tar:tar" "readelf:binutils")

PACK_FORMAT=slackware
NATIVE_LABEL="Native .txz"

distro_build() {
    local ARCH; ARCH="$(uname -m)"
    local BUILD="${SLACKWARE_BUILD:-1}"
    local TAG="${SLACKWARE_TAG:-}"

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${ARCH}-${BUILD}${TAG}.txz"
    local build_dir="$PROJECT_ROOT/pkg-slackbuild"

    log "Building Slackware .txz (arch=$ARCH, build=$BUILD)..."

    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg not found. Requires Slackware with pkgtools." >&2
        return 1
    fi

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] slackware: failed to prepare staging" >&2
            return 1
        fi
    fi

    rm -rf "$build_dir"
    mkdir -p "$build_dir/PKG/install"

    cp -a "$STAGING_DIR/usr" "$build_dir/PKG/"

    cat > "$build_dir/PKG/install/slack-desc" << EOF
# HOW TO EDIT THIS FILE:
# The "handy ruler" below makes it easier to edit a package description.
# Line up the first '|' above the ':' following the base package name, and
# the '|' on the right side marks the last column you can put a character in.
# You must make exactly 11 lines for the formatting to be correct.  It's also
# customary to leave one space after the ':' except on otherwise blank lines.

         |-----handy-ruler------------------------------------------------------|
$PACKAGE_NAME: $PACKAGE_NAME (IPTV Playlist Player)
$PACKAGE_NAME:
$PACKAGE_NAME: A simple player for playlists with GUI.
$PACKAGE_NAME:
$PACKAGE_NAME: Homepage: https://github.com/i-jurij/$PACKAGE_NAME
$PACKAGE_NAME:
$PACKAGE_NAME:
$PACKAGE_NAME:
$PACKAGE_NAME:
$PACKAGE_NAME:
EOF

    if ! ( cd "$build_dir/PKG" && makepkg -l y -c n "$pkg_file" ); then
        echo "[!] makepkg failed" >&2
        return 1
    fi

    if [ ! -f "$pkg_file" ]; then
        echo "[!] slackware: makepkg did not produce $pkg_file" >&2
        return 1
    fi

    log ".txz: $pkg_file"
    return 0
}