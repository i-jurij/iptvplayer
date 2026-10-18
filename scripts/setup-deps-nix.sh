#!/usr/bin/env bash
# setup-deps-nix.sh – NixOS
#
# На NixOS «установить пакет глобально» нельзя — зависимости
# объявляются декларативно. Если вы запускаете build-release.sh
# вне nix-shell, он упадёт на отсутствии cmake/autoconf/etc.
# Запускайте через `nix-shell` или `nix develop`, либо используйте
# --native-nix (он обходит setup-deps).
#
# deps_packages возвращает пусто → MISSING_PACKAGES пуст →
# setup-deps.sh дойдёт до сборки wxWidgets с тем, что есть в PATH.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-nix.sh — библиотека." >&2; exit 1; fi

deps_packages() {
    cat >&2 <<'EOF'
[i] NixOS: системные зависимости не ставятся автоматически.
[i] Запустите сборку внутри nix-shell с нужными пакетами:
[i]   nix-shell -p gcc cmake gnumake pkg-config autoconf automake \
[i]     libtool git curl gtk3 mpv rapidjson libGL libX11 libxcb \
[i]     wayland freetype fontconfig expat zlib libpng libjpeg libwebp \
[i]     --run ./scripts/build-release.sh --yes
[i] Или используйте --native-nix: он собирает derivation без setup-deps.
EOF
    :  # пустой список пакетов
}

deps_is_installed() { return 0; }
deps_update() { :; }
deps_install() { :; }
deps_remove() { :; }