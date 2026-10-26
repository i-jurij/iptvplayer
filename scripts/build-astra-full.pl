#!/usr/bin/perl
# build-astra-full.pl — сборка iptvplayer с нуля для Astra Linux 2.12 (Debian 9 база).
#
# Этапы:
#   1. Установка системных зависимостей через apt (кроме --skip-system).
#   2. Скачивание и сборка wxWidgets (статически) в third_party/wx.
#   3. Скачивание и сборка wxSQLite3 (статически) в third_party/wxsqlite3.
#   4. Сборка проекта через CMake в build-release.
#   5. Установка в staging-каталог.
#   6. Упаковка в .deb (опционально — подпись detached .asc).
#
# Требования: perl (есть в Astra), build-essential, cmake, git,
# autoconf, automake, libtool, dpkg-dev, wget или curl.
#
# Использование:
#   ./build-astra-full.pl
#   ./build-astra-full.pl --skip-system        # не ставить системные пакеты
#   ./build-astra-full.pl --rebuild-deps       # пересобрать wx/wxSQLite3
#   ./build-astra-full.pl --sign --gpg-key 0xABCDEF
#   ./build-astra-full.pl --keep-build         # не удалять build-каталоги

use strict;
use warnings;
use File::Basename qw(basename dirname);
use File::Copy qw(copy);
use File::Path qw(make_path remove_tree);
use File::Temp qw(tempdir);
use Getopt::Long qw(GetOptions);
use Cwd qw(abs_path getcwd);

# ------------------------- Конфигурация -------------------------
my $PACKAGE     = "iptvplayer";
my $METAINFO    = "iptvplayer.metainfo.xml";
my $ICON_NAME   = "iptvplayer.svg";
my $WX_VERSION  = $ENV{WX_VERSION}      // "3.3.2";
my $WXSQLITE3_VERSION = $ENV{WXSQLITE3_VERSION} // "5.0.1";

my $PROJECT_ROOT = abs_path(dirname(__FILE__) . "/..") // die "Не могу определить корень проекта";
my $THIRD_PARTY  = "$PROJECT_ROOT/third_party";
my $WX_DIR       = "$THIRD_PARTY/wx";
my $WXSQLITE3_DIR = "$THIRD_PARTY/wxsqlite3";
my $BUILD_DIR    = "$PROJECT_ROOT/build-release";
my $STAGING      = "$PROJECT_ROOT/pkg-staging-astra";
my $OUTPUT_DIR   = "$PROJECT_ROOT/dist";
use constant DISTRO => 'astra';

my $SUDO = '';
if ($> != 0) {
    if (system('sh', '-c', 'command -v sudo >/dev/null 2>&1') == 0) {
        $SUDO = 'sudo';
    }
}

# ------------------------- Аргументы -------------------------
my ($skip_system, $rebuild_deps, $sign, $gpg_key, $keep_build);
GetOptions(
    "skip-system"  => \$skip_system,
    "rebuild-deps" => \$rebuild_deps,
    "sign"         => \$sign,
    "gpg-key=s"    => \$gpg_key,
    "keep-build"   => \$keep_build,
) or die "Использование: $0 [--skip-system] [--rebuild-deps] [--sign --gpg-key KEY] [--keep-build]\n";

# ------------------------- Утилиты -------------------------
sub info     { print "[INFO] @_\n" }
sub warn_msg { print STDERR "[WARN] @_\n" }
sub error    { print STDERR "[ERROR] @_\n"; exit 1 }

sub run {
    my (@cmd) = @_;
    info "Выполняю: @cmd";
    system(@cmd) == 0 or error "Команда завершилась с ошибкой: @cmd";
}

sub run_in_dir {
    my ($dir, @cmd) = @_;
    my $old = getcwd();
    chdir $dir or error "Не могу перейти в $dir: $!";
    my $rc = system(@cmd);
    chdir $old;
    $rc == 0 or error "Команда завершилась с ошибкой в $dir: @cmd";
}

sub download {
    my ($url, $out) = @_;
    info "Скачивание: $url";
    if (system("wget", "-q", "-O", $out, $url) != 0) {
        if (system("curl", "-fsSL", "-o", $out, $url) != 0) {
            error "Не удалось скачать $url (ни wget, ни curl)";
        }
    }
    -s $out or error "Скачанный файл пуст: $out";
}

sub read_first_line {
    my ($path) = @_;
    open my $fh, '<', $path or return '';
    my $line = <$fh>;
    close $fh;
    $line //= '';
    $line =~ s/^\s+|\s+$//g;
    return $line;
}

sub detect_deb_arch {
    my $machine = `uname -m`;
    chomp $machine;
    my %map = (
        x86_64 => 'amd64', amd64 => 'amd64',
        aarch64 => 'arm64', arm64 => 'arm64',
        armv7l => 'armhf', armv7 => 'armhf',
        i686 => 'i386', i386 => 'i386',
    );
    return $map{$machine} // error "Неподдерживаемая архитектура: $machine";
}
sub nproc {
    my $n = `nproc 2>/dev/null`;
    chomp $n;
    $n =~ s/\D//g;
    return $n || 1;
}

sub write_desktop_file {
    my ($path) = @_;
    (my $icon_base = $ICON_NAME) =~ s/\.svg$//;
    make_path(dirname($path));
    open my $fh, '>', $path or error "Не могу открыть $path: $!";
    print $fh <<"DESKTOP";
[Desktop Entry]
Name=IPTV Player
Exec=$PACKAGE %F
Icon=$icon_base
Type=Application
Categories=AudioVideo;
Comment=IPTV Playlist Player
Comment[ru]=Плеер для плейлистов IPTV
Terminal=false
StartupNotify=true
StartupWMClass=$PACKAGE
MimeType=video/mp4;video/x-matroska;video/avi;application/x-mpegURL;audio/x-mpegurl;audio/x-scpls;application/xspf+xml;
DESKTOP
    close $fh;
}

# ------------------------- 1. Системные зависимости -------------------------
sub install_system_deps {
    return if $skip_system;
    info "Установка системных зависимостей...";

    # Список пакетов для Astra 2.12 (Debian 9): libfreetype6-dev, а не libfreetype-dev.
    my @pkgs = qw(
        build-essential cmake git autoconf automake libtool pkg-config
        wget curl libcurl4-openssl-dev libmpv-dev libgtk-3-dev
        librsvg2-common libx11-dev libx11-xcb-dev libgl1-mesa-dev
        libegl1-mesa-dev libpng-dev libjpeg-dev libwebp-dev zlib1g-dev
        libexpat1-dev libfreetype6-dev dpkg-dev
    );

    # rapidjson в Astra 2.12 может отсутствовать — ставим отдельно.
    my $has_rapidjson = 0;
    for my $inc (qw(/usr/include/rapidjson/document.h /usr/local/include/rapidjson/document.h)) {
        $has_rapidjson = 1 if -f $inc;
    }

    if ($> != 0 && $SUDO eq '') {
        error "Установка системных пакетов требует root, а sudo не найден. "
            . "Запустите от root или с --skip-system.";
    }

    my @missing = grep { system("dpkg -s $_ >/dev/null 2>&1") != 0 } @pkgs;
    if (@missing) {
        info "Устанавливаю: @missing";
        my @sudo = $SUDO ? ($SUDO) : ();
        run(@sudo, "apt-get", "update", "-qq");
        run(@sudo, "apt-get", "install", "-y", "--no-install-recommends", @missing);
    }

    # rapidjson: header-only fallback.
    unless ($has_rapidjson) {
        info "rapidjson не найден, устанавливаю заголовки вручную...";
        my $tmp = tempdir(CLEANUP => 1);
        my $tar = "$tmp/rapidjson.tar.gz";
        download("https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz", $tar);
        run("tar", "xzf", $tar, "-C", $tmp);
        my @sudo = $SUDO ? ($SUDO) : ();
        run(@sudo, "mkdir", "-p", "/usr/local/include/rapidjson");
        run(@sudo, "cp", "-r", "$tmp/rapidjson-1.1.0/include/rapidjson/.", "/usr/local/include/rapidjson/");
        info "rapidjson установлен в /usr/local/include/rapidjson";
    }
}

# ------------------------- 2. wxWidgets -------------------------
sub build_wxwidgets {
    if (-d $WX_DIR && !$rebuild_deps) {
        info "wxWidgets уже собран, пропускаю (--rebuild-deps для пересборки)";
        return;
    }

    info "Сборка wxWidgets $WX_VERSION (cmake)...";
    remove_tree($WX_DIR) if -d $WX_DIR;
    make_path($WX_DIR);

    my $archive = "wxWidgets-$WX_VERSION.tar.bz2";
    my $url = "https://github.com/wxWidgets/wxWidgets/releases/download/v$WX_VERSION/$archive";
    download($url, "$WX_DIR/$archive");
    run("tar", "xjf", "$WX_DIR/$archive", "-C", $WX_DIR);
    rename("$WX_DIR/wxWidgets-$WX_VERSION", "$WX_DIR/src")
        or error "rename распакованного каталога: $!";
    unlink "$WX_DIR/$archive";

    make_path("$WX_DIR/build", "$WX_DIR/install");
    chdir "$WX_DIR/build" or error "chdir $WX_DIR/build: $!";

    my @cmake = (
        "cmake", "../src",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        "-DwxBUILD_SHARED=OFF",
        "-DwxUSE_LIBWEBP=builtin",
        "-DwxUSE_LIBJPEG=builtin",
        "-DwxUSE_SVG=ON",
        "-DwxUSE_REGEX:BOOL=OFF",
        "-DwxUSE_MEDIACTRL:BOOL=OFF",
        "-DwxUSE_GSTREAMER:BOOL=OFF",
        "-DwxUSE_WEBKIT:BOOL=OFF",
        "-DwxUSE_WEBVIEW_WEBKIT:BOOL=OFF",
        "-DwxUSE_LIBTIFF:BOOL=OFF",
        "-DwxUSE_GIF:BOOL=OFF",
        "-DwxUSE_ACCESSIBILITY:BOOL=OFF",
        "-DwxUSE_HELP:BOOL=OFF",
        "-DwxUSE_HTML:BOOL=OFF",
        "-DwxUSE_STC:BOOL=OFF",
        "-DwxUSE_MS_HTML_HELP:BOOL=OFF",
        "-DwxUSE_WXHTML_HELP:BOOL=OFF",
        "-DwxUSE_XRC:BOOL=OFF",
        "-DwxUSE_XML:BOOL=OFF",
        "-DwxUSE_NET:BOOL=OFF",
        "-DwxUSE_RICHTEXT:BOOL=OFF",
        "-DwxUSE_LIBSDL:BOOL=OFF",
        "-DwxUSE_PRINTING_ARCHITECTURE=OFF",
        "-DCMAKE_INSTALL_PREFIX=../install",
    );
    run(@cmake);
    run("cmake", "--build", ".", "--target", "install", "-j" . nproc());

    chdir $PROJECT_ROOT or error "chdir $PROJECT_ROOT: $!";

    my $cfg = "$WX_DIR/install/lib/cmake/wxWidgets/wxWidgetsConfig.cmake";
    -f $cfg or error "wxWidgetsConfig.cmake не появился: $cfg. "
                   . "Без него find_package(wxWidgets CONFIG) из CMakeLists.txt не сработает.";
    info "wxWidgets установлен в $WX_DIR/install";
}

# ------------------------- 3. wxSQLite3 -------------------------
sub build_wxsqlite3 {
    if (-d $WXSQLITE3_DIR && !$rebuild_deps) {
        info "wxSQLite3 уже собран, пропускаю (--rebuild-deps для пересборки)";
        return;
    }

    info "Сборка wxSQLite3 $WXSQLITE3_VERSION...";
    remove_tree($WXSQLITE3_DIR) if -d $WXSQLITE3_DIR;
    make_path($WXSQLITE3_DIR);

    my $archive = "wxsqlite3-$WXSQLITE3_VERSION.tar.gz";
    my $url = "https://github.com/utelle/wxsqlite3/archive/refs/tags/v$WXSQLITE3_VERSION.tar.gz";

    download($url, "$WXSQLITE3_DIR/$archive");
    run("tar", "xzf", "$WXSQLITE3_DIR/$archive", "-C", $WXSQLITE3_DIR);
    # Имя каталога может быть с "v" или без.
    my $src = "$WXSQLITE3_DIR/wxsqlite3-$WXSQLITE3_VERSION";
    $src = "$WXSQLITE3_DIR/wxsqlite3-v$WXSQLITE3_VERSION" unless -d $src;
    rename($src, "$WXSQLITE3_DIR/src") or error "Не могу переименовать распакованный каталог";
    unlink "$WXSQLITE3_DIR/$archive";

    # autoreconf для генерации configure.
    run_in_dir("$WXSQLITE3_DIR/src", "autoreconf", "-fi");

    make_path("$WXSQLITE3_DIR/build", "$WXSQLITE3_DIR/install");
    chdir "$WXSQLITE3_DIR/build" or error $!;

    my $wx_config = "$WX_DIR/install/bin/wx-config";
    -x $wx_config or error "wx-config не найден: $wx_config";

    $ENV{CXXFLAGS} = join(" ", grep { $_ } ($ENV{CXXFLAGS} // "", "-std=c++20"));

    my @configure = (
        "../src/configure",
        "--prefix=$WXSQLITE3_DIR/install",
        "--with-wx-config=$wx_config",
        "--disable-shared",
        "--enable-static",
        "--without-sqlcipher",
    );

    run(@configure);
    run("make", "-j" . nproc());
    run("make", "install");

    chdir $PROJECT_ROOT;
    info "wxSQLite3 установлен в $WXSQLITE3_DIR/install";
}

# ------------------------- 4. Сборка проекта -------------------------
sub build_project {
    info "Сборка $PACKAGE...";
    remove_tree($BUILD_DIR) if -d $BUILD_DIR;
    make_path($BUILD_DIR);

    chdir $BUILD_DIR or error $!;

    my $wx_root = "$WX_DIR/install";
    my $wxsqlite3_root = "$WXSQLITE3_DIR/install";

    my @cmake = (
        "cmake", $PROJECT_ROOT,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$STAGING/usr",
        "-DwxWidgets_DIR=$WX_DIR/install/lib/cmake",
        "-DwxWidgets_ROOT_DIR=$wx_root",
        "-DwxWidgets_LIB_DIR=$wx_root/lib",
        "-DwxSQLite3_ROOT_DIR=$wxsqlite3_root",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
    );
    run(@cmake);

    run("cmake", "--build", ".", "--target", "all", "-j" . nproc());
    run("cmake", "--build", ".", "--target", "install");

    chdir $PROJECT_ROOT;
    info "Проект установлен в $STAGING/usr";
}

# ------------------------- 5. Упаковка в .deb -------------------------
sub build_deb {
    info "Упаковка в .deb...";

    # === Дополняем staging тем, что CMakeLists не ставит ===
    # 1. .desktop-файл для меню
    my $desktop = "$STAGING/usr/share/applications/$PACKAGE.desktop";
    write_desktop_file($desktop);

    # 2. Иконка hicolor (CMakeLists ставит её только в share/iptvplayer/icons/)
    my $icon_src = "$STAGING/usr/share/$PACKAGE/icons/$ICON_NAME";
    if (-f $icon_src) {
        my $icon_dst = "$STAGING/usr/share/icons/hicolor/scalable/apps/$ICON_NAME";
        make_path(dirname($icon_dst));
        copy($icon_src, $icon_dst) or error "copy $icon_src → $icon_dst: $!";
    } else {
        warn_msg "Иконка не найдена в staging: $icon_src";
    }

    my $version      = read_first_line("$STAGING/usr/VERSION");
    # VERSION* не должны попасть в .deb — они нужны только build-package.sh,
    # а не конечной системе.
    unlink "$STAGING/usr/VERSION",
           "$STAGING/usr/VERSION_FULL",
           "$STAGING/usr/VERSION_FILE";
    my $version_file = read_first_line("$STAGING/usr/VERSION_FILE");
    my $version_full = read_first_line("$STAGING/usr/VERSION_FULL");
    error "Пустой VERSION в $STAGING/usr" unless length $version;

    my $deb_arch = detect_deb_arch();
    my $deb_name = "${PACKAGE}_${version}_DISTRO_${deb_arch}.deb";
    my $deb_path = "$OUTPUT_DIR/$deb_name";

    make_path($OUTPUT_DIR);
    make_path("$STAGING/DEBIAN");

    # Определение зависимостей через dpkg-shlibdeps.
    my $bin = "$STAGING/usr/bin/$PACKAGE";
    -f $bin or error "Бинарник не найден: $bin";

    my $tmp_deps = tempdir(CLEANUP => 1);
    make_path("$tmp_deps/debian");
    open my $cf, '>', "$tmp_deps/debian/control" or error $!;
    print $cf "Source: $PACKAGE\nPackage: $PACKAGE\nArchitecture: any\n";
    close $cf;

    my $out = `cd '$tmp_deps' && dpkg-shlibdeps -O '$bin' 2>&1`;
    my ($depends) = $out =~ /shlibs:Depends=(.+)/;
    error "dpkg-shlibdeps не вернул Depends" unless $depends;
    info "Depends: $depends";

    # control
    open my $ctrl, '>', "$STAGING/DEBIAN/control" or error $!;
    print $ctrl <<"CTRL";
Package: $PACKAGE
Version: $version
Section: video
Priority: optional
Architecture: $deb_arch
Depends: $depends
Maintainer: ijurij <mnisjil\@duck.com>
Homepage: https://github.com/i-jurij/$PACKAGE
Description: IPTV Playlist Player
 A simple player for M3U playlists with GUI.
CTRL
    close $ctrl;

    # postinst
    my $postinst = "$STAGING/DEBIAN/postinst";
    open my $pi, '>', $postinst or error $!;
    print $pi <<'POSTINST';
#!/bin/bash
set -e
if [ -x /usr/bin/update-icon-caches ]; then
    /usr/bin/update-icon-caches /usr/share/icons/hicolor || true
fi
POSTINST
    close $pi;
    chmod 0755, $postinst;

    # prerm
    my $prerm = "$STAGING/DEBIAN/prerm";
    open my $pr, '>', $prerm or error $!;
    print $pr <<"PRERM";
#!/bin/bash
set -e
if [ \$1 = "remove" ] || [ \$1 = "purge" ]; then
    rm -f "/usr/share/applications/$PACKAGE.desktop"
    rm -f "/usr/share/icons/hicolor/scalable/apps/$ICON_NAME"
fi
PRERM
    close $pr;
    chmod 0755, $prerm;

    # Права
    system("chmod", "-R", "u+rwX,go+rX,go-w", "$STAGING/usr") == 0 or error "chmod failed";

    # Сборка deb
    unlink $deb_path if -f $deb_path;
    run("dpkg-deb", "-Zxz", "--build", "--root-owner-group", $STAGING, $deb_path);
    info "Создан: $deb_path";

    # Подпись (опционально, только detached .asc — debsigs в Astra не используется)
    if ($sign) {
        my $key = $gpg_key // $ENV{GPG_KEY_ID};
        if ($key) {
            my $asc = "$deb_path.asc";
            if (system("gpg", "--yes", "--detach-sign", "--armor",
                       "--local-user", $key, "--output", $asc, $deb_path) == 0) {
                info "Отсоединённая подпись: $asc";
            } else {
                warn_msg "Не удалось создать .asc";
            }
        } else {
            warn_msg "--sign указан, но ключ не задан (--gpg-key или GPG_KEY_ID)";
        }
    }
}

# ------------------------- main -------------------------
sub main {
    info "=== Сборка $PACKAGE для Astra Linux (база Debian 9) ===";
    if ($> != 0 && $SUDO eq '' && !$skip_system) {
        error "Скрипт запущен без root и без sudo, но нужны apt-get и /usr/local. "
            . "Запустите от root, установите sudo или используйте --skip-system.";
    }
    info $SUDO ? "Режим: через sudo" : ($> == 0 ? "Режим: root" : "Режим: пользователь, --skip-system");
    info "Корень проекта: $PROJECT_ROOT";

    install_system_deps();
    build_wxwidgets();
    build_wxsqlite3();
    build_project();
    build_deb();


    info "Готово. Артефакт: $OUTPUT_DIR/${PACKAGE}_*_" . DISTRO . "_*.deb";
}

main();