#include "xtream/PlaylistDetector.h"

#include "Utils_Net.h"

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <curl/curl.h>

#include <wx/app.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <thread>

namespace xtream {

namespace {

constexpr size_t kMaxPrefixBytes = 16 * 1024;

struct DetectBuffer {
  std::string data;
  DetectBuffer() { data.reserve(kMaxPrefixBytes); }
};

size_t WriteCb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *buf = static_cast<DetectBuffer *>(userdata);
  const size_t total = size * nmemb;
  const size_t remain = kMaxPrefixBytes - buf->data.size();
  const size_t take = (total < remain) ? total : remain;
  buf->data.append(ptr, take);
  if (buf->data.size() >= kMaxPrefixBytes)
    return 0; // abort transfer; curl returns CURLE_WRITE_ERROR
  return total;
}

std::string ToLowerAscii(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool LooksLikeM3u(const std::string &prefix) {
  size_t start = 0;
  if (prefix.size() >= 3 && static_cast<unsigned char>(prefix[0]) == 0xEF &&
      static_cast<unsigned char>(prefix[1]) == 0xBB &&
      static_cast<unsigned char>(prefix[2]) == 0xBF) {
    start = 3;
  }
  while (start < prefix.size()) {
    const char c = prefix[start];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      ++start;
    else
      break;
  }
  if (start >= prefix.size())
    return false;
  const size_t limit =
      (prefix.size() < start + 200) ? prefix.size() : start + 200;
  const size_t pos = prefix.find("#EXTM3U", start);
  return pos != std::string::npos && pos < limit;
}

// Возвращает true, если response — HTML.
bool LooksLikeHtml(const std::string &prefix, const std::string &contentType) {
  const std::string ctLower = ToLowerAscii(contentType);
  if (ctLower.find("html") != std::string::npos)
    return true;

  const size_t n = std::min<size_t>(prefix.size(), 512);
  const std::string head = ToLowerAscii(prefix.substr(0, n));
  return head.find("<html") != std::string::npos ||
         head.find("<!doctype") != std::string::npos ||
         head.find("<script") != std::string::npos;
}

// true, если Content-Type / magic bytes намекают на медиапоток.
bool LooksLikeBinaryStream(const std::string &prefix,
                           const std::string &contentType) {
  const std::string ctLower = ToLowerAscii(contentType);
  if (ctLower.find("video/") != std::string::npos ||
      ctLower.find("audio/") != std::string::npos ||
      ctLower.find("application/octet-stream") != std::string::npos) {
    return true;
  }

  if (prefix.size() < 12)
    return false;

  // MP4: "ftyp" на offset 4
  if (prefix[4] == 'f' && prefix[5] == 't' && prefix[6] == 'y' &&
      prefix[7] == 'p') {
    return true;
  }
  // AVI/WAV: "RIFF" на offset 0
  if (prefix[0] == 'R' && prefix[1] == 'I' && prefix[2] == 'F' &&
      prefix[3] == 'F') {
    return true;
  }
  // Matroska/WebM: 0x1A 0x45 0xDF 0xA3
  if (static_cast<unsigned char>(prefix[0]) == 0x1A &&
      static_cast<unsigned char>(prefix[1]) == 0x45 &&
      static_cast<unsigned char>(prefix[2]) == 0xDF &&
      static_cast<unsigned char>(prefix[3]) == 0xA3) {
    return true;
  }
  // MPEG-TS: 0x47 sync на offset 0 и offset 188
  if (static_cast<unsigned char>(prefix[0]) == 0x47 && prefix.size() > 188 &&
      static_cast<unsigned char>(prefix[188]) == 0x47) {
    return true;
  }
  return false;
}

DetectionKind ClassifyPrefix(const std::string &prefix,
                             const std::string &contentType,
                             std::string *userInfoOut,
                             std::string *serverInfoOut) {
  // 1) M3U
  if (LooksLikeM3u(prefix))
    return DetectionKind::M3U;

  // 2) HTML
  if (LooksLikeHtml(prefix, contentType))
    return DetectionKind::Html;

  // 3) JSON
  if (!prefix.empty() && (prefix[0] == '{' || prefix[0] == '[')) {
    rapidjson::Document d;
    if (!d.Parse(prefix.c_str()).HasParseError()) {
      if (d.IsObject() && d.HasMember("user_info") &&
          d["user_info"].IsObject()) {
        const auto &ui = d["user_info"];
        bool auth = false;
        if (ui.HasMember("auth")) {
          const auto &a = ui["auth"];
          if (a.IsInt() && a.GetInt() != 0)
            auth = true;
          else if (a.IsBool() && a.GetBool())
            auth = true;
          else if (a.IsString()) {
            const std::string s = a.GetString();
            if (s == "1" || s == "true")
              auth = true;
          }
        }
        if (auth) {
          if (userInfoOut) {
            rapidjson::StringBuffer buf;
            rapidjson::Writer<rapidjson::StringBuffer> w(buf);
            ui.Accept(w);
            userInfoOut->assign(buf.GetString(), buf.GetSize());
          }
          if (serverInfoOut && d.HasMember("server_info") &&
              d["server_info"].IsObject()) {
            rapidjson::StringBuffer buf;
            rapidjson::Writer<rapidjson::StringBuffer> w(buf);
            d["server_info"].Accept(w);
            serverInfoOut->assign(buf.GetString(), buf.GetSize());
          }
          return DetectionKind::XtreamAuth;
        }
      }
      return DetectionKind::JsonOther;
    }
    // "{" в начале, но parse fail — всё равно JsonOther (сервер пытался отдать
    // JSON, не M3U; например обрезанный ответ или ошибка).
    return DetectionKind::JsonOther;
  }

  // 4) Binary stream
  if (LooksLikeBinaryStream(prefix, contentType))
    return DetectionKind::BinaryStream;

  return DetectionKind::Unknown;
}

} // namespace

void DetectContentAsync(const wxString &url, const wxString &userAgent,
                        DetectCallback cb) {
  const std::string urlUtf8(url.utf8_str().data());
  const std::string uaUtf8(userAgent.utf8_str().data());

  std::thread([urlUtf8, uaUtf8, cb]() {
    Detection det;
    det.finalUrl = wxString::FromUTF8(urlUtf8);

    CURL *curl = curl_easy_init();
    if (!curl) {
      det.kind = DetectionKind::NetworkError;
      det.error = "CURL init failed";
    } else {
      ApplyCurlCaBundle(curl);

      DetectBuffer buf;
      char rangeStr[32];
      std::snprintf(rangeStr, sizeof(rangeStr), "0-%zu", kMaxPrefixBytes - 1);

      curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
      curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
      curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(curl, CURLOPT_RANGE, rangeStr);
      if (!uaUtf8.empty())
        curl_easy_setopt(curl, CURLOPT_USERAGENT, uaUtf8.c_str());
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

      const CURLcode res = curl_easy_perform(curl);

      long httpCode = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
      det.httpCode = httpCode;

      char *ct = nullptr;
      curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
      if (ct)
        det.contentType = ct;

      char *effUrl = nullptr;
      curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effUrl);
      if (effUrl)
        det.finalUrl = wxString::FromUTF8(effUrl);

      curl_easy_cleanup(curl);

      // WriteCb вернул 0 после 16 KB → CURLE_WRITE_ERROR, но заголовки уже
      // получены (httpCode != 0). Это штатное завершение.
      const bool writeAborted = (res == CURLE_WRITE_ERROR && httpCode != 0);

      if (res != CURLE_OK && !writeAborted) {
        det.kind = DetectionKind::NetworkError;
        det.error = curl_easy_strerror(res);
      } else if (httpCode >= 400) {
        det.kind = DetectionKind::HttpError;
        det.error = "HTTP " + std::to_string(httpCode);
      } else if (buf.data.empty()) {
        det.kind = DetectionKind::Empty;
      } else {
        det.kind = ClassifyPrefix(buf.data, det.contentType, &det.userInfoJson,
                                  &det.serverInfoJson);
      }
    }

    if (wxTheApp) {
      wxTheApp->CallAfter([cb, det]() {
        if (cb)
          cb(det);
      });
    } else {
      if (cb)
        cb(det);
    }
  }).detach();
}

} // namespace xtream
