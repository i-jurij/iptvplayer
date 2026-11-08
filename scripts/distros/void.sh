# distro-patterns: void*
#
# Void Linux - .xbps via xbps-src (void-packages clone required).

PKGMGR=xbps-install
PKGMGR_CHECK=xbps-query
PKGMGR_REMOVE=xbps-remove
COM_CHECK="-l"
COM_UPDATE="-S"
COM_INSTALL="-y"
COM_REMOVE="-y -R"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE="-Su -y"
OPTS_UPGRADE=""
CHECK_CHAIN=("-l|")

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
curl-dev:libcurl-devel
mpv-dev:mpv-devel
rapidjson-dev:rapidjson
gtk3-dev:gtk+3-devel
rsvg-common:librsvg
x11-dev:libX11-devel
x11-xcb-dev:libxcb-devel
gl-dev:MesaLib-devel
egl-dev:MesaLib-devel
png-dev:libpng-devel
jpeg-dev:libjpeg-turbo-devel
webp-dev:libwebp-devel
zlib-dev:zlib-devel
expat-dev:expat-devel
freetype-dev:freetype-devel
"

REQUIRED_TOOLS=("xbps-src:void-packages" "tar:tar" "readelf:binutils")

PACK_FORMAT=void
NATIVE_LABEL="Native .xbps"

distro_build() {
    local VOID_PACKAGES_DIR="${VOID_PACKAGES_DIR:-$HOME/void-packages}"
    local revision="${XBPS_REVISION:-1}"
    local arch; arch="$(uname -m)"

    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}_${revision}.${arch}.xbps"
    local template_dir="$VOID_PACKAGES_DIR/srcpkgs/$PACKAGE_NAME"

    log "Building Void .xbps (revision=$revision, arch=$arch)..."

    if [ ! -d "$VOID_PACKAGES_DIR" ]; then
        echo "[!] VOID_PACKAGES_DIR does not exist: $VOID_PACKAGES_DIR" >&2
        echo "    Clone: git clone https://github.com/void-linux/void-packages.git" >&2
        return 1
    fi
    if [ ! -x "$VOID_PACKAGES_DIR/xbps-src" ]; then
        echo "[!] xbps-src not found in $VOID_PACKAGES_DIR" >&2
        return 1
    fi

    local install_dir="$PROJECT_ROOT/install"
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] Not found: $install_dir/bin/$PACKAGE_NAME - build first" >&2
        return 1
    fi

    rm -rf "$template_dir"
    mkdir -p "$template_dir"
    mkdir -p "$template_dir/files"
    cp -a "$install_dir/." "$template_dir/files/"

    cat > "$template_dir/template" << EOF
# Template file for '${PACKAGE_NAME}'
pkgname=${PACKAGE_NAME}
version=${VERSION}
revision=${revision}
build_style=custom
short_desc="IPTV Playlist Player"
maintainer="ijurij <mnisjil@duck.com>"
license="MIT"
homepage="https://github.com/i-jurij/${PACKAGE_NAME}"
distfiles=""
checksum=""

do_install() {
    mkdir -p "\${DESTDIR}/usr"
    cp -a "\${FILESDIR}/." "\${DESTDIR}/usr/"
}
EOF

    local su_prefix=""
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m xbuilder 2>/dev/null || true
        chown -R xbuilder:xbuilder "$VOID_PACKAGES_DIR"
        su_prefix="su -c"
    fi

    local xbps_cmd="cd '$VOID_PACKAGES_DIR' && ./xbps-src pkg '$PACKAGE_NAME'"
    if [ -n "$su_prefix" ]; then
        if ! $su_prefix "$xbps_cmd" xbuilder </dev/null; then
            echo "[!] xbps-src pkg failed" >&2
            return 1
        fi
    else
        if ! bash -c "$xbps_cmd"; then
            echo "[!] xbps-src pkg failed" >&2
            return 1
        fi
    fi

    local built
    built=$(find "$VOID_PACKAGES_DIR/hostdir/binpkgs" \
              -name "${PACKAGE_NAME}-${VERSION}_${revision}.${arch}.xbps" \
              -type f -print -quit 2>/dev/null)
    if [ -z "$built" ]; then
        built=$(find "$VOID_PACKAGES_DIR/hostdir/binpkgs" \
                  -name "${PACKAGE_NAME}-*.xbps" -type f -print -quit 2>/dev/null)
    fi
    if [ -z "$built" ]; then
        echo "[!] xbps-src did not produce .xbps in $VOID_PACKAGES_DIR/hostdir/binpkgs" >&2
        return 1
    fi
    if ! cp "$built" "$out_file"; then
        echo "[!] Failed to copy $built -> $out_file" >&2
        return 1
    fi

    log ".xbps: $out_file"
    rm -rf "$template_dir"
    return 0
}