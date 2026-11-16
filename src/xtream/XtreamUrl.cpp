#include "xtream/XtreamUrl.h"
#include "LogControl.h"
#include "Utils_Net.h"
#include "Utils_Platform.h"
#include "version.h"
#include "xtream/PlaylistDetector.h"

#include <curl/curl.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <thread>

#include <wx/app.h>
#include <wx/uri.h>
#include <wx/weakref.h>

// ---------------------------------------------------------------------------
// Version string (same fallback chain as main.cpp / MainFrame.cpp)
// ---------------------------------------------------------------------------
#if defined(IPTVPLAYER_VERSION_FULL)
#define IPTV_VERSION_STR IPTVPLAYER_VERSION_FULL
#elif defined(IPTVPLAYER_VERSION)
#define IPTV_VERSION_STR IPTVPLAYER_VERSION
#elif defined(VERSION_FULL)
#define IPTV_VERSION_STR VERSION_FULL
#elif defined(VERSION)
#define IPTV_VERSION_STR VERSION
#else
#define IPTV_VERSION_STR "unknown"
#endif

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
    return 0; // aborts transfer; curl returns CURLE_WRITE_ERROR
  return total;
}

// GET first maxBytes bytes. Abort by maxBytes is not an error if httpCode != 0.
// Empty userAgent → DefaultUserAgent().
FetchResult FetchPrefix(const wxString &url, size_t maxBytes, int timeoutSec,
                        const wxString &userAgent) {
  FetchResult result;

  CURL *curl = curl_easy_init();
  if (!curl) {
    result.error = "CURL init failed";
    return result;
  }

  ApplyCurlCaBundle(curl);

  const std::string urlUtf8(url.utf8_str().data());
  const wxString ua =
      userAgent.IsEmpty() ? xtream::DefaultUserAgent() : userAgent;
  const std::string uaUtf8(ua.utf8_str().data());

  WriteBuf buf;
  buf.maxBytes = maxBytes;

  curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, uaUtf8.c_str());
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

// true if #EXTM3U appears in first 200 bytes (after BOM/whitespace).
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

// Serialize a rapidjson Value to std::string.
void SerializeValue(const rapidjson::Value &v, std::string &out) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  v.Accept(w);
  out.assign(buf.GetString(), buf.GetSize());
}

// Validate player_api.php JSON: user_info.auth must be truthy.
// Optionally extracts raw JSON of user_info / server_info.
bool CheckAuthJson(const std::string &body, std::string &error,
                   std::string *userInfoOut, std::string *serverInfoOut) {
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

  if (userInfoOut)
    SerializeValue(ui, *userInfoOut);
  if (serverInfoOut && doc.HasMember("server_info") &&
      doc["server_info"].IsObject())
    SerializeValue(doc["server_info"], *serverInfoOut);

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
    std::string msg;
    if (ui.HasMember("message") && ui["message"].IsString())
      msg = ui["message"].GetString();
    if (msg.empty() && ui.HasMember("status") && ui["status"].IsString())
      msg = ui["status"].GetString();
    error = msg.empty() ? "Invalid username or password" : msg;
    return false;
  }
  return true;
}

struct AttemptResult {
  bool ok = false;
  bool retryHttps = false;
  wxString finalUrl;
  std::string error;
  std::string userInfoJson;
  std::string serverInfoJson;
};

// One attempt at the given base URL (either http:// or https://).
AttemptResult TryProbe(const wxString &base, const wxString &user,
                       const wxString &pass, const wxString &ua) {
  AttemptResult r;

  // Stage 1: auth via player_api.php (16 KiB limit — some panels return
  // a large server_info payload).
  const wxString apiUrl = base + "/player_api.php?username=" + UrlEncode(user) +
                          "&password=" + UrlEncode(pass);
  FetchResult fr = FetchPrefix(apiUrl, 16 * 1024, 8, ua);
  LOG_DEBUG("Xtream probe: player_api.php HTTP %ld, %zu bytes", fr.httpCode,
            fr.body.size());

  if (!fr.ok) {
    r.error = std::string("Connection failed: ") + fr.error;
    r.retryHttps = true; // could be https-only server
    LOG_WARN("Xtream probe: player_api.php connection failed: %s",
             fr.error.c_str());
    return r;
  }
  if (fr.httpCode == 401 || fr.httpCode == 403) {
    r.error = "Invalid username or password";
    LOG_WARN("Xtream probe: HTTP %ld (auth rejected)", fr.httpCode);
    return r;
  }
  if (fr.httpCode != 200) {
    r.error = "HTTP " + std::to_string(fr.httpCode) + " on player_api.php";
    if (fr.httpCode >= 500)
      r.retryHttps = true;
    LOG_WARN("Xtream probe: %s", r.error.c_str());
    return r;
  }

  std::string authErr;
  if (!CheckAuthJson(fr.body, authErr, &r.userInfoJson, &r.serverInfoJson)) {
    r.error = authErr;
    LOG_WARN("Xtream auth failed: %s", authErr.c_str());
    return r;
  }

  // Stage 2: try M3U formats sequentially
  for (xtream::Format fmt :
       {xtream::Format::M3uPlusTs, xtream::Format::M3uPlusHls}) {
    const wxString m3uUrl = xtream::BuildM3u(base, user, pass, fmt);
    FetchResult mr = FetchPrefix(m3uUrl, 4096, 8, ua);
    const char *fmtName =
        (fmt == xtream::Format::M3uPlusTs) ? "m3u_plus/ts" : "m3u_plus/hls";
    LOG_DEBUG("Xtream probe: %s HTTP %ld", fmtName, mr.httpCode);
    if (mr.ok && mr.httpCode == 200 && LooksLikeM3u(mr.body)) {
      r.ok = true;
      r.finalUrl = m3uUrl;
      return r;
    }
  }

  r.error = "Server did not return a valid M3U playlist";
  LOG_WARN("Xtream probe: %s", r.error.c_str());
  return r;
}

wxString MakeHttpsUrl(const wxString &url) {
  if (url.Lower().StartsWith("http://"))
    return "https://" + url.Mid(7);
  return url;
}

struct ProbeResult {
  bool ok = false;
  wxString finalUrl;
  wxString error;
  std::string userInfoJson;
  std::string serverInfoJson;
};

ProbeResult DoProbe(const wxString &baseUrl, const wxString &user,
                    const wxString &pass, const wxString &ua) {
  ProbeResult result;

  LOG_DEBUG("Xtream probe: base='%s'", baseUrl.utf8_str().data());

  AttemptResult first = TryProbe(baseUrl, user, pass, ua);
  if (first.ok) {
    result.ok = true;
    result.finalUrl = first.finalUrl;
    result.userInfoJson = std::move(first.userInfoJson);
    result.serverInfoJson = std::move(first.serverInfoJson);
    LOG_INFO("Xtream probe OK: %s", result.finalUrl.utf8_str().data());
    return result;
  }

  // HTTPS fallback (only when we started with http:// and the failure
  // looks like "http not listening" or 5xx).
  const wxURI uri(baseUrl);
  const bool wasHttp = uri.HasScheme() && uri.GetScheme().Lower() == "http";
  if (wasHttp && first.retryHttps) {
    const wxString httpsUrl = MakeHttpsUrl(baseUrl);
    LOG_INFO("Xtream: retry with https (%s)", httpsUrl.utf8_str().data());

    AttemptResult second = TryProbe(httpsUrl, user, pass, ua);
    if (second.ok) {
      result.ok = true;
      result.finalUrl = second.finalUrl;
      result.userInfoJson = std::move(second.userInfoJson);
      result.serverInfoJson = std::move(second.serverInfoJson);
      LOG_INFO("Xtream probe OK (https): %s",
               result.finalUrl.utf8_str().data());
      return result;
    }
    result.error = wxString::FromUTF8(second.error);
    LOG_WARN("Xtream probe (https) failed: %s", second.error.c_str());
    return result;
  }

  result.error = wxString::FromUTF8(first.error);
  LOG_WARN("Xtream probe failed: %s", first.error.c_str());
  return result;
}

struct ApiProbeResult {
  bool ok = false;
  wxString finalBase;
  wxString error;
  std::string userInfoJson;
  std::string serverInfoJson;
};

// player_api.php auth-only probe (no get.php). Mirrors TryProbe's Stage 1
// with the same https fallback logic.
ApiProbeResult DoProbeApi(const wxString &baseUrl, const wxString &user,
                          const wxString &pass, const wxString &ua) {
  ApiProbeResult result;
  LOG_DEBUG("Xtream API probe: base='%s'", baseUrl.utf8_str().data());

  auto tryOne = [&](const wxString &base) -> AttemptResult {
    AttemptResult r;
    const wxString apiUrl = base + "/player_api.php?username=" +
                            UrlEncode(user) + "&password=" + UrlEncode(pass);
    FetchResult fr = FetchPrefix(apiUrl, 16 * 1024, 8, ua);
    LOG_DEBUG("Xtream API probe: player_api.php HTTP %ld, %zu bytes",
              fr.httpCode, fr.body.size());
    if (!fr.ok) {
      r.error = std::string("Connection failed: ") + fr.error;
      r.retryHttps = true;
      return r;
    }
    if (fr.httpCode == 401 || fr.httpCode == 403) {
      r.error = "Invalid username or password";
      return r;
    }
    if (fr.httpCode != 200) {
      r.error = "HTTP " + std::to_string(fr.httpCode) + " on player_api.php";
      if (fr.httpCode >= 500)
        r.retryHttps = true;
      return r;
    }
    std::string authErr;
    if (!CheckAuthJson(fr.body, authErr, &r.userInfoJson, &r.serverInfoJson)) {
      r.error = authErr;
      return r;
    }
    r.ok = true;
    r.finalUrl = base;
    return r;
  };

  AttemptResult first = tryOne(baseUrl);
  if (first.ok) {
    result.ok = true;
    result.finalBase = baseUrl;
    result.userInfoJson = std::move(first.userInfoJson);
    result.serverInfoJson = std::move(first.serverInfoJson);
    return result;
  }

  const wxURI uri(baseUrl);
  const bool wasHttp = uri.HasScheme() && uri.GetScheme().Lower() == "http";
  if (wasHttp && first.retryHttps) {
    const wxString httpsUrl = MakeHttpsUrl(baseUrl);
    LOG_INFO("Xtream: API probe retry with https (%s)",
             httpsUrl.utf8_str().data());
    AttemptResult second = tryOne(httpsUrl);
    if (second.ok) {
      result.ok = true;
      result.finalBase = httpsUrl;
      result.userInfoJson = std::move(second.userInfoJson);
      result.serverInfoJson = std::move(second.serverInfoJson);
      return result;
    }
    result.error = wxString::FromUTF8(second.error);
    return result;
  }

  result.error = wxString::FromUTF8(first.error);
  return result;
}

// Unused after 10.2 (Xtream add-flow goes via player_api.php). Kept for
// potential Q4 fallback (M3U when all three player_api endpoints fail).
[[maybe_unused]] AttemptResult TryGetPhpOnly(const wxString &base, const wxString &user,
                            const wxString &pass, const wxString &ua) {
  AttemptResult r;
  for (xtream::Format fmt :
       {xtream::Format::M3uPlusTs, xtream::Format::M3uPlusHls}) {
    const wxString m3uUrl = xtream::BuildM3u(base, user, pass, fmt);
    FetchResult mr = FetchPrefix(m3uUrl, 4096, 8, ua);
    const char *fmtName =
        (fmt == xtream::Format::M3uPlusTs) ? "m3u_plus/ts" : "m3u_plus/hls";
    LOG_DEBUG("Xtream dial-probe: %s HTTP %ld", fmtName, mr.httpCode);
    if (mr.ok && mr.httpCode == 200 && LooksLikeM3u(mr.body)) {
      r.ok = true;
      r.finalUrl = m3uUrl;
      return r;
    }
  }
  r.error = "Server did not return a valid M3U playlist";
  return r;
}
} // namespace

namespace xtream {

wxString DefaultUserAgent() {
  return wxString::FromUTF8("mpv/") + wxString::FromUTF8(IPTV_VERSION_STR);
}

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
                const wxString &pass, const wxString &ua,
                ProbeCallback callback) {
  std::thread([baseUrl, user, pass, ua, callback]() {
    ProbeResult r = DoProbe(baseUrl, user, pass, ua);
    wxWeakRef<wxAppConsole> weakApp(wxTheApp);
    wxAppConsole *app = weakApp.get();
    if (!app)
      return;
    app->CallAfter([callback, r]() {
      if (callback)
        callback(r.ok, r.finalUrl, r.error, r.userInfoJson, r.serverInfoJson);
    });
  }).detach();
}

void ProbeApiAsync(const wxString &baseUrl, const wxString &user,
                   const wxString &pass, const wxString &ua,
                   ApiProbeCallback callback) {
  const wxString uaEff = ua.IsEmpty() ? DefaultUserAgent() : ua;
  std::thread([baseUrl, user, pass, uaEff, callback]() {
    ApiProbeResult r = DoProbeApi(baseUrl, user, pass, uaEff);
    wxWeakRef<wxAppConsole> weakApp(wxTheApp);
    wxAppConsole *app = weakApp.get();
    if (!app)
      return;
    app->CallAfter([callback, r]() {
      if (callback)
        callback(r.ok, r.finalBase, r.error, r.userInfoJson,
                 r.serverInfoJson);
    });
  }).detach();
}

void DialProbeAsync(const wxString &inputUrl, const wxString &user,
                    const wxString &pass, const wxString &ua,
                    DialProbeCallback callback) {
  const wxString uaEff = ua.IsEmpty() ? DefaultUserAgent() : ua;

  DetectContentAsync(
      inputUrl, uaEff,
      [inputUrl, user, pass, uaEff, callback](const Detection &det) {
        switch (det.kind) {
        case DetectionKind::XtreamAuth: {
          const wxString base = StripPhpSuffix(inputUrl);
          // Sniffing already confirmed player_api.php + auth. Channels
          // will be fetched by lazy refresh (RefreshLiveCatalog) via
          // player_api, not get.php. Pass base as finalUrl; meta carries
          // user_info / server_info.
          auto deliver = [det, callback, base]() {
            callback(true, base, "", det.userInfoJson, det.serverInfoJson,
                     /*isM3u=*/false);
          };
          if (wxTheApp)
            wxTheApp->CallAfter(deliver);
          else
            deliver();
          return;
        }

        case DetectionKind::M3U: {
          // M3U по содержимому. Если в query есть creds — это может быть
          // get.php URL: extract credentials, try player_api.php first.
          // If the API works — Xtream playlist (isM3u=false); if not —
          // fall back to M3U playlist (isM3u=true) using the original URL.
          const wxString wxUrl =
              det.finalUrl.IsEmpty() ? inputUrl : det.finalUrl;
          const UrlCredentials creds = ExtractQueryCredentials(wxUrl);
          if (creds.valid && !creds.user.IsEmpty() && !creds.pass.IsEmpty()) {
            const wxString base = StripPhpSuffix(wxUrl);
            ProbeApiAsync(base, creds.user, creds.pass, uaEff,
                          [callback, wxUrl](bool ok,
                                            const wxString &finalBase,
                                            const wxString & /*error*/,
                                            const std::string &ui,
                                            const std::string &si) {
                            if (ok)
                              callback(true, finalBase, "", ui, si, false);
                            else
                              callback(true, wxUrl, "", "", "", true);
                          });
            return;
          }
          callback(true, wxUrl, "", "", "", true);
          return;
        }

        case DetectionKind::JsonOther:
          callback(false, "",
                   _U("Server returned JSON, but not a Xtream auth "
                      "endpoint (no user_info)."),
                   "", "", false);
          return;

        case DetectionKind::Html:
          callback(false, "",
                   _U("Server returned an HTML page, not a playlist."), "", "",
                   false);
          return;

        case DetectionKind::BinaryStream:
          callback(false, "",
                   _U("This URL points to a media stream, not a playlist. "
                      "Use the server address (http://host:port) instead."),
                   "", "", false);
          return;

        case DetectionKind::Empty:
        case DetectionKind::HttpError:
        case DetectionKind::NetworkError:
        case DetectionKind::Unknown:
        default: {
          // Bare server может отдавать 404/empty на "/". Try player_api
          // first; if that fails, full probe (auth + get.php) as M3U
          // fallback for non-Xtream servers that answer get.php only.
          const wxString base = StripPhpSuffix(inputUrl);
          ProbeApiAsync(base, user, pass, uaEff,
                        [callback, base, user, pass, uaEff](
                            bool ok, const wxString &finalBase,
                            const wxString & /*error*/,
                            const std::string &userInfoJson,
                            const std::string &serverInfoJson) {
                          if (ok) {
                            callback(true, finalBase, "", userInfoJson,
                                     serverInfoJson, /*isM3u=*/false);
                            return;
                          }
                          ProbeAsync(
                              base, user, pass, uaEff,
                              [callback](bool ok2, const wxString &finalUrl,
                                         const wxString &err2,
                                         const std::string &ui2,
                                         const std::string &si2) {
                                if (ok2)
                                  callback(true, finalUrl, "", ui2, si2,
                                           /*isM3u=*/true);
                                else
                                  callback(false, "", err2, "", "", false);
                              });
                        });
          return;
        }
        }
      });
}

wxString StripPhpSuffix(const wxString &url) {
  if (url.IsEmpty())
    return url;

  wxURI uri(url);
  if (!uri.HasScheme() || !uri.HasServer())
    return url;

  wxString origin = uri.GetScheme().Lower() + "://" + uri.GetServer();
  if (uri.HasPort())
    origin += ":" + uri.GetPort();

  wxString path = uri.GetPath();
  while (!path.IsEmpty() && path.EndsWith("/"))
    path.RemoveLast();

  if (!path.IsEmpty()) {
    const size_t lastSlash = path.rfind('/');
    const wxString lastSegment =
        (lastSlash == wxString::npos) ? path : path.Mid(lastSlash + 1);
    if (lastSegment.Lower().EndsWith(".php")) {
      if (lastSlash == wxString::npos || lastSlash == 0) {
        path.clear();
      } else {
        path = path.Left(lastSlash);
      }
    }
  }

  return origin + path;
}

} // namespace xtream
