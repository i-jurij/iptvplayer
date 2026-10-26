#!/bin/sh
# =============================================================================
# bootstrap-bash.sh – POSIX-бутстрап: гарантирует, что вызывающий скрипт
# исполняется под bash. Сорсится из точек входа (build-package.sh,
# build-release.sh, setup-deps.sh) до любого bash-кода.
# =============================================================================
#
# Написан на POSIX sh и не использует bash-измов — на системах без bash
# (Alpine, NixOS, distroless) именно он исполняется первым.
#
# Как работает:
#   1. BASH_VERSION установлен → мы уже под bash, return 0.
#   2. Иначе: ищем системный bash в PATH.
#   3. Если нет — скачиваем статический (robxu9/bash-static) по HTTPS
#      в каталог скриптов, рядом с linuxdeploy-*/appimagetool-*/quick-sharun.
#   4. exec <bash> "$0" "$@" — bash перечитывает точку входа с начала;
#      бутстрап запускается второй раз, видит BASH_VERSION и выходит.
#
# Переменные окружения (опционально):
#   IPTVPLAYER_BASH_STATIC=/path/to/bash   — использовать указанный бинарник.
#   IPTVPLAYER_BASH_TAG=<tag|latest>       — тег релиза robxu9/bash-static.
#   IPTVPLAYER_BOOTSTRAP_DISABLE=1         — не скачивать, только системный.
# =============================================================================

# Уже под bash, но проверяем, что он пригоден для наших скриптов.
if [ -n "${BASH_VERSION:-}" ]; then
    if ! "$BASH" -c 'exec 3< <(:) ' 2>/dev/null; then
        echo "[!] bash without process substitution." >&2
    fi
    return 0 2>/dev/null || exit 0
fi

# Защита от прямого запуска (не через source).
case "${0##*/}" in
    bootstrap-bash.sh)
        echo "bootstrap-bash.sh is a library, do not run directly." >&2
        echo "It is sourced from build-package.sh / build-release.sh / setup-deps.sh." >&2
        exit 1
        ;;
esac

_iptv_bs_dir="$(CDPATH= cd -- "$(dirname -- "$0")" 2>/dev/null && pwd)" || _iptv_bs_dir=""
_iptv_bs_tag="${IPTVPLAYER_BASH_TAG:-latest}"
_iptv_bs_bash=""

# 1. Явно указанный бинарник.
if [ -n "${IPTVPLAYER_BASH_STATIC:-}" ] && [ -x "$IPTVPLAYER_BASH_STATIC" ]; then
    _iptv_bs_bash="$IPTVPLAYER_BASH_STATIC"
fi

# 2. Системный bash.
if [ -z "$_iptv_bs_bash" ]; then
    _sys="$(command -v bash 2>/dev/null || true)"
    if [ -n "$_sys" ] && [ -x "$_sys" ]; then
        _iptv_bs_bash="$_sys"
    fi
fi

# 3. Статический bash в каталоге скриптов.
if [ -z "$_iptv_bs_bash" ] && [ -n "$_iptv_bs_dir" ]; then
    # armv8l — 32-битный userspace на ARMv8, исполняет armv7-бинарники,
    # а НЕ aarch64; маппинг armv8l -> aarch64 дал бы Exec format error.
    case "$(uname -m)" in
        x86_64|amd64)          _asset="bash-linux-x86_64";  _suffix="x86_64"  ;;
        i686|i386)             _asset="bash-linux-i686";    _suffix="i686"    ;;
        aarch64|arm64)         _asset="bash-linux-aarch64"; _suffix="aarch64" ;;
        armv8l|armv7l|armhf)   _asset="bash-linux-armv7l";  _suffix="armv7l"  ;;
        *)                     _asset="";                   _suffix=""        ;;
    esac

    if [ -n "$_asset" ]; then
        _static="$_iptv_bs_dir/bash-static-${_suffix}"

        if [ ! -x "$_static" ] && [ -z "${IPTVPLAYER_BOOTSTRAP_DISABLE:-}" ]; then
            _url="https://github.com/robxu9/bash-static/releases/download/${_iptv_bs_tag}/${_asset}"
            printf '[*] bash not found — downloading static build (%s)...\n' "$_asset" >&2

            _tmp="${_static}.tmp.$$"
            rm -f "$_tmp"

            _ok=0
            if command -v curl >/dev/null 2>&1; then
                curl -fsSL "$_url" -o "$_tmp" 2>/dev/null && _ok=1
            elif command -v wget >/dev/null 2>&1; then
                wget -q "$_url" -O "$_tmp" 2>/dev/null && _ok=1
            else
                echo "[!] Neither curl nor wget found — cannot download bash." >&2
            fi

            if [ "$_ok" = 1 ] && [ -s "$_tmp" ]; then
                # ELF-магия (7f 45 4c 46): отсекает HTML-страницы ошибок
                # прокси и усечённые загрузки. dd/od/tr — POSIX, есть в busybox.
                _magic="$(dd if="$_tmp" bs=4 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')"
                if [ "$_magic" = "7f454c46" ]; then
                    chmod +x "$_tmp" 2>/dev/null || true
                    mv "$_tmp" "$_static"
                    echo "[+] bash-static: $_static" >&2
                else
                    echo "[!] Downloaded file is not ELF — probably an HTML error page." >&2
                    rm -f "$_tmp"
                fi
            else
                echo "[!] Failed to download $_url" >&2
                rm -f "$_tmp"
            fi
        fi

        if [ -x "$_static" ]; then
            _iptv_bs_bash="$_static"
        fi
    fi
fi

# 4. Ничего не нашли — внятная инструкция.
if [ -z "$_iptv_bs_bash" ]; then
    echo "[!] bash not found and failed to download static build." >&2
    if [ -n "${IPTVPLAYER_BOOTSTRAP_DISABLE:-}" ]; then
        echo "    IPTVPLAYER_BOOTSTRAP_DISABLE=1 — download disabled," >&2
        echo "    searched only for system bash in PATH." >&2
    fi
    echo "    Install bash using your distribution package manager:" >&2
    echo "      apt install bash | dnf install bash | apk add bash | pacman -S bash" >&2
    exit 1
fi

# 5. Перезапуск под bash.
exec "$_iptv_bs_bash" "$0" "$@"

# exec не возвращается при успехе; сюда попадаем только если он упал.
echo "[!] Failed to start bash: $_iptv_bs_bash" >&2
exit 1
