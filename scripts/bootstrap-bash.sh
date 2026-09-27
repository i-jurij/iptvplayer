#!/bin/sh
# =============================================================================
# bootstrap-bash.sh – POSIX-бутстрап: гарантирует, что вызывающий скрипт
# исполняется под bash. Сорсится из точек входа (build-package.sh,
# build-release.sh, setup-deps.sh) до любого bash-кода и до `set -e`.
# =============================================================================
#
# Зачем POSIX: на системах без bash (Alpine без apk add bash, NixOS со
# stripped PATH, distroless) именно этот файл исполняется первым. Значит,
# он не может использовать bash-измы.
#
# Как работает:
#   1. BASH_VERSION установлен → мы уже под bash, return 0.
#   2. Иначе: ищем системный bash в PATH.
#   3. Если нет — скачиваем статический (robxu9/bash-static) в каталог
#      скриптов, рядом с linuxdeploy-*/appimagetool-*/quick-sharun.sh.
#   4. exec <bash> "$0" "$@" — bash перечитывает точку входа с начала;
#      бутстрап запускается второй раз, видит BASH_VERSION и выходит.
#
# SHA256:
#   Первое скачивание — trust-on-first-use: считаем хеш, кладём в sidecar
#   `bash-static-<arch>.sha256`. При последующих запусках кешированный
#   бинарник сверяется с sidecar; несовпадение → перекачка. Если sidecar
#   закоммитить в репозиторий — хеш становится pinned автоматически,
#   без ручного заполнения констант.
#
# Переменные окружения:
#   IPTVPLAYER_BASH_STATIC=/path/to/bash   — использовать указанный бинарник.
#   IPTVPLAYER_BASH_TAG=<tag|latest>       — тег релиза robxu9/bash-static.
#   IPTVPLAYER_BOOTSTRAP_DISABLE=1         — не скачивать, только системный.
# =============================================================================

# Уже под bash — выходим немедленно, не определяя никаких переменных.
if [ -n "${BASH_VERSION:-}" ]; then
    return 0 2>/dev/null || exit 0
fi

# Защита от прямого запуска (не через source).
case "${0##*/}" in
    bootstrap-bash.sh)
        echo "bootstrap-bash.sh — библиотека, не запускается напрямую." >&2
        echo "Он сорсится из build-package.sh / build-release.sh / setup-deps.sh." >&2
        exit 1
        ;;
esac

# -----------------------------------------------------------------------------
# Вспомогательные функции. Определяются до основного кода, чтобы быть
# доступными в любой ветке. В sourced-контексте попадают в namespace
# вызывающего скрипта, но живут недолго: bootstrap либо возвращается
# сразу (BASH_VERSION), либо exec'ается в bash — прежний shell-процесс
# исчезает.
# -----------------------------------------------------------------------------

_iptv_bs_download() {
    # $1 = url, $2 = out. 0 при успехе.
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$1" -o "$2" 2>/dev/null
        return $?
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -q "$1" -O "$2" 2>/dev/null
        return $?
    fi
    return 127
}

_iptv_bs_sha256() {
    # $1 = file. Печатает хеш или пустоту.
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" 2>/dev/null | awk '{print $1}'
        return 0
    fi
    if command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" 2>/dev/null | awk '{print $1}'
        return 0
    fi
    return 1
}

_iptv_bs_is_elf() {
    # $1 = file. 0 если первые 4 байта = \x7fELF. Отсекает HTML-страницы
    # прокси и усечённые загрузки до того, как мы что-то посчитаем.
    _magic="$(dd if="$1" bs=4 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    [ "$_magic" = "7f454c46" ]
}

_iptv_bs_ask() {
    # $1 = prompt. 0=да, 1=нет. Неинтерактивно → да.
    if [ ! -t 0 ]; then
        return 0
    fi
    printf '%s [Y/n] ' "$1" >&2
    _a=""
    read -r _a || _a=""
    case "$_a" in
        ''|y|Y|yes|YES|Yes) return 0 ;;
        *) return 1 ;;
    esac
}

# -----------------------------------------------------------------------------
# Определяем каталог скриптов (рядом с точкой входа) — туда кешируем.
# -----------------------------------------------------------------------------
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
        _sumfile="${_static}.sha256"

        # Проверка кеша: файл есть и sidecar есть → сверяем.
        if [ -x "$_static" ] && [ -f "$_sumfile" ]; then
            _want="$(awk 'NR==1 {print $1}' "$_sumfile" 2>/dev/null)"
            _got="$(_iptv_bs_sha256 "$_static")"
            if [ -n "$_want" ] && [ -n "$_got" ] && [ "$_want" != "$_got" ]; then
                echo "[!] Кешированный $_static не совпадает с $_sumfile — перекачиваю." >&2
                rm -f "$_static" "$_sumfile"
            fi
        fi

        # Скачивание.
        if [ ! -x "$_static" ] && [ -z "${IPTVPLAYER_BOOTSTRAP_DISABLE:-}" ]; then
            if _iptv_bs_ask "bash не найден. Скачать статический ($_asset, tag=$_iptv_bs_tag)?"; then
                _url="https://github.com/robxu9/bash-static/releases/download/${_iptv_bs_tag}/${_asset}"
                _tmp="${_static}.tmp.$$"
                rm -f "$_tmp"

                printf '[*] Скачиваю %s...\n' "$_asset" >&2
                if _iptv_bs_download "$_url" "$_tmp" && [ -s "$_tmp" ] && _iptv_bs_is_elf "$_tmp"; then
                    _h="$(_iptv_bs_sha256 "$_tmp")"
                    chmod +x "$_tmp" 2>/dev/null || true
                    mv "$_tmp" "$_static"
                    if [ -n "$_h" ]; then
                        printf '%s  %s\n' "$_h" "${_static##*/}" > "$_sumfile"
                        echo "[+] bash-static: $_static" >&2
                        echo "[i] sha256=$_h" >&2
                        echo "[i] Хеш сохранён в ${_sumfile##*/}. Закоммитьте его, чтобы зафиксировать версию (опционально)." >&2
                    else
                        echo "[+] bash-static: $_static" >&2
                        echo "[i] sha256sum/shasum недоступны — sidecar не создан." >&2
                    fi
                else
                    echo "[!] Не удалось скачать или файл не является ELF: $_url" >&2
                    rm -f "$_tmp"
                fi
            fi
        fi

        if [ -x "$_static" ]; then
            _iptv_bs_bash="$_static"
        fi
    fi
fi

# 4. Ничего не нашли — внятная инструкция.
if [ -z "$_iptv_bs_bash" ]; then
    echo "[!] bash не найден и не удалось скачать статический." >&2
    if [ -n "${IPTVPLAYER_BOOTSTRAP_DISABLE:-}" ]; then
        echo "    IPTVPLAYER_BOOTSTRAP_DISABLE=1 — скачивание отключено," >&2
        echo "    искали только системный bash в PATH." >&2
    fi
    echo "    Установите bash средствами вашего дистрибутива:" >&2
    echo "      apt install bash | dnf install bash | apk add bash | pacman -S bash" >&2
    exit 1
fi

# 5. Перезапуск под bash. bash перечитает точку входа с начала — бутстрап
#    тогда увидит BASH_VERSION и сразу вернёт управление.
exec "$_iptv_bs_bash" "$0" "$@"

# exec не возвращается при успехе; сюда попадаем только если он упал.
echo "[!] Не удалось запустить bash: $_iptv_bs_bash" >&2
exit 1