#!/usr/bin/env bash
# setup-deps-openmandriva.sh – dnf: OpenMandriva Lx
#
# OpenMandriva использует lib64-префикс для devel-пакетов (как ROSA),
# generic setup-deps-rpm.sh с lib64 не дружит.
#
# lib64mpv-devel лежит в репозитории restricted. Включаем его ДО makecache:
# иначе dnf не увидит пакет в кэше и install упадёт с "no match".
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then echo "setup-deps-openmandriva.sh — библиотека." >&2; exit 1; fi

# Включает restricted-репозиторий, если он выключен.
# Локально — интерактивный запрос; в CI (NON_INTERACTIVE=true) — сразу.
_ensure_restricted_repo() {
    # Уже включён — выходим.
    if dnf repolist --enabled 2>/dev/null | grep -q 'restricted'; then
        return 0
    fi

    # Ищем ID выключенных restricted-репозиториев.
    local _ids
    _ids=$(dnf repolist --disabled 2>/dev/null \
           | awk '/restricted/ {print $1}' \
           | tr '\n' ' ')
    # restricted нет в конфиге вовсе — не наша забота,
    # dnf install сам скажет, что пакета нет.
    [ -z "$_ids" ] && return 0

    if [[ "$NON_INTERACTIVE" == true ]]; then
        log "CI: включаю restricted-репозиторий автоматически: ${_ids% }"
    else
        warn "Для lib64mpv-devel требуется репозиторий restricted."
        warn "В нём лежат пакеты с ограничениями по лицензиям/патентам;"
        warn "убедитесь, что их использование законно в вашей юрисдикции."
        if ! ask "Включить restricted (${_ids% }) сейчас? [Y/n]:" y; then
            error "Без restricted lib64mpv-devel не установить. Включите вручную: om-repo-picker"
        fi
    fi

    # shellcheck disable=SC2086
    $SUDO dnf config-manager --enable $_ids || {
        error "Не удалось включить restricted (${_ids% }). Включите через om-repo-picker."
    }
    log "✅ restricted включён: ${_ids% }"
}

deps_packages() {
    local _pfx="lib64"
    [[ "$(uname -m)" == i686 || "$(uname -m)" == i386 ]] && _pfx="lib"
    cat <<EOF
build-essential:gcc gcc-c++ make glibc-devel llvm gettext
cmake:cmake
git:git
autoconf:autoconf
automake:automake
libtool:libtool
gnupg:gnupg2
pkg-config:pkgconf
wget:wget
curl:curl
curl-dev:${_pfx}curl-devel
mpv-dev:${_pfx}mpv-devel
rapidjson-dev:rapidjson
gtk3-dev:${_pfx}gtk+3.0-devel
rsvg-common:librsvg2
x11-dev:${_pfx}x11-devel
x11-xcb-dev:${_pfx}xcb-devel
gl-dev:${_pfx}gl-devel
egl-dev:${_pfx}egl-devel
png-dev:${_pfx}png-devel
jpeg-dev:${_pfx}jpeg-devel
webp-dev:${_pfx}webp-devel
zlib-dev:${_pfx}z-devel
expat-dev:${_pfx}expat-devel
freetype-dev:${_pfx}freetype6-devel
EOF
}

deps_is_installed() { rpm -q "$1" &>/dev/null; }

deps_update() {
    _ensure_restricted_repo

    log "Обновление метаданных dnf (makecache)..."
    if ! $SUDO dnf makecache; then
        warn "dnf makecache завершился с ошибкой — продолжаем."
        warn "dnf install попробует обновить metadata самостоятельно."
        warn "Если проблема повторится — проверьте вручную:"
        warn "  sudo dnf repolist"
        warn "  sudo dnf makecache"
    fi
}

deps_install() { $SUDO dnf install -y "$@"; }
deps_remove()  { $SUDO dnf remove -y "$@"; }