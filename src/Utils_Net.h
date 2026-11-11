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

// ============================================================================
// URL helpers (RFC 3986)
// ============================================================================

// Percent-encoding (UTF-8). Unreserved A-Z a-z 0-9 - _ . ~ → как есть,
// остальные байты → %XX. Для form-encoding (пробел → '+') НЕ подходит.
wxString UrlEncode(const wxString &s);

// Percent-decoding. Невалидные %XX оставляет как есть.
// '+' не трогает (это не form-encoding).
wxString UrlDecode(const wxString &s);

// scheme ∈ {http, https}, case-insensitive. URL без схемы → false.
bool IsHttpUrl(const wxString &url);
// Полная валидация server URL: схема ∈ {http, https}, хост не пуст,
// порт (если указан) ∈ [1, 65535]. Используется для server_info.url
// и ручного ввода.
bool IsValidServerUrl(const wxString &url);

// true если wxURI::GetPath() не пуст и не "/".
// Т.е. пользователь дал endpoint, а не просто host:port.
bool HasNonTrivialPath(const wxString &url);

// scheme://host[:port], без path/query. URL без схемы → пустая строка.
wxString GetOrigin(const wxString &url);

// scheme://host[:port][/parent-path]
// Отсекает последний сегмент path (get.php → /, /iptv/get.php → /iptv).
wxString GetOriginWithParentPath(const wxString &url);

struct UrlCredentials {
  wxString user;
  wxString pass;
  bool valid = false; // true если URL разобран wxURI (схема есть)
};

// Извлекает username/password из query. URL без схемы → valid=false.
// Отсутствие username/password в query → valid=true, user/pass пустые.
UrlCredentials ExtractQueryCredentials(const wxString &url);

// Возвращает URL с замаскированными username/password в query.
// Остальное (scheme, host, port, path, другие query-параметры) не меняется.
// Для URL без схемы возвращает вход как есть.
wxString SanitizeUrlForLog(const wxString &url);
inline std::string SanitizeUrlForLog(const std::string &url) {
  return std::string(
      SanitizeUrlForLog(wxString::FromUTF8(url)).ToUTF8().data());
}
