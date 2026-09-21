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
#   librsvg — SVG-лоадер для gdk-pixbuf (build-sharun.sh его явно проверяет).
RUN pacman -Syu --noconfirm --needed \
        base-devel \
        cmake git wget curl file tar xz zstd bzip2 patchelf \
        gnupg python \
        mpv gtk3 gdk-pixbuf2 librsvg \
        libjpeg-turbo expat zlib libwebp freetype2 libpng rapidjson \
        libx11 libxcb mesa \
    && pacman -Scc --noconfirm

# Debloated Mesa. Скрипт скачивает mesa-mini, vulkan-*-mini и связанные
# пакеты из archlinux-pkgs-debloated и ставит их через pacman -U.
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

# Файловая база pacman (-Fy) нужна для автодетекта зависимостей нативного
# пакета: ldd → pacman -Fq → имя пакета. Кэшируется в образе, чтобы
# не тянуть ~100 МБ файловой базы на каждый релиз.
RUN pacman -Fy