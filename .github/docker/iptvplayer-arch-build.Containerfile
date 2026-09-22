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

# Минимальный GTK-рабочий стол Xfce и другие зависимости.
# xorg-server-xvfb и weston — headless-серверы для трассировки dlopen (X11 и Wayland).
RUN pacman -Syu --noconfirm --needed \
        base-devel \
        cmake git wget curl file tar xz zstd bzip2 patchelf \
        gnupg python \
        mpv \
        xfwm4 xfce4-panel xfdesktop xfce4-session \
        xfce4-settings xfce4-appfinder xfconf \
        gtk3 gdk-pixbuf2 librsvg \
        gsettings-desktop-schemas dconf \
        hicolor-icon-theme adwaita-icon-theme \
        ttf-dejavu shared-mime-info \
        libx11 libxcb \
        wayland wayland-protocols libxkbcommon \
        mesa \
        xorg-server-xvfb xorg-xauth weston \
        desktop-file-utils gtk-update-icon-cache \
    && pacman -Scc --noconfirm

# Post-install хуки pacman в Docker могут не отработать — повторяем вручную.
RUN gdk-pixbuf-query-loaders --update-cache \
    && fc-cache -f \
    && update-mime-database /usr/share/mime \
    && glib-compile-schemas /usr/share/glib-2.0/schemas \
    && gtk-update-icon-cache -f -t /usr/share/icons/hicolor \
    && update-desktop-database -q

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