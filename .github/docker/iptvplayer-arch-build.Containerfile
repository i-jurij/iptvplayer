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

# ВСЕ в одном RUN: установка пакетов, генерация кэшей и debloated Mesa.
RUN pacman -Syu --noconfirm --needed \
        base-devel \
        cmake git wget curl file tar xz zstd bzip2 patchelf \
        gnupg python \
        mpv gtk3 gdk-pixbuf2 librsvg \
        libjpeg-turbo expat zlib libwebp freetype2 libpng rapidjson \
        libx11 libxcb mesa \
        xfwm4 xfce4-panel xfdesktop xfce4-session \
        xfce4-settings xfce4-appfinder xfconf \
        gsettings-desktop-schemas dconf \
        hicolor-icon-theme adwaita-icon-theme \
        ttf-dejavu shared-mime-info \
        wayland wayland-protocols libxkbcommon \
        xorg-server-xvfb xorg-xauth weston \
        desktop-file-utils gtk-update-icon-cache \
    && curl -fsSL \
        https://raw.githubusercontent.com/pkgforge-dev/Anylinux-AppImages/main/useful-tools/get-debloated-pkgs.sh \
        -o /usr/local/bin/get-debloated-pkgs.sh \
    && chmod +x /usr/local/bin/get-debloated-pkgs.sh \
    && /usr/local/bin/get-debloated-pkgs.sh --add-mesa \
    && gdk-pixbuf-query-loaders --update-cache \
    && ( ls /usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/ 2>/dev/null \
         | grep -q pixbufloader \
         || { echo "=== ERROR: gdk-pixbuf loaders missing ==="; \
              ls -la /usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/ 2>&1; \
              exit 1; } ) \
    && fc-cache -f \
    && update-mime-database /usr/share/mime \
    && glib-compile-schemas /usr/share/glib-2.0/schemas \
    && gtk-update-icon-cache -f -t /usr/share/icons/hicolor \
    && update-desktop-database -q \
    && pacman -Scc --noconfirm

LABEL org.opencontainers.image.source="https://github.com/i-jurij/iptvplayer"
LABEL org.opencontainers.image.description="Arch Linux build image for iptvplayer sharun-AppImage"
LABEL org.opencontainers.image.licenses="MIT"
LABEL org.iptvplayer.debloated-release-updated="${DEBLOATED_RELEASE_UPDATED}"
LABEL org.iptvplayer.debloated-script-sha="${DEBLOATED_SCRIPT_SHA}"