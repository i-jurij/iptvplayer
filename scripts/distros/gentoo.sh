# distro-patterns: gentoo*
#
# Gentoo Linux - .tbz2 via ebuild (sys-apps/portage).
# ebuild does not run as root; uses portage user.

PKGMGR=emerge
PKGMGR_CHECK=portageq
COM_CHECK="has_version /"
COM_UPDATE="--sync"
COM_INSTALL="-1 --noreplace"
COM_REMOVE="-C"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE=""
OPTS_UPGRADE=""
CHECK_CHAIN=("has_version /|")

DEPS="
build-essential:sys-devel/gcc sys-devel/binutils
cmake:dev-build/cmake
git:dev-vcs/git
autoconf:sys-devel/autoconf
automake:sys-devel/automake
libtool:sys-devel/libtool
gnupg:app-crypt/gnupg
pkg-config:dev-util/pkgconf
wget:net-misc/wget
curl:net-misc/curl
mpv-dev:media-video/mpv
rapidjson-dev:dev-libs/rapidjson
gtk3-dev:x11-libs/gtk+:3
rsvg-common:gnome-base/librsvg
x11-dev:x11-libs/libX11
x11-xcb-dev:x11-libs/libxcb
gl-dev:media-libs/mesa
png-dev:media-libs/libpng
jpeg-dev:media-libs/libjpeg-turbo
webp-dev:media-libs/libwebp
zlib-dev:sys-libs/zlib
expat-dev:dev-libs/expat
freetype-dev:media-libs/freetype
"

REQUIRED_TOOLS=("ebuild:portage" "tar:tar" "readelf:binutils")

PACK_FORMAT=gentoo
NATIVE_LABEL="Native .tbz2"

distro_build() {
    local category="${GENTOO_CATEGORY:-media-video}"
    local install_dir="$PROJECT_ROOT/install"
    local ebuild_dir="$PROJECT_ROOT/pkg-gentoo/$category/$PACKAGE_NAME"
    local ebuild_file="$ebuild_dir/${PACKAGE_NAME}-${VERSION}.ebuild"
    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}.tbz2"

    log "Building Gentoo .tbz2..."

    if ! command -v ebuild >/dev/null 2>&1; then
        echo "[!] ebuild not found. Requires sys-apps/portage (Gentoo)." >&2
        return 1
    fi
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] Not found: $install_dir/bin/$PACKAGE_NAME - build first" >&2
        return 1
    fi

    rm -rf "$ebuild_dir"
    mkdir -p "$ebuild_dir/files"
    cp -a "$install_dir/." "$ebuild_dir/files/"

    local _kw=""
    case "${APPIMAGE_ARCH:-}" in
        x86_64)  _kw="~amd64" ;;
        aarch64) _kw="~arm64" ;;
        armhf)   _kw="~arm"   ;;
        i686)    _kw="~x86"   ;;
        *)       _kw="~${APPIMAGE_ARCH:-amd64}" ;;
    esac

    cat > "$ebuild_file" << EOF
# Copyright 1999-${VERSION%-*} Gentoo Authors
# Distributed under the terms of the GNU General Public License v2

EAPI=8

DESCRIPTION="IPTV Playlist Player"
HOMEPAGE="https://github.com/i-jurij/${PACKAGE_NAME}"
SRC_URI=""

LICENSE="MIT"
SLOT="0"
KEYWORDS="$_kw"

RDEPEND="
    x11-libs/gtk+:3
    media-video/mpv
    x11-libs/wxGTK:3.2
"

src_install() {
    cp -a "\${FILESDIR}/." "\${D}/"
    fperms 0755 /usr/bin/${PACKAGE_NAME}
}
EOF

    local ebuild_cmd="cd '$ebuild_dir' && ebuild '$ebuild_file' package"
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m portage 2>/dev/null || true
        chown -R portage:portage "$ebuild_dir"
        if ! su -c "$ebuild_cmd" portage </dev/null; then
            echo "[!] ebuild package failed" >&2
            return 1
        fi
    else
        if ! bash -c "$ebuild_cmd"; then
            echo "[!] ebuild package failed" >&2
            return 1
        fi
    fi

    local built="" _pkgdir
    for _pkgdir in \
        "${GENTOO_PKGDIR:-}" \
        /var/cache/binpkgs \
        /usr/portage/packages \
        "$HOME/.cache/binpkgs"
    do
        [ -z "$_pkgdir" ] && continue
        [ -d "$_pkgdir" ] || continue
        built=$(find "$_pkgdir" -name "${PACKAGE_NAME}-${VERSION}*.tbz2" -type f -print -quit 2>/dev/null)
        [ -n "$built" ] && break
    done

    if [ -z "$built" ]; then
        echo "[!] ebuild did not produce .tbz2. Check PKGDIR in /etc/portage/make.conf" >&2
        return 1
    fi
    if ! cp "$built" "$out_file"; then
        echo "[!] Failed to copy $built -> $out_file" >&2
        return 1
    fi

    log ".tbz2: $out_file"
    rm -rf "$ebuild_dir"
    return 0
}