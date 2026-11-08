# distro-patterns: debian* ubuntu* linuxmint* pop* astra*
#
# Debian / Ubuntu / Mint / Pop / Astra. Single .deb adapter.

PKGMGR=apt-get
PKGMGR_CHECK=dpkg
COM_CHECK="-s"
COM_UPDATE="update"
COM_INSTALL="install -y --no-install-recommends"
COM_REMOVE="remove -y --auto-remove"
OPTS_CHECK=""
OPTS_UPDATE=""
OPTS_INSTALL=""
OPTS_REMOVE=""
COM_UPGRADE="upgrade -y"
OPTS_UPGRADE=""
CHECK_CHAIN=("-s|")

DEPS="
build-essential:build-essential
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg
debsigs:debsigs
pkg-config:pkg-config
wget:wget
curl:curl
gettext:gettext
curl-dev:libcurl4-openssl-dev
mpv-dev:libmpv-dev
rapidjson-dev:rapidjson-dev
gtk3-dev:libgtk-3-dev
rsvg-common:librsvg2-common
x11-dev:libx11-dev
x11-xcb-dev:libx11-xcb-dev
gl-dev:libgl1-mesa-dev
egl-dev:libegl1-mesa-dev
png-dev:libpng-dev
jpeg-dev:libjpeg-dev
webp-dev:libwebp-dev
zlib-dev:zlib1g-dev
expat-dev:libexpat1-dev
freetype-dev:libfreetype-dev
"

REQUIRED_TOOLS=("dpkg-deb:dpkg" "dpkg-shlibdeps:dpkg-dev" "tar:tar" "readelf:binutils" "debsigs:debsigs")

PACK_FORMAT=deb
DEB_OUTPUT_SUFFIX="$DISTRO"

NATIVE_LABEL="Native .deb"

distro_build() {
    prepare_staging && pack_deb
}