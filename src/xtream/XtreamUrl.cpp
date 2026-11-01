#include "xtream/XtreamUrl.h"

#include "Utils_Net.h"

#include <curl/curl.h>
#include <rapidjson/document.h>

#include <thread>

#include <wx/app.h>
#include <wx/weakref.h>

namespace {

struct FetchResult {
  bool ok = false;
  long httpCode = 0;
  std::string body;
  std::string error;
};

struct WriteBuf {
  std::string data;
  size_t maxBytes = 0;
};

size_t WriteCb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  WriteBuf *buf = static_cast<WriteBuf *>(userdata);
  const size_t total = size * nmemb;
  const size_t remain = buf->maxBytes - buf->data.size();
  const size_t take = (total < remain) ? total : remain;
  buf->data.append(ptr, take);
  if (buf->data.size() >= buf->maxBytes)
    return 0; // обрываем загрузку — curl вернёт CURLE_WRITE_ERROR
  return total;
}

// GET первых maxBytes байт. Обрыв по maxBytes — не ошибка, если httpCode != 0.
FetchResult FetchPrefix(const wxString &url, size_t maxBytes, int timeoutSec) {
  FetchResult result;

  CURL *curl = curl_easy_init();
  if (!curl) {
    result.error = "CURL init failed";
    return result;
  }

  ApplyCurlCaBundle(curl);

  const std::string urlUtf8(url.utf8_str().data());

  WriteBuf buf;
  buf.maxBytes = maxBytes;

  curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  const CURLcode res = curl_easy_perform(curl);

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  curl_easy_cleanup(curl);

  result.httpCode = httpCode;
  result.body = std::move(buf.data);

  if (res == CURLE_OK || (res == CURLE_WRITE_ERROR && httpCode != 0)) {
    result.ok = true;
    return result;
  }

  result.error = curl_easy_strerror(res);
  return result;
}

// true если #EXTM3U встречается в первых 200 байтах после BOM/whitespace.
bool LooksLikeM3u(const std::string &body) {
  size_t start = 0;
  if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
      static_cast<unsigned char>(body[1]) == 0xBB &&
      static_cast<unsigned char>(body[2]) == 0xBF) {
    start = 3;
  }
  while (start < body.size()) {
    const char c = body[start];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      ++start;
    else
      break;
  }
  if (start >= body.size())
    return false;
  const size_t limit = (body.size() < start + 200) ? body.size() : start + 200;
  const size_t pos = body.find("#EXTM3U", start);
  return pos != std::string::npos && pos < limit;
}

// Проверка player_api.php JSON: user_info.auth должен быть truthy.
bool CheckAuthJson(const std::string &body, std::string &error) {
  rapidjson::Document doc;
  doc.Parse(body.c_str());
  if (doc.HasParseError() || !doc.IsObject()) {
    error = "player_api.php did not return valid JSON";
    return false;
  }
  if (!doc.HasMember("user_info") || !doc["user_info"].IsObject()) {
    error = "player_api.php response missing user_info";
    return false;
  }
  const auto &ui = doc["user_info"];
  bool auth = false;
  if (ui.HasMember("auth")) {
    const auto &a = ui["auth"];
    if (a.IsInt() && a.GetInt() != 0)
      auth = true;
    else if (a.IsBool() && a.GetBool())
      auth = true;
    else if (a.IsString()) {
      const std::string s(a.GetString());
      if (s == "1" || s == "true")
        auth = true;
    }
  }
  if (!auth) {
    error = "Invalid username or password";
    return false;
  }
  return true;
}

struct ProbeResult {
  bool ok = false;
  wxString finalUrl;
  wxString error;
};

ProbeResult DoProbe(const wxString &baseUrl, const wxString &user,
                    const wxString &pass) {
  ProbeResult result;

  // Этап 1: проверка credentials через player_api.php
  const wxString apiUrl = baseUrl +
                          "/player_api.php?username=" + UrlEncode(user) +
                          "&password=" + UrlEncode(pass);
  FetchResult fr = FetchPrefix(apiUrl, 4096, 8);

  if (!fr.ok) {
    result.error = wxString::FromUTF8("Connection failed: " + fr.error);
    return result;
  }
  if (fr.httpCode == 401 || fr.httpCode == 403) {
    result.error = "Invalid username or password";
    return result;
  }
  if (fr.httpCode != 200) {
    result.error = wxString::Format("HTTP %ld on player_api.php", fr.httpCode);
    return result;
  }

  std::string authErr;
  if (!CheckAuthJson(fr.body, authErr)) {
    result.error = wxString::FromUTF8(authErr);
    return result;
  }

  // Этап 2: пробуем форматы последовательно
  for (xtream::Format fmt :
       {xtream::Format::M3uPlusTs, xtream::Format::M3uPlusHls}) {
    const wxString m3uUrl = xtream::BuildM3u(baseUrl, user, pass, fmt);
    FetchResult mr = FetchPrefix(m3uUrl, 4096, 8);
    if (mr.ok && mr.httpCode == 200 && LooksLikeM3u(mr.body)) {
      result.ok = true;
      result.finalUrl = m3uUrl;
      return result;
    }
  }

  result.error = "Server did not return a valid M3U playlist";
  return result;
}

} // namespace

namespace xtream {

wxString BuildM3u(const wxString &base, const wxString &user,
                  const wxString &pass, Format format) {
  const wxString suffix = (format == Format::M3uPlusHls)
                              ? "type=m3u_plus&output=m3u8"
                              : "type=m3u_plus&output=ts";
  return base + "/get.php?username=" + UrlEncode(user) +
         "&password=" + UrlEncode(pass) + "&" + suffix;
}

wxString BuildXmltv(const wxString &base, const wxString &user,
                    const wxString &pass) {
  return base + "/xmltv.php?username=" + UrlEncode(user) +
         "&password=" + UrlEncode(pass);
}

void ProbeAsync(const wxString &baseUrl, const wxString &user,
                const wxString &pass, ProbeCallback callback) {
  std::thread([baseUrl, user, pass, callback]() {
    ProbeResult r = DoProbe(baseUrl, user, pass);
    wxWeakRef<wxAppConsole> weakApp(wxTheApp);
    wxAppConsole *app = weakApp.get();
    if (!app)
      return;
    app->CallAfter([callback, r]() {
      if (callback)
        callback(r.ok, r.finalUrl, r.error);
    });
  }).detach();
}

} // namespace xtream
