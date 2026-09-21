# =============================================================================
# iptvplayer-arch-build — образ для CI-сборки sharun-AppImage.
# =============================================================================
#
# Arch Linux с полным тулчейном и debloated Mesa (mesa-mini без libLLVM).
# Образ пересобирается:
#   - job-ом arch-image в release.yml, если upstream изменился;
#   - workflow rebuild-arch-image.yml по квартальному расписанию;
#   - при правках этого Containerfile (push trigger в rebuild-arch-image.yml);
#   - вручную через workflow_dispatch.
#
# LABEL-ы org.iptvplayer.* хранят upstream-сигналы на момент сборки.
# Проверка перед пересборкой читает их через docker inspect и сравнивает
# с текущими значениями на GitHub. Если совпадают — сборка пропускается.
# =============================================================================

FROM archlinux:latest

ARG DEBLOATED_RELEASE_UPDATED=unknown
ARG DEBLOATED_SCRIPT_SHA=unknown

# Полный тулчейн + runtime-библиотеки одним слоем.
#   -Syu вместо -Sy: rolling-дистрибутив не поддерживает частичные
#   апгрейды, база и репозитории должны быть согласованы.
#   bzip2 — для распаковки wxWidgets-3.3.2.tar.bz2.
#   librsvg — SVG-лоадер для gdk-pixbuf (нужен для отрисовки SVG-иконок
#     приложения в рантайме).
#   xorg-server-xvfb — нужен quick-sharun для трассировки dlopen
#     (запускает GUI-приложение в виртуальном X-дисплее и смотрит
#      через strace/LD_DEBUG, какие библиотеки оно подгружает).
#   desktop-file-utils, gtk-update-icon-cache — для генерации кэшей
#     ниже (см. отдельный RUN).
RUN pacman -Syu --noconfirm --needed \
        base-devel \
        cmake git wget curl file tar xz zstd bzip2 patchelf \
        gnupg python \
        mpv gtk3 gdk-pixbuf2 librsvg \
        libjpeg-turbo expat zlib libwebp freetype2 libpng rapidjson \
        libx11 libxcb mesa \
        xorg-server-xvfb desktop-file-utils gtk-update-icon-cache \
    && pacman -Scc --noconfirm

# Явно генерируем кэши, которые обычно создаются post-install скриптами
# pacman. В Docker-сборке эти скрипты иногда не отрабатывают, из-за чего:
#   - без loaders.cache gdk-pixbuf в рантайме не видит ни одного лоадера,
#     даже если .so уже развёрнуты в AppDir. Сам SVG-.so quick-sharun
#     находит глобом по LIB_DIR и разворачивает независимо от кэша —
#     кэш нужен именно в рантайме, чтобы gdk-pixbuf знал про .so;
#   - без mime.cache/schemas/icon-theme.cache приложения не подхватывают
#     MIME-типы, GSettings-схемы и иконки.
#
# Ошибка любого апдейтера валит сборку образа намеренно: тихо
# недособранный образ не нужен — лучше узнать о проблеме здесь, чем
# потом ловить «почему в AppImage нет SVG».
RUN gdk-pixbuf-query-loaders --update-cache \
    && fc-cache -f \
    && update-mime-database /usr/share/mime \
    && glib-compile-schemas /usr/share/glib-2.0/schemas \
    && gtk-update-icon-cache -f -t /usr/share/icons/hicolor \
    && update-desktop-database -q

# Debloated Mesa. Скрипт скачивает mesa-mini, vulkan-*-mini и связанные
# пакеты из archlinux-pkgs-debloated и ставит их через pacman -U.
# Без флага --prefer-mini: скрипт сам ставит mini по умолчанию.
# nano не берём — по README может иметь проблемы с производительностью.
RUN curl -fsSL \
        https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/main/useful-tools/get-debloated-pkgs.sh \
        -o /usr/local/bin/get-debloated-pkgs.sh \
    && chmod +x /usr/local/bin/get-debloated-pkgs.sh \
    && /usr/local/bin/get-debloated-pkgs.sh --add-mesa \
    && pacman -Scc --noconfirm

LABEL org.opencontainers.image.source="https://github.com/i-jurij/iptvplayer"
LABEL org.opencontainers.image.description="Arch Linux build image for iptvplayer sharun-AppImage"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.iptvplayer.debloated-release-updated="${DEBLOATED_RELEASE_UPDATED}"
LABEL org.iptvplayer.debloated-script-sha="${DEBLOATED_SCRIPT_SHA}"