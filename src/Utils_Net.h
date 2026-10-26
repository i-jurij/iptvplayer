#pragma once

#include <curl/curl.h>

#include <string>

#include <wx/string.h>

// Все платформы: IPTVPLAYER_CA_BUNDLE → CURLOPT_CAINFO (если задан).
// Windows: ca-bundle.crt рядом с exe + CURLSSLOPT_NATIVE_CA.
// Linux/BSD/macOS: no-op, libcurl использует системный store.
void ApplyCurlCaBundle(CURL *handle);

struct UrlAvailabilityResult {
  bool available;          // true, если код 200
  long httpCode;           // HTTP-статус или 0 при ошибке curl
  long long contentLength; // размер в байтах, -1 если неизвестен
  std::string errorText;   // описание ошибки
};

/// Проверяет доступность URL (HEAD-запрос с таймаутом).
UrlAvailabilityResult
CheckUrlAvailability(const std::string &url, const std::string &userAgent = "",
                     int timeoutSeconds = 5,
                     long long maxFileSize = 250 * 1024 * 1024);

bool IsNetworkUrl(const wxString &url);
inline bool IsNetworkUrl(const std::string &url) {
  return IsNetworkUrl(wxString::FromUTF8(url));
}
