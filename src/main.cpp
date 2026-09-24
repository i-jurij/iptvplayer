#include "Application.h"
#include "version.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/stat.h>

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
               "Without options, the application starts normally.\n";
}

// ---------------------------------------------------------------------------
// Выбор системного CA-bundle по первому существующему пути.
//
// Нужно потому, что в AppImage (sharun) бандлится libcurl, собранный на Arch
// с захардкоженным путём /etc/ssl/certs/ca-certificates.crt. В Fedora 44
// этого файла нет (там /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem),
// и libcurl не может проверить SSL-сертификат.
//
// Приоритет путей — от самого распространённого к более редкому.
// ---------------------------------------------------------------------------
static const char *pick_ca_bundle() {
  static const char *candidates[] = {
      // Debian / Ubuntu / Arch / большинство дистрибутивов
      "/etc/ssl/certs/ca-certificates.crt",
      // Fedora / RHEL 9+ / CentOS Stream 9+
      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
      // RHEL 8 / CentOS 8 / старые Fedora
      "/etc/pki/tls/cert.pem",
      // Дополнительный путь RHEL-семейства
      "/etc/pki/tls/cacert.pem",
      // Alpine Linux
      "/etc/ssl/cert.pem",
      // openSUSE / SUSE Linux Enterprise
      "/var/lib/ca-certificates/ca-bundle.pem", nullptr};

  struct stat st;
  for (int i = 0; candidates[i]; ++i) {
    if (stat(candidates[i], &st) == 0)
      return candidates[i];
  }
  return nullptr;
}

// Кладём найденный путь в environ текущего процесса. Это видят libcurl
// (CURL_CA_BUNDLE переопределяет --with-ca-bundle), OpenSSL (SSL_CERT_FILE)
// и любые вложенные потребители (REQUESTS_CA_BUNDLE — на случай Python-
// зависимостей).
//
// setenv() работает внутри процесса, поэтому не зависит от того, пробрасывает
// ли sharun переменные окружения хоста в песочницу.
static void setup_ca_bundle_env() {
  const char *ca = pick_ca_bundle();
  if (!ca) {
    std::cerr << "[ca] не найден ни один системный CA-bundle" << std::endl;
    return;
  }
  setenv("CURL_CA_BUNDLE", ca, 1);
  setenv("SSL_CERT_FILE", ca, 1);
  setenv("REQUESTS_CA_BUNDLE", ca, 1);
  std::cout << "[ca] using CA bundle: " << ca << std::endl;
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

  // До wxEntry и до первого CurlGlobal::instance()/curl_easy_init().
  setup_ca_bundle_env();

  return wxEntry(argc, argv);
}
