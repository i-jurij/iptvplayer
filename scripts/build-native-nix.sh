#!/usr/bin/env bash
# =============================================================================
# build-native-nix.sh – Nix derivation для NixOS
# =============================================================================
# Библиотека. Сорсится из build-package.sh.
#
# Работает при наличии nix в PATH (не только на NixOS — Nix можно
# установить на любой дистрибутив).
#
# Особенности:
#   * Пакет описывается декларативно через stdenv.mkDerivation.
#   * autoPatchelfHook переписывает ELF-заголовки бинарника, подменяя
#     жёстко прописанные пути к библиотекам на пути в /nix/store.
#   * Сборка: nix build --no-link --print-out-paths, результат упаковывается
#     в .tar.zst.
#   * Артефакт: dist/iptvplayer-<ver>-<system>.tar.zst
#
# Переменные окружения:
#   NIX_SYSTEM  (опц.) system; default = "x86_64-linux" (или aarch64-linux)
# =============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-nix.sh — библиотека." >&2; exit 1
fi

if ! declare -F log >/dev/null 2>&1; then
    source "$SCRIPT_DIR/common.sh"
fi

build_native_nix() {
    : "${PROJECT_ROOT:?}" "${OUTPUT_DIR:?}" "${PACKAGE_NAME:?}" "${VERSION:?}"

    local install_dir="$PROJECT_ROOT/install"
    local nix_dir="$PROJECT_ROOT/pkg-nix"
    local sys_arch; sys_arch="$(uname -m)"
    local nix_system="${NIX_SYSTEM:-${sys_arch}-linux}"
    local out_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-${nix_system}.tar.zst"

    echo "[+] Сборка Nix derivation (system=$nix_system)..."

    if ! command -v nix >/dev/null 2>&1; then
        echo "[!] nix не найден. Установите: https://nixos.org/download" >&2
        return 1
    fi
    if [ ! -x "$install_dir/bin/$PACKAGE_NAME" ]; then
        echo "[!] не найден $install_dir/bin/$PACKAGE_NAME — нужен build-release.sh" >&2
        return 1
    fi

    rm -rf "$nix_dir"
    mkdir -p "$nix_dir/staging"

    # Кладём install/ в staging/ — derivation будет копировать оттуда.
    cp -a "$install_dir/." "$nix_dir/staging/"

    # default.nix
    # - autoPatchelfHook в nativeBuildInputs — инструмент сборки.
    # - buildInputs — рантайм-зависимости: gtk3, mpv, curl и т.д.
    # - dontUnpack/dontConfigure/dontBuild — бинарник уже собран.
    # - installPhase копирует staging/ в $out.
    # - stdenv.cc.cc.lib — стандартная C++ библиотека для autoPatchelfHook.
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

    # Сборка. --print-out-paths печатает путь в /nix/store.
    local store_path
    if ! store_path=$(cd "$nix_dir" && nix build --no-link --print-out-paths \
        --extra-experimental-features "nix-command flakes" \
        -f default.nix); then
        echo "[!] nix build упал" >&2
        echo "$store_path" >&2
        return 1
    fi

    if [ -z "$store_path" ] || [ ! -d "$store_path" ]; then
        echo "[!] nix build не вернул путь к store" >&2
        return 1
    fi

    echo "[i] Store path: $store_path"

    # Упаковка в tar.zst.
    if ! tar --zstd -cf "$out_file" -C "$store_path" .; then
        echo "[!] упаковка в tar.zst упала" >&2
        return 1
    fi

    if [ ! -f "$out_file" ]; then
        echo "[!] build_native_nix: не создан $out_file" >&2
        return 1
    fi

    echo "[✓] Nix derivation: $out_file"
    rm -rf "$nix_dir"
    return 0
}