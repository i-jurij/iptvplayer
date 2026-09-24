#include "Application.h"
#include "version.h"

#include <iostream>
#include <string>

// wxIMPLEMENT_APP создаёт и main(), и точку входа wxApp. Нам нужен только
// второй — main() пишем сами, чтобы обработать --version/--help до
// инициализации GTK. В CI без Xvfb / в минимальном контейнере это работает,
// а wxIMPLEMENT_APP — нет.
wxIMPLEMENT_APP_NO_MAIN(Application);

// version.h генерируется CMake. Имя макроса с версией может отличаться
// в зависимости от того, как настроен configure_file. Подставим первое,
// что найдётся, чтобы код собрался в любом случае.
#if defined(IPTVPLAYER_VERSION)
#define IPTV_VERSION_STR IPTVPLAYER_VERSION
#elif defined(VERSION_FULL)
#define IPTV_VERSION_STR VERSION_FULL
#elif defined(VERSION)
#define IPTV_VERSION_STR VERSION
#else
#define IPTV_VERSION_STR "unknown"
#endif

static void print_version() {
  std::cout << "iptvplayer " << IPTV_VERSION_STR << "\n";
}

static void print_help() {
  std::cout << "iptvplayer " << IPTV_VERSION_STR
            << "\n"
               "\n"
               "Usage: iptvplayer [OPTIONS]\n"
               "\n"
               "Options:\n"
               "  -h, --help           Show this help and exit\n"
               "  -V, --version        Show version and exit\n"
               "\n"
               "Environment:\n"
               "  IPTVPLAYER_CA_BUNDLE Path to a PEM file with trusted CA\n"
               "                       certificates. Used by libcurl via\n"
               "                       CURLOPT_CAINFO. Linux/BSD only; on\n"
               "                       Windows/macOS libcurl uses the system\n"
               "                       store and this is ignored.\n"
               "                       Inside *-sharun.AppImage it is set\n"
               "                       automatically by bin/ca-bundle.hook.\n"
               "\n"
               "Without options, the application starts normally.\n";
}

int main(int argc, char **argv) {
  // Ранний выход: не требует дисплея, не трогает wxWidgets.
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--version" || a == "-V") {
      print_version();
      return 0;
    }
    if (a == "--help" || a == "-h") {
      print_help();
      return 0;
    }
  }
  return wxEntry(argc, argv);
}
