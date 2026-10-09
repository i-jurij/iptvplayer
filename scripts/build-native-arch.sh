#!/usr/bin/env bash

# Библиотека, не запускается напрямую. Сорсится из build-package.sh.

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "build-native-arch.sh — библиотека, не запускается напрямую. Используйте build-package.sh." >&2
    exit 1
fi

# ---- Нативный .pkg.tar.zst ARCH ----
build_pkg_arch() {
    # На не-Arch системах makepkg нет. Явный вызов --native-arch
    # на Ubuntu/Debian — ошибка пользователя.
    if ! command -v makepkg >/dev/null 2>&1; then
        echo "[!] makepkg не найден. Сборка .pkg.tar.zst возможна только на Arch/Manjaro."
        return 1
    fi

    # Пустой STAGING_DIR опаснее отсутствующего: проверки вида
    # "$STAGING_DIR/usr/bin/$PACKAGE_NAME" без него схлопываются в
    # /usr/bin/... и функция может случайно работать с системными
    # файлами вместо staging.
    if [ -z "$STAGING_DIR" ] || [ ! -d "$STAGING_DIR" ]; then
        echo "[!] STAGING_DIR не установлен или не существует: ${STAGING_DIR:-<empty>}" >&2
        return 1
    fi

    local pkg_file="$OUTPUT_DIR/${PACKAGE_NAME}-${VERSION}-1-${DISTRO}-${APPIMAGE_ARCH}.pkg.tar.zst"
    echo "[+] Создание нативного .pkg.tar.zst через makepkg..."

    if [ ! -f "$STAGING_DIR/usr/bin/$PACKAGE_NAME" ]; then
        if ! prepare_staging; then
            echo "[!] build_pkg_arch: не удалось подготовить staging" >&2
            return 1
        fi
    fi

    # actions/upload-artifact теряет exec-бит.
    chmod +x "$STAGING_DIR/usr/bin/$PACKAGE_NAME" 2>/dev/null || true

    # Автоопределение зависимостей: ldd -> .so -> pacman -Fq -> пакет-владелец.
    local BIN="$STAGING_DIR/usr/bin/$PACKAGE_NAME"
    [ -x "$BIN" ] || { echo "[!] $BIN не исполняем" >&2; return 1; }

    # Файловая БД pacman (нужна для pacman -Fq) синхронизируется
    # командой 'pacman -Fy', которая требует root. В CI мы root —
    # вызываем напрямую. Локально — повышаем привилегии через
    # sudo/doas/run0/pkexec. Если ни одно не сработало — падаем:
    # без БД автоопределение даст пустой результат, а собирать
    # пакет с выдуманными зависимостями хуже, чем не собрать вовсе.
    if [ "$(id -u)" -eq 0 ]; then
        echo "[i] Синхронизация файловой БД pacman (pacman -Fy)..."
        if ! timeout 120 pacman -Fy >/dev/null; then
            echo "[!] 'pacman -Fy' завершился с ошибкой" >&2
            return 1
        fi
    else
        local _sudo="" _c
        for _c in sudo doas run0 pkexec; do
            if command -v "$_c" >/dev/null 2>&1; then
                _sudo="$_c"
                break
            fi
        done
        if [ -z "$_sudo" ]; then
            echo "[!] Для 'pacman -Fy' нужен root, но ни sudo, ни doas, ни run0, ни pkexec не найдены." >&2
            echo "[!] Установите sudo или запустите сборку от root." >&2
            return 1
        fi
        echo "[i] Синхронизация файловой БД pacman через '$_sudo pacman -Fy'..."
        # stderr не глушим — пользователь увидит запрос пароля
        # (sudo/doas/run0) или сообщение об отказе в правах.
        if ! timeout 300 "$_sudo" pacman -Fy >/dev/null; then
            echo "[!] '$_sudo pacman -Fy' завершился с ошибкой" >&2
            return 1
        fi
    fi

    # Временные файлы и workdir. trap RETURN срабатывает на любом
    # выходе из функции, включая ранние return 1. workdir остаётся
    # только если keep_workdir=1 — это для отладки упавшего makepkg.
    local libs_tmp pkgs_tmp workdir keep_workdir=0
    libs_tmp=$(mktemp) || { echo "[!] mktemp для libs_tmp упал" >&2; return 1; }
    pkgs_tmp=$(mktemp) || { echo "[!] mktemp для pkgs_tmp упал" >&2; return 1; }
    workdir=$(mktemp -d) || { echo "[!] mktemp для workdir упал" >&2; return 1; }
    # shellcheck disable=SC2064
    # trap с RETURN не снимается автоматически после выхода из функции —
    # он сработает на выходе из ЛЮБОЙ следующей функции в скрипте.
    # Добавляем `trap - RETURN` внутрь самого trap: первое же срабатывание
    # (при выходе из build_pkg_arch) выполнит очистку и снимет trap.
    trap 'rm -f "$libs_tmp" "$pkgs_tmp"; [ "$keep_workdir" = 1 ] || rm -rf "$workdir"; trap - RETURN' RETURN

    ldd "$BIN" 2>/dev/null \
      | awk '
          /=>/ && $3 ~ /^\//  { print $3 }
          /^\t\//              { print $1 }
        ' \
      | sort -u > "$libs_tmp"

    : > "$pkgs_tmp"
    while IFS= read -r lib; do
        [ -z "$lib" ] && continue
        # pacman -Fq не находит владельца для библиотек вне пакетов
        # (собрано вручную, /usr/local/lib). Это норма, не ошибка.
        pacman -Fq "$lib" 2>/dev/null \
          | awk -F/ '{print $2}' >> "$pkgs_tmp" || true
    done < "$libs_tmp"

    # Конфликтующие пары: несколько пакетов предоставляют один soname
    # (jack2/pipewire-jack для libjack.so.0). Их нельзя ставить
    # одновременно, но оба попадают в вывод pacman -Fq. Решение:
    # для каждого пакета находим виртуальное имя, которое он
    # предоставляет через Provides, и пишем его в depends. Если
    # виртуального имени нет — оставляем имя пакета.
    #
    # mesa-amber — legacy-пакет, конфликтует с mesa на уровне файлов.
    # Виртуального имени у него нет, поэтому фильтруем отдельно.
    local raw_deps
    raw_deps=$(sort -u "$pkgs_tmp" \
                | grep -vxE 'glibc|gcc-libs|mesa-amber' \
                | grep -v '^$')

    local auto_deps=""
    local _owner _virtual _seen=" "
    while IFS= read -r _owner; do
        [ -z "$_owner" ] && continue
        # Берём только soname-style provides (содержит ".so")
        _virtual=$(pacman -Si "$_owner" 2>/dev/null \
                    | awk -F': ' '/^Provides/{print $2}' \
                    | tr ' ' '\n' \
                    | grep -vx "$_owner" \
                    | grep -vx 'None' \
                    | grep '\.so' \
                    | head -n1)
        if [ -z "$_virtual" ]; then
            _virtual="$_owner"
        fi
        case "$_seen" in
            *" $_virtual "*) continue ;;
        esac
        _seen="$_seen$_virtual "
        auto_deps="$auto_deps $_virtual"
    done <<EOF
$raw_deps
EOF
    auto_deps=$(printf '%s\n' $auto_deps | sort -u | tr '\n' ' ')

    if [ -z "${auto_deps// }" ]; then
        echo "[!] Автоопределение зависимостей не дало ни одного пакета." >&2
        echo "[!] Это значит, что 'pacman -Fq' не смог найти владельцев ни одной библиотеки." >&2
        echo "[!] Проверьте: sudo pacman -Fy && pacman -Fq /usr/lib/libgtk-3.so.0" >&2
        return 1
    fi

    # extra_deps не нужен: mesa и gdk-pixbuf2 приходят из ldd,
    # librsvg в Arch не предоставляет .so-загрузчик для gdk-pixbuf
    # (SVG идёт через glycin, который тянется с gtk3).
    local all_deps
    all_deps=$(printf '%s\n' $auto_deps | sort -u | tr '\n' ' ')

    echo "[i] auto-detected: $auto_deps"
    echo "[i] final:         $all_deps"

    cp -a "$STAGING_DIR/usr" "$workdir/staging_usr"

    # depends=('a' 'b' 'c') — через printf, чтобы не собирать строку руками.
    local -a deps_array=()
    local d
    for d in $all_deps; do
        deps_array+=("$d")
    done

    {
        cat <<EOF
pkgname=$PACKAGE_NAME
pkgver=$VERSION
pkgrel=1
pkgdesc="IPTV Playlist Player"
arch=('$APPIMAGE_ARCH')
url="https://github.com/i-jurij/$PACKAGE_NAME"
license=('MIT')
# !debug: без отдельного -debug пакета с символами — он не нужен
# в релизе и удваивает размер артефактов.
options=('!debug')
EOF
        printf "depends=("
        printf "'%s' " "${deps_array[@]}"
        printf ")\n"
        cat <<EOF

package() {
    install -d "\$pkgdir/usr"
    cp -a "\$startdir/staging_usr/." "\$pkgdir/usr/"
}
EOF
    } > "$workdir/PKGBUILD"

    # makepkg отказывается работать от root. Если мы root — создаём
    # (идемпотентно) пользователя builder и отдаём ему workdir.
    local makepkg_cmd
    if [ "$(id -u)" -eq 0 ]; then
        useradd -m builder 2>/dev/null || true
        chown -R builder:builder "$workdir"
        makepkg_cmd="runuser -u builder -- makepkg -f --nodeps --nocheck"
    else
        makepkg_cmd="makepkg -f --nodeps --nocheck"
    fi

    if ! ( cd "$workdir" && $makepkg_cmd ); then
        keep_workdir=1
        echo "[!] makepkg упал. Логи и PKGBUILD: $workdir" >&2
        echo "[!] Для отладки: cd $workdir && cat PKGBUILD" >&2
        return 1
    fi

    # Фильтруем debug-пакет — даже при options=('!debug') подстрахуемся.
    local built
    built=$(find "$workdir" -maxdepth 1 \
              -name '*.pkg.tar.zst' \
              ! -name '*-debug-*' \
              -print -quit)
    if [ -n "$built" ]; then
        mv "$built" "$pkg_file"
        echo "[✓] Нативный .pkg.tar.zst: $pkg_file"
    else
        keep_workdir=1
        echo "[!] makepkg не создал пакет. Содержимое: $workdir" >&2
        return 1
    fi

    # Снимаем RETURN-trap: он не сбрасывается автоматически и иначе
    # сработает на каждом последующем return с уже несуществующими
    # $libs_tmp/$pkgs_tmp/$workdir.
    trap - RETURN
    return 0
}