# distro-patterns: nixos*
#
# NixOS - derivation via nix build. Works on any host with nix in PATH.
# autoPatchelfHook rewrites ELF headers to /nix/store paths.

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

REQUIRED_TOOLS=("nix:nix" "tar:tar" "readelf:binutils")

PACK_FORMAT=nix
NATIVE_LABEL="Native Nix derivation"

distro_build() {
    local install_dir="$PROJECT_ROOT/install"
    local nix_dir="$PROJECT_ROOT/pkg-nix"
    local sys_arch; sys_arch="$(uname -m)"
    local nix_system="${NIX_SYSTEM:-${sys_arch}-linux}"
    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${nix_system}.tar.zst"

    log "Building Nix derivation (system=$nix_system)..."

    if ! command -v nix >/dev/null 2>&1; then
        echo "[!] nix not found. Install: https://nixos.org/download" >&2
        return 1
    fi
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] Not found: $install_dir/bin/$PACKAGE_NAME - build first" >&2
        return 1
    fi

    rm -rf "$nix_dir"
    mkdir -p "$nix_dir/staging"
    cp -a "$install_dir/." "$nix_dir/staging/"

    cat > "$nix_dir/default.nix" << NIXEOF
{ stdenv
, lib
, autoPatchelfHook
, makeWrapper
, wrapGAppsHook3
, gtk3
, mpv
, curl
, rapidjson
, libGL
, libEGL
, xorg
, wayland
, freetype
, fontconfig
, expat
, zlib
, libpng
, libjpeg
, libwebp
}:

stdenv.mkDerivation {
  pname = "${PACKAGE_NAME}";
  version = "${VERSION}";

  src = ./.;

  dontUnpack = true;
  dontConfigure = true;
  dontBuild = true;

  nativeBuildInputs = [
    autoPatchelfHook
    makeWrapper
    wrapGAppsHook3
  ];

  buildInputs = [
    gtk3
    mpv
    curl
    rapidjson
    libGL
    libEGL
    xorg.libX11
    xorg.libXext
    wayland
    freetype
    fontconfig
    expat
    zlib
    libpng
    libjpeg
    libwebp
    stdenv.cc.cc.lib
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p \$out/bin
    mkdir -p \$out/share

    cp -a \${./staging}/bin/. \$out/bin/
    cp -a \${./staging}/share/. \$out/share/ 2>/dev/null || true

    wrapProgram \$out/bin/${PACKAGE_NAME} \\
      --prefix LD_LIBRARY_PATH : \${lib.makeLibraryPath [ gtk3 mpv curl ]}

    runHook postInstall
  '';

  meta = with lib; {
    description = "IPTV Playlist Player";
    homepage = "https://github.com/i-jurij/${PACKAGE_NAME}";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "${PACKAGE_NAME}";
  };
}
NIXEOF

    local store_path
    if ! store_path=$(cd "$nix_dir" && nix build --no-link --print-out-paths \
        --extra-experimental-features "nix-command flakes" \
        -f default.nix); then
        echo "[!] nix build failed" >&2
        echo "$store_path" >&2
        return 1
    fi

    if [ -z "$store_path" ] || [ ! -d "$store_path" ]; then
        echo "[!] nix build did not return a store path" >&2
        return 1
    fi

    log "Store path: $store_path"

    if ! tar --zstd -cf "$out_file" -C "$store_path" .; then
        echo "[!] tar.zst packing failed" >&2
        return 1
    fi

    if [ ! -f "$out_file" ]; then
        echo "[!] nix: $out_file not created" >&2
        return 1
    fi

    log "Nix derivation: $out_file"
    rm -rf "$nix_dir"
    return 0
}