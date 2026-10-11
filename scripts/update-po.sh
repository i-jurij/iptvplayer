#!/bin/sh
# =============================================================================
# update-po.sh — регенерация po/iptvplayer.pot и обновление существующих .po
# =============================================================================
set -e

command -v xgettext >/dev/null 2>&1 || {
    echo "[!] xgettext не найден. Установи пакет gettext." >&2
    exit 1
}
command -v msgmerge >/dev/null 2>&1 || {
    echo "[!] msgmerge не найден. Установи пакет gettext." >&2
    exit 1
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
cd "$PROJECT_ROOT"

mkdir -p po

SOURCES=$(find src \( -name '*.cpp' -o -name '*.h' \) -type f)

xgettext \
    --keyword=_U \
    --from-code=UTF-8 \
    --msgid-bugs-address='https://github.com/i-jurij/iptvplayer/issues' \
    --package-name=iptvplayer \
    --output=po/iptvplayer.pot \
    $SOURCES

echo "[✓] po/iptvplayer.pot обновлён."

for po in po/*.po; do
    [ -f "$po" ] || continue
    echo "[+] msgmerge $po"
    msgmerge --update --backup=none "$po" po/iptvplayer.pot
done

echo "[✓] Готово."