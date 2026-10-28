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
#   ./build-astra-full.pl --rebuild-wxsqlite3   # пересобрать wxSQLite3, продолжить полную сборку
#   ./build-astra-full.pl --only-wxsqlite3      # пересобрать ТОЛЬКО wxSQLite3 и выйти
#   ./build-astra-full.pl --sign --gpg-key 0xABCDEF
#   ./build-astra-full.pl --keep-build         # не удалять build-каталоги

use strict;

# Автофлаш STDOUT: без этого [INFO]-строки видны только после
# заполнения буфера (при редиректе/пайпе — вообще в конце).
$| = 1;

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
my ($skip_system, $rebuild_deps, $rebuild_wxsqlite3, $only_wxsqlite3,
    $sign, $gpg_key, $keep_build);
GetOptions(
    "skip-system"       => \$skip_system,
    "rebuild-deps"      => \$rebuild_deps,
    "rebuild-wxsqlite3" => \$rebuild_wxsqlite3,
    "only-wxsqlite3"    => \$only_wxsqlite3,
    "sign"              => \$sign,
    "gpg-key=s"         => \$gpg_key,
    "keep-build"        => \$keep_build,
) or die "Использование: $0 [--skip-system] [--rebuild-deps] [--rebuild-wxsqlite3] [--only-wxsqlite3] [--sign --gpg-key KEY] [--keep-build]\n";

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

    # Прогресс нужен только когда пользователь смотрит в терминал.
    # В CI/редиректе — тихий режим, чтобы не засорять логи.
    my $is_tty = -t STDERR;  # wget/curl пишут прогресс на stderr

    my $have_wget = (system("sh", "-c", "command -v wget >/dev/null 2>&1") == 0);
    my $have_curl = (system("sh", "-c", "command -v curl >/dev/null 2>&1") == 0);
    ($have_wget || $have_curl)
        or error "Ни wget, ни curl не найдены в PATH";

    info "Скачивание: $url";
    my $ok = 0;

    if ($have_wget) {
        my @args = (
            "wget",
            "--tries=1",           # ретраи делает сам sub download (внешний цикл), а не wget
            "--timeout=30",        # таймаут на установку соединения
            "--read-timeout=60",   # таймаут на паузы при чтении
            "-O", $out,
        );
        # --show-progress есть в wget 1.16+ (Debian 9: 1.18, Astra 2.12: 1.19).
        # В TTY — прогресс-бар, иначе -q (тихо).
        push @args, ($is_tty ? "--show-progress" : "-q");
        push @args, $url;
        $ok = (system(@args) == 0);
    }

    if (!$ok && $have_curl) {
        my @args = (
            "curl", "-L", "-f",
            "--connect-timeout", "30",
            "-o", $out,
        );
        # --progress-bar = короткая однострочная полоса с процентами
        # и скоростью. В TTY — она, иначе -sS (silent, но ошибки видны).
        push @args, ($is_tty ? "--progress-bar" : "-sS");
        push @args, $url;
        $ok = (system(@args) == 0);
    }

    $ok or error "Не удалось скачать $url";

    -s $out or error "Скачанный файл пуст: $out";

    my $size = -s $out;
    my $size_h = $size >= 1048576
        ? sprintf("%.1f MB", $size / 1048576)
        : sprintf("%.0f KB", $size / 1024);
    info "Скачано: $out ($size_h)";
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
        build-essential gcc-mozilla cmake git autoconf automake libtool pkg-config
        wget curl libcurl4-openssl-dev libmpv-dev libgtk-3-dev
        librsvg2-common libx11-dev libx11-xcb-dev libgl1-mesa-dev
        libegl1-mesa-dev libpng-dev libjpeg-dev libwebp-dev zlib1g-dev
        libexpat1-dev libfreetype6-dev dpkg-dev gettext
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
    my @wx_cfg = glob("$WX_DIR/install/lib/cmake/wxWidgets*/wxWidgetsConfig.cmake");
    if (@wx_cfg && !$rebuild_deps) {
        info "wxWidgets уже собран, пропускаю (--rebuild-deps для пересборки)";
        return;
    }
    if (-d $WX_DIR) {
        info "wxWidgets: каталог есть, CMake-конфига нет — пересобираю.";
        remove_tree($WX_DIR);
    }

    info "Сборка wxWidgets $WX_VERSION (cmake)...";
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
        "-DCMAKE_CXX_STANDARD=17",
        "-DCMAKE_INSTALL_LIBDIR=lib",
        "-DwxBUILD_SHARED=OFF",
        "-DwxUSE_GLCANVAS=ON",
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
        "-DwxUSE_EXPAT:BOOL=OFF",
        "-DwxUSE_NET:BOOL=OFF",
        "-DwxUSE_RICHTEXT:BOOL=OFF",
        "-DwxUSE_LIBSDL:BOOL=OFF",
        "-DwxUSE_PRINTING_ARCHITECTURE=OFF",
        "-DwxBUILD_SAMPLES=OFF",
        "-DwxBUILD_DEMOS=OFF",
        "-DwxBUILD_TESTS=OFF",
        "-DwxBUILD_BENCHMARKS=OFF",
        "-DCMAKE_INSTALL_PREFIX=../install",
    );
    run(@cmake);
    run("cmake", "--build", ".", "--target", "install", "-j" . nproc());

    chdir $PROJECT_ROOT or error "chdir $PROJECT_ROOT: $!";

    # wxWidgets кладёт CMake-конфиг по-разному в зависимости от версии:
    #   3.2.x → lib/cmake/wxWidgets/wxWidgetsConfig.cmake
    #   3.3.x → lib/cmake/wxWidgets-3.3/wxWidgetsConfig.cmake
    # В некоторых сборках — lib/cmake/wxWidgetsConfig.cmake без подкаталога.
    my $cfg_dir = "$WX_DIR/install/lib/cmake";
    my @cfg = glob("$cfg_dir/wxWidgets*/wxWidgetsConfig.cmake");
    push @cfg, "$cfg_dir/wxWidgetsConfig.cmake" if -f "$cfg_dir/wxWidgetsConfig.cmake";

    @cfg or error "wxWidgetsConfig.cmake не найден в $cfg_dir. "
                . "Без него find_package(wxWidgets CONFIG) из CMakeLists.txt не сработает.";
    info "wxWidgets установлен в $WX_DIR/install (config: $cfg[0])";
}

# ------------------------- 3. wxSQLite3 -------------------------
sub build_wxsqlite3 {
    my @existing = glob("$WXSQLITE3_DIR/install/lib/libwxcode*.a");
    if (@existing && !$rebuild_deps && !$rebuild_wxsqlite3) {
        info "wxSQLite3 уже собран ($existing[0]), пропускаю "
           . "(--rebuild-deps / --rebuild-wxsqlite3 для пересборки)";
        return;
    }
    if (-d $WXSQLITE3_DIR) {
        if ($rebuild_wxsqlite3 || $rebuild_deps) {
            info "wxSQLite3: принудительная пересборка "
               . "(--rebuild-wxsqlite3 / --rebuild-deps)";
        } else {
            info "wxSQLite3: каталог есть, библиотеки нет — пересобираю.";
        }
        remove_tree($WXSQLITE3_DIR);
    }

    info "Сборка wxSQLite3 $WXSQLITE3_VERSION...";
    remove_tree($WXSQLITE3_DIR) if -d $WXSQLITE3_DIR;
    make_path($WXSQLITE3_DIR);

    my $archive = "v$WXSQLITE3_VERSION.tar.gz";

    # Два URL, как в bash: codeload первым (минует прокси github.com,
    # который часто отдаёт 504), github-archive вторым.
    my @urls = (
        "https://codeload.github.com/utelle/wxsqlite3/tar.gz/refs/tags/v$WXSQLITE3_VERSION",
        "https://github.com/utelle/wxsqlite3/archive/refs/tags/v$WXSQLITE3_VERSION.tar.gz",
    );
    my $downloaded = 0;
    for my $u (@urls) {
        if (eval { download($u, "$WXSQLITE3_DIR/$archive"); 1 }) {
            $downloaded = 1; last;
        }
        warn_msg "Не удалось скачать $u, пробую следующий";
    }
    $downloaded or error "Не удалось скачать wxSQLite3 ни с одного URL";

    run("tar", "xzf", "$WXSQLITE3_DIR/$archive", "-C", $WXSQLITE3_DIR);
    my $src = "$WXSQLITE3_DIR/wxsqlite3-$WXSQLITE3_VERSION";
    $src = "$WXSQLITE3_DIR/wxsqlite3-v$WXSQLITE3_VERSION" unless -d $src;
    rename($src, "$WXSQLITE3_DIR/src") or error "Не могу переименовать распакованный каталог: $!";
    unlink "$WXSQLITE3_DIR/$archive";

    run_in_dir("$WXSQLITE3_DIR/src", "autoreconf", "-fi");

    make_path("$WXSQLITE3_DIR/build", "$WXSQLITE3_DIR/install");
    chdir "$WXSQLITE3_DIR/build" or error "chdir $WXSQLITE3_DIR/build: $!";

    my $wx_config = "$WX_DIR/install/bin/wx-config";
    -x $wx_config or error "wx-config не найден: $wx_config";

    $ENV{CXXFLAGS} = join(" ", grep { $_ } ($ENV{CXXFLAGS} // "", "-std=c++17"));

    # Все 4 флага --without-* обязательны, как в bash. Без aes128cbc/
    # ascon128/aegis make падает на сборке модулей шифрования — их
    # исходников в архиве нет.
    my @configure = (
        "../src/configure",
        "--prefix=$WXSQLITE3_DIR/install",
        "--with-wx-config=$wx_config",
        "--disable-shared",
        "--enable-static",
        "--without-sqlcipher",
        "--without-aes128cbc",
        "--without-ascon128",
        "--without-aegis",
    );
    run(@configure);

    # Определяем цель сборки ТОЛЬКО библиотеки. `make` без цели
    # собирает и samples/treeview, а treeview требует libwx_gtk3u_xml,
    # которой нет (wxUSE_XML=OFF). Берём LIB_TARGET из Makefile —
    # это ровно то, что делает setup-deps.sh.
    my $makefile = "$WXSQLITE3_DIR/build/Makefile";
    -f $makefile or error "Makefile не найден: $makefile";

    my ($lib_target) = `grep -E '^[[:space:]]*lib.*_LTLIBRARIES' '$makefile' 2>/dev/null`
                        =~ /=\s*(\S+)/;
    chomp $lib_target if defined $lib_target;

    if (!defined $lib_target || $lib_target eq '') {
        # Fallback: имя .la-файла без суффикса (как в bash).
        my @la = glob("$WXSQLITE3_DIR/build/*.la");
        ($lib_target) = map { s{^.*/}{}; s/\.la$//r } @la;
    }
    $lib_target
        or error "Не удалось определить LIB_TARGET в $makefile — make соберёт samples и упадёт";

    info "wxSQLite3 LIB_TARGET: $lib_target";
    run("make", "-j" . nproc(), $lib_target);
    run("make", "install-exec");

    # install-exec не ставит заголовки — копируем вручную, как в bash.
    if (!-d "$WXSQLITE3_DIR/install/include/wx") {
        info "Копирую заголовки wxSQLite3 вручную (install-exec их не ставит)...";
        make_path("$WXSQLITE3_DIR/install/include/wx");
        run("cp", "-r", "$WXSQLITE3_DIR/src/include/wx/.", "$WXSQLITE3_DIR/install/include/wx/");
    }

    chdir $PROJECT_ROOT or error "chdir $PROJECT_ROOT: $!";
    info "wxSQLite3 установлен в $WXSQLITE3_DIR/install";
}

# ------------------------- 4. Сборка проекта -------------------------
sub build_project {
    info "Сборка $PACKAGE...";
    # Используем компиляторы из gcc-mozilla (GCC 7.5) — системный GCC 6.3
    # не поддерживает C++17 inline-переменные.
    my $moz_bin = "/usr/lib/gcc-mozilla/bin";
    if (-x "$moz_bin/gcc" && -x "$moz_bin/g++") {
        $ENV{CC}  = "$moz_bin/gcc";
        $ENV{CXX} = "$moz_bin/g++";
        info "Использую компиляторы gcc-mozilla: CC=$ENV{CC}, CXX=$ENV{CXX}";
    } else {
        warn_msg "gcc-mozilla не найден в $moz_bin — используется системный компилятор. "
               . "Если это GCC 6.3, сборка упадёт на C++17.";
    }

    remove_tree($BUILD_DIR) if -d $BUILD_DIR;
    make_path($BUILD_DIR);

    chdir $BUILD_DIR or error "chdir $BUILD_DIR: $!";

    # Ищем реальный путь к wxWidgetsConfig.cmake. Версии wxWidgets
    # кладут конфиг в разные подкаталоги (wxWidgets/, wxWidgets-3.3/,
    # или прямо в lib/cmake/). Перебираем все варианты и берём первый
    # найденный — иначе -DwxWidgets_DIR указывает в пустоту и
    # find_package(wxWidgets CONFIG) падает.
    my $cmake_cfg_root = "$WX_DIR/install/lib/cmake";
    my ($wx_cfg) = glob("$cmake_cfg_root/wxWidgets*/wxWidgetsConfig.cmake");
    $wx_cfg //= -f "$cmake_cfg_root/wxWidgetsConfig.cmake"
              ? "$cmake_cfg_root/wxWidgetsConfig.cmake"
              : undef;
    $wx_cfg
        or error "wxWidgetsConfig.cmake не найден в $cmake_cfg_root. "
               . "Пересоберите wxWidgets (--rebuild-deps).";
    (my $wx_cfg_dir = $wx_cfg) =~ s{/wxWidgetsConfig\.cmake$}{};
    info "wxWidgets CMake config: $wx_cfg";

    # CMakeLists.txt проекта использует find_package(wxWidgets CONFIG)
    # и жёстко зашитый путь к wxSQLite3 (${CMAKE_SOURCE_DIR}/third_party/
    # wxsqlite3/install), поэтому -DwxSQLite3_ROOT_DIR и
    # -DwxWidgets_ROOT_DIR/_LIB_DIR игнорируются — не передаём их,
    # чтобы не создавать иллюзию настройки.
    my @cmake = (
        "cmake", $PROJECT_ROOT,
        "-DCMAKE_CXX_STANDARD=17",
        "-DCMAKE_CXX_STANDARD_REQUIRED=ON",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_INSTALL_PREFIX=$STAGING/usr",
        "-DwxWidgets_DIR=$wx_cfg_dir",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF",
        "-DIPTVPLAYER_ENABLE_LTO=OFF",
    );
    run(@cmake);

    run("cmake", "--build", ".", "--target", "all", "-j" . nproc());
    run("cmake", "--build", ".", "--target", "install");

    chdir $PROJECT_ROOT or error "chdir $PROJECT_ROOT: $!";
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

    # --only-wxsqlite3: пересобрать ТОЛЬКО wxSQLite3 и выйти.
    # wxWidgets не трогаем, проект не собираем, .deb не пакуем.
    # Подразумевает --rebuild-wxsqlite3 (принудительно).
    if ($only_wxsqlite3) {
        my $wx_config = "$WX_DIR/install/bin/wx-config";
        -x $wx_config
            or error "wxWidgets не собран ($wx_config не найден). "
                   . "Сначала запустите скрипт без --only-wxsqlite3, "
                   . "чтобы собрать wxWidgets.";
        info "Режим: только wxSQLite3 (wxWidgets уже собран: $wx_config)";

        # Минимальный набор инструментов, без которого wxSQLite3 не соберётся.
        # Системные пакеты здесь не ставим: у пользователя уже есть
        # wxWidgets, значит deps установлены. Если чего-то не хватает —
        # внятная ошибка, а не падение в недрах make.
        for my $tool (qw(autoreconf make gcc g++)) {
            system("sh", "-c", "command -v $tool >/dev/null 2>&1") == 0
                or error "Не найден $tool. Установите: "
                       . "sudo apt install build-essential autoconf automake libtool";
        }

        $rebuild_wxsqlite3 = 1;   # принудительная пересборка
        build_wxsqlite3();
        info "Готово. wxSQLite3 пересобран в $WXSQLITE3_DIR/install";
        return;
    }

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