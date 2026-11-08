# distro-patterns: alpine*
#
# Alpine Linux - .apk via abuild. Only runs inside an Alpine environment
# (musl, abuild). Cross-build is not supported.

PKGMGR=apk
PKGMGR_CHECK=apk
COM_CHECK="info -e"
COM_UPDATE="update"
COM_INSTALL="add --no-cache"
COM_REMOVE="del"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE="upgrade"
OPTS_UPGRADE=""
CHECK_CHAIN=("info -e|")

DEPS="
build-essential:build-base
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
pkg-config:pkgconf
wget:wget
curl:curl
gettext:gettext
curl-dev:curl-dev
mpv-dev:mpv-dev
rapidjson-dev:rapidjson-dev
gtk3-dev:gtk+3.0-dev
rsvg-common:librsvg
x11-dev:libx11-dev
x11-xcb-dev:libxcb-dev
gl-dev:mesa-dev
egl-dev:mesa-dev
png-dev:libpng-dev
jpeg-dev:libjpeg-turbo-dev
webp-dev:libwebp-dev
zlib-dev:zlib-dev
expat-dev:expat-dev
freetype-dev:freetype-dev
"

REQUIRED_TOOLS=("abuild:alpine-sdk" "tar:tar" "readelf:binutils")

PACK_FORMAT=alpine
NATIVE_LABEL="Native .apk"

distro_build() {
    local pkgrel="0"
    local APK_ARCH
    case "$(uname -m)" in
        armv7l|armv8l) APK_ARCH="armv7" ;;
        i686|i386)     APK_ARCH="x86"   ;;
        *)             APK_ARCH="$(uname -m)" ;;
    esac
    local depends="gtk+3.0 mpv"
    local makedepends=""

    local apk_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-r${pkgrel}.apk"
    local spec_dir="$PROJECT_ROOT/pkg-apk"

    log "Building Alpine .apk (pkgrel=$pkgrel)..."

    ensure_tools "Alpine .apk" "abuild:alpine-sdk"

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] alpine: failed to prepare staging" >&2
            return 1
        fi
    fi

    rm -rf "$spec_dir"
    mkdir -p "$spec_dir"

    local pkg_dir="$spec_dir/$PACKAGE_NAME"
    mkdir -p "$pkg_dir"
    cp -a "$STAGING_DIR/usr" "$pkg_dir/staging_usr"

    local depends_line="" makedepends_line=""
    [ -n "$depends" ] && depends_line="depends=\"$depends\""
    [ -n "$makedepends" ] && makedepends_line="makedepends=\"$makedepends\""

    cat > "$pkg_dir/APKBUILD" << EOF
# Maintainer: ijurij <mnisjil@duck.com>
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=$pkgrel
pkgdesc="IPTV Playlist Player"
url="https://github.com/i-jurij/$PACKAGE_NAME"
arch="$APK_ARCH"
license="MIT"
$depends_line
$makedepends_line
source=""
builddir="\$srcdir"

build() {
    :
}

package() {
    install -d "\$pkgdir/usr"
    cp -a "\$startdir/staging_usr/." "\$pkgdir/usr/"
}
EOF

    local su_prefix=""
    if [ "$(id -u)" -eq 0 ]; then
        adduser -D builder 2>/dev/null || useradd -m builder 2>/dev/null || true
        chown -R builder:builder "$pkg_dir"
        su_prefix="su -c"
    fi

    local key_cmd='[ -f "$HOME/.abuild/$(hostname).rsa" ] || abuild-keygen -a -n'
    if [ -n "$su_prefix" ]; then
        $su_prefix "$key_cmd" builder </dev/null >/dev/null 2>&1 || true
    else
        bash -c "$key_cmd" >/dev/null 2>&1 || true
    fi

    local abuild_cmd="cd '$pkg_dir' && abuild -F -K -P"
    if [ -n "$su_prefix" ]; then
        if ! $su_prefix "$abuild_cmd" builder </dev/null; then
            echo "[!] abuild failed" >&2
            return 1
        fi
    else
        if ! bash -c "$abuild_cmd"; then
            echo "[!] abuild failed" >&2
            return 1
        fi
    fi

    local built
    built=$(find "$spec_dir" -name "${PACKAGE_NAME}-${VERSION}-r${pkgrel}.apk" -type f -print -quit 2>/dev/null)
    if [ -z "$built" ]; then
        echo "[!] abuild did not produce .apk in $spec_dir" >&2
        return 1
    fi
    if ! mv "$built" "$apk_file"; then
        echo "[!] Failed to move $built -> $apk_file" >&2
        return 1
    fi

    log ".apk: $apk_file"
    return 0
}