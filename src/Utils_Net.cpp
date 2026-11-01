#include "config.h"

#include "Utils_FsPath.h"
#include "Utils_Net.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/uri.h>

#include <filesystem>
#include <string>

bool IsNetworkUrl(const wxString &url) {
  if (url.IsEmpty())
    return false;

  wxURI uri(url);
  if (!uri.HasScheme())
    return false;

  wxString scheme = uri.GetScheme();
  scheme.MakeLower();
  if (scheme == "file")
    return false;

  return true;
}

// Структура для сбора заголовков
struct HeaderData {
  long long contentLength;
  HeaderData() : contentLength(-1) {}
};

static size_t HeaderCallback(char *buffer, size_t size, size_t nitems,
                             void *userdata) {
  HeaderData *hd = static_cast<HeaderData *>(userdata);
  std::string line(buffer, size * nitems);
  std::string prefix = "Content-Length:";
  if (line.find(prefix) == 0) {
    std::string val = line.substr(prefix.size());
    val.erase(0, val.find_first_not_of(" \t\r\n"));
    val.erase(val.find_last_not_of(" \t\r\n") + 1);
    hd->contentLength = std::stoll(val);
  }
  return size * nitems;
}

void ApplyCurlCaBundle(CURL *handle) {
  if (!handle)
    return;

  if (const char *ca = std::getenv("IPTVPLAYER_CA_BUNDLE"); ca && *ca) {
    curl_easy_setopt(handle, CURLOPT_CAINFO, ca);
    return;
  }

#if defined(_WIN32)
  wxFileName exeFn(wxStandardPaths::Get().GetExecutablePath());
  exeFn.MakeAbsolute();
  std::string exeDirUtf8(exeFn.GetPath().ToUTF8().data());

  std::filesystem::path candidate = ToFsPath(exeDirUtf8) / "ca-bundle.crt";

  std::error_code ec;
  if (std::filesystem::exists(candidate, ec) && !ec) {
    auto u8 = candidate.u8string();
    std::string caPath(reinterpret_cast<const char *>(u8.data()), u8.size());
    curl_easy_setopt(handle, CURLOPT_CAINFO, caPath.c_str());
  }

  curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS,
                   static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
}

UrlAvailabilityResult CheckUrlAvailability(const std::string &url,
                                           const std::string &userAgent,
                                           int timeoutSeconds,
                                           long long maxFileSize) {
  UrlAvailabilityResult result;
  result.available = false;
  result.httpCode = 0;
  result.contentLength = -1;
  result.errorText.clear();

  if (!IsNetworkUrl(wxString::FromUTF8(url))) {
    result.errorText = "Invalid URL";
    return result;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    result.errorText = "CURL init failed";
    return result;
  }

  ApplyCurlCaBundle(curl);

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  if (!userAgent.empty()) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
  }

  HeaderData headerData;
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerData);

  curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "HEAD");

  CURLcode res = curl_easy_perform(curl);
  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  result.httpCode = httpCode;

  if (res != CURLE_OK) {
    result.errorText = "CURL error: " + std::string(curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    return result;
  }

  if (httpCode == 405) {
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_RANGE, "0-0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     [](char *, size_t size, size_t nmemb, void *) -> size_t {
                       return size * nmemb;
                     });
    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    result.httpCode = httpCode;
    if (res != CURLE_OK) {
      result.errorText =
          "CURL error on GET Range: " + std::string(curl_easy_strerror(res));
      curl_easy_cleanup(curl);
      return result;
    }
    if (httpCode != 200 && httpCode != 206) {
      result.errorText = "HTTP " + std::to_string(httpCode);
      curl_easy_cleanup(curl);
      return result;
    }
  } else if (httpCode != 200) {
    result.errorText = "HTTP " + std::to_string(httpCode);
    curl_easy_cleanup(curl);
    return result;
  }

  result.available = true;
  result.contentLength = headerData.contentLength;

  if (maxFileSize > 0 && result.contentLength > maxFileSize) {
    result.available = false;
    result.errorText = "File too large (" +
                       std::to_string(result.contentLength) + " bytes, limit " +
                       std::to_string(maxFileSize) + ")";
  }

  curl_easy_cleanup(curl);
  return result;
}

// ============================================================================
// URL helpers
// ============================================================================
wxString UrlEncode(const wxString &s) {
  static const char hex[] = "0123456789ABCDEF";
  wxString out;
  const wxScopedCharBuffer buf = s.utf8_str();
  const size_t len = buf.length();
  out.reserve(len * 3);
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(buf.data()[i]);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

wxString UrlDecode(const wxString &s) {
  const wxScopedCharBuffer buf = s.utf8_str();
  const size_t len = buf.length();
  std::string out;
  out.reserve(len);
  auto hexVal = [](char ch) -> int {
    if (ch >= '0' && ch <= '9')
      return ch - '0';
    if (ch >= 'a' && ch <= 'f')
      return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
      return ch - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < len; ++i) {
    const char c = buf.data()[i];
    if (c == '%' && i + 2 < len) {
      const int h = hexVal(buf.data()[i + 1]);
      const int l = hexVal(buf.data()[i + 2]);
      if (h >= 0 && l >= 0) {
        out += static_cast<char>((h << 4) | l);
        i += 2;
        continue;
      }
    }
    out += c;
  }
  return wxString::FromUTF8(out.data(), out.size());
}

bool IsHttpUrl(const wxString &url) {
  if (url.IsEmpty())
    return false;
  wxURI uri(url);
  if (!uri.HasScheme())
    return false;
  const wxString scheme = uri.GetScheme().Lower();
  return scheme == "http" || scheme == "https";
}

bool HasNonTrivialPath(const wxString &url) {
  if (url.IsEmpty())
    return false;
  wxURI uri(url);
  const wxString path = uri.GetPath();
  return !path.IsEmpty() && path != "/";
}

wxString GetOrigin(const wxString &url) {
  if (url.IsEmpty())
    return wxString();
  wxURI uri(url);
  if (!uri.HasScheme() || !uri.HasServer())
    return wxString();
  wxString origin = uri.GetScheme().Lower() + "://";
  origin += uri.GetServer();
  if (uri.HasPort()) {
    origin += ":" + uri.GetPort();
  }
  return origin;
}

UrlCredentials ExtractQueryCredentials(const wxString &url) {
  UrlCredentials result;
  if (url.IsEmpty())
    return result;
  wxURI uri(url);
  if (!uri.HasScheme())
    return result;
  result.valid = true;
  if (!uri.HasQuery())
    return result;
  const wxString query = uri.GetQuery();
  size_t pos = 0;
  while (pos < query.length()) {
    const size_t amp = query.find('&', pos);
    const wxString pair =
        (amp == wxString::npos) ? query.Mid(pos) : query.Mid(pos, amp - pos);
    const size_t eq = pair.find('=');
    if (eq != wxString::npos) {
      const wxString key = pair.Left(eq).Lower();
      const wxString value = pair.Mid(eq + 1);
      if (key == "username")
        result.user = UrlDecode(value);
      else if (key == "password")
        result.pass = UrlDecode(value);
    }
    if (amp == wxString::npos)
      break;
    pos = amp + 1;
  }
  return result;
}
