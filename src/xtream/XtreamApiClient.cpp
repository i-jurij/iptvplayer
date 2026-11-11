#include "xtream/XtreamApiClient.h"

#include "LogControl.h"
#include "Utils_Net.h"

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/memorystream.h>
#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <thread>

namespace xtream {

namespace {

constexpr long kMaxBodyBytes = 100L * 1024 * 1024;

const char *CategoryEndpoint(XtreamApiClient::CategoryType t) {
  switch (t) {
  case XtreamApiClient::CategoryType::Live:
    return "get_live_categories";
  case XtreamApiClient::CategoryType::Vod:
    return "get_vod_categories";
  case XtreamApiClient::CategoryType::Series:
    return "get_series_categories";
  }
  return "get_live_categories";
}

std::string U8(const wxString &s) { return std::string(s.utf8_str().data()); }

void SerializeValue(const rapidjson::Value &v, std::string &out) {
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  v.Accept(w);
  out.assign(buf.GetString(), buf.GetSize());
}

struct WriteBuf {
  std::string data;
  size_t maxBytes = kMaxBodyBytes;

  WriteBuf() { data.reserve(64 * 1024); }
};

size_t WriteCb(char *ptr, size_t size, size_t nmemb, void *userdata) {
  auto *buf = static_cast<WriteBuf *>(userdata);
  const size_t total = size * nmemb;
  const size_t remain = buf->maxBytes - buf->data.size();
  const size_t take = (total < remain) ? total : remain;
  buf->data.append(ptr, take);
  if (buf->data.size() >= buf->maxBytes)
    return 0;
  return total;
}

int CancelCb(void *clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto *flag = static_cast<std::atomic<bool> *>(clientp);
  return (flag && flag->load()) ? 1 : 0;
}

bool LooksLikeNonJson(const std::string &body, const std::string &contentType) {
  if (!contentType.empty()) {
    std::string ctLower = contentType;
    std::transform(ctLower.begin(), ctLower.end(), ctLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (ctLower.find("html") != std::string::npos ||
        ctLower.find("xml") != std::string::npos) {
      return true;
    }
  }

  const size_t n = std::min<size_t>(body.size(), 512);
  std::string head = body.substr(0, n);
  std::transform(head.begin(), head.end(), head.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (head.find("<html") != std::string::npos ||
      head.find("<!doctype") != std::string::npos ||
      head.find("<script") != std::string::npos) {
    return true;
  }

  return false;
}

struct AttemptResult {
  bool ok = false;
  bool retryable = false;
  long httpCode = 0;
  std::string body;
  std::string error;
  std::string contentType;
};

AttemptResult PerformHttpGet(const std::string &url, const std::string &ua,
                             int timeoutSec, std::atomic<bool> &cancelFlag) {
  AttemptResult r;

  CURL *curl = curl_easy_init();
  if (!curl) {
    r.error = "CURL init failed";
    return r;
  }

  ApplyCurlCaBundle(curl);

  WriteBuf buf;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSec));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, kMaxBodyBytes);
  if (!ua.empty())
    curl_easy_setopt(curl, CURLOPT_USERAGENT, ua.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CancelCb);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelFlag);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

  const CURLcode res = curl_easy_perform(curl);

  long httpCode = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
  r.httpCode = httpCode;

  char *ct = nullptr;
  curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
  if (ct)
    r.contentType = ct;

  r.body = std::move(buf.data);

  curl_easy_cleanup(curl);

  if (res == CURLE_ABORTED_BY_CALLBACK) {
    r.error = "Cancelled";
    return r;
  }

  if (res != CURLE_OK) {
    r.error = curl_easy_strerror(res);
    switch (res) {
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_COULDNT_CONNECT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_PARTIAL_FILE:
      r.retryable = true;
      break;
    default:
      break;
    }
    return r;
  }

  r.ok = true;
  return r;
}

int JitterMs() {
  static thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(0, 500);
  return dist(rng);
}

// SAX handler для get_vod_streams: массив объектов.
// rapidjson::Reader читает body из памяти; batch накапливается и
// flush'ится в БД через callback каждые kBatchSize записей.
class VodStreamSaxHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>,
                                          VodStreamSaxHandler> {
public:
  using FlushFn =
      std::function<bool(const std::vector<XtreamDatabase::VodStream> &)>;

  static constexpr size_t kBatchSize = 5000;

  VodStreamSaxHandler(FlushFn flush, const std::atomic<bool> &cancel,
                      XtreamApiClient::ProgressFn progress)
      : m_flush(std::move(flush)), m_cancel(cancel),
        m_progress(std::move(progress)) {
    m_batch.reserve(kBatchSize);
  }

  bool StartObject() {
    m_current = XtreamDatabase::VodStream{};
    m_field = Field::None;
    return true;
  }

  bool Key(const char *str, rapidjson::SizeType len, bool) {
    const std::string key(str, len);
    if (key == "stream_id")
      m_field = Field::StreamId;
    else if (key == "name")
      m_field = Field::Name;
    else if (key == "stream_icon")
      m_field = Field::StreamIcon;
    else if (key == "rating")
      m_field = Field::Rating;
    else if (key == "rating_5based")
      m_field = Field::Rating5based;
    else if (key == "added")
      m_field = Field::Added;
    else if (key == "container_extension")
      m_field = Field::ContainerExtension;
    else if (key == "custom_sid")
      m_field = Field::CustomSid;
    else
      m_field = Field::None;
    return true;
  }

  bool Uint64(uint64_t v) {
    if (m_field == Field::StreamId)
      m_current.streamId = static_cast<int64_t>(v);
    else if (m_field == Field::Added)
      m_current.added = static_cast<int64_t>(v);
    return true;
  }

  bool Int(int v) {
    if (m_field == Field::StreamId)
      m_current.streamId = v;
    else if (m_field == Field::Added)
      m_current.added = v;
    return true;
  }

  bool Uint(unsigned v) {
    if (m_field == Field::StreamId)
      m_current.streamId = v;
    else if (m_field == Field::Added)
      m_current.added = v;
    return true;
  }

  bool Double(double v) {
    if (m_field == Field::Rating)
      m_current.rating = v;
    else if (m_field == Field::Rating5based)
      m_current.rating5based = v;
    return true;
  }

  bool String(const char *str, rapidjson::SizeType len, bool) {
    const std::string val(str, len);
    switch (m_field) {
    case Field::Name:
      m_current.name = val;
      break;
    case Field::StreamIcon:
      m_current.posterUrl = val;
      break;
    case Field::ContainerExtension:
      m_current.containerExt = val;
      break;
    case Field::CustomSid:
      m_current.customSid = val;
      break;
    default:
      break;
    }
    return true;
  }

  bool EndObject(rapidjson::SizeType) {
    if (m_current.streamId <= 0)
      return true;

    m_batch.push_back(std::move(m_current));
    m_current = XtreamDatabase::VodStream{};

    if ((m_batch.size() % 1000) == 0 && m_cancel.load()) {
      m_cancelled = true;
      return false;
    }

    if (m_batch.size() >= kBatchSize && !FlushBatch())
      return false;

    return true;
  }

  bool EndArray(rapidjson::SizeType) {
    if (!FlushBatch())
      return false;
    return true;
  }

  bool HasError() const { return m_error; }
  bool WasCancelled() const { return m_cancelled; }
  size_t TotalInserted() const { return m_totalInserted; }

private:
  bool FlushBatch() {
    if (m_batch.empty())
      return true;

    if (m_cancel.load()) {
      m_cancelled = true;
      return false;
    }

    if (!m_flush(m_batch)) {
      m_error = true;
      return false;
    }

    m_totalInserted += m_batch.size();
    m_batch.clear();

    if (m_progress) {
      m_progress(-1, "VOD: " + std::to_string(m_totalInserted) + " items");
    }

    return true;
  }

  enum class Field {
    None,
    StreamId,
    Name,
    StreamIcon,
    Rating,
    Rating5based,
    Added,
    ContainerExtension,
    CustomSid
  };

  FlushFn m_flush;
  const std::atomic<bool> &m_cancel;
  XtreamApiClient::ProgressFn m_progress;

  std::vector<XtreamDatabase::VodStream> m_batch;
  XtreamDatabase::VodStream m_current;
  Field m_field = Field::None;

  bool m_error = false;
  bool m_cancelled = false;
  size_t m_totalInserted = 0;
};

class SeriesSaxHandler
    : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, SeriesSaxHandler> {
public:
  using FlushFn =
      std::function<bool(const std::vector<XtreamDatabase::Series> &)>;

  static constexpr size_t kBatchSize = 5000;

  SeriesSaxHandler(FlushFn flush, const std::atomic<bool> &cancel,
                   XtreamApiClient::ProgressFn progress)
      : m_flush(std::move(flush)), m_cancel(cancel),
        m_progress(std::move(progress)) {
    m_batch.reserve(kBatchSize);
  }

  bool StartObject() {
    m_current = XtreamDatabase::Series{};
    m_field = Field::None;
    return true;
  }

  bool Key(const char *str, rapidjson::SizeType len, bool) {
    const std::string key(str, len);
    if (key == "series_id")
      m_field = Field::SeriesId;
    else if (key == "name")
      m_field = Field::Name;
    else if (key == "cover")
      m_field = Field::Cover;
    else if (key == "plot")
      m_field = Field::Plot;
    else if (key == "cast")
      m_field = Field::Cast;
    else if (key == "director")
      m_field = Field::Director;
    else if (key == "genre")
      m_field = Field::Genre;
    else if (key == "releaseDate" || key == "release_date")
      m_field = Field::ReleaseDate;
    else if (key == "rating")
      m_field = Field::Rating;
    else if (key == "rating_5based")
      m_field = Field::Rating5based;
    else if (key == "backdrop_path")
      m_field = Field::BackdropPath;
    else if (key == "youtube_trailer")
      m_field = Field::YoutubeTrailer;
    else if (key == "episode_run_time")
      m_field = Field::EpisodeRunTime;
    else if (key == "last_modified")
      m_field = Field::LastModified;
    else
      m_field = Field::None;
    return true;
  }

  bool Uint64(uint64_t v) {
    if (m_field == Field::SeriesId)
      m_current.seriesId = static_cast<int64_t>(v);
    else if (m_field == Field::LastModified)
      m_current.lastModified = static_cast<int64_t>(v);
    return true;
  }

  bool Int(int v) {
    if (m_field == Field::SeriesId)
      m_current.seriesId = v;
    else if (m_field == Field::LastModified)
      m_current.lastModified = v;
    return true;
  }

  bool Uint(unsigned v) {
    if (m_field == Field::SeriesId)
      m_current.seriesId = v;
    else if (m_field == Field::LastModified)
      m_current.lastModified = v;
    return true;
  }

  bool Double(double v) {
    if (m_field == Field::Rating)
      m_current.rating = v;
    else if (m_field == Field::Rating5based)
      m_current.rating5based = v;
    return true;
  }

  bool String(const char *str, rapidjson::SizeType len, bool) {
    const std::string val(str, len);
    switch (m_field) {
    case Field::Name:
      m_current.name = val;
      break;
    case Field::Cover:
      m_current.coverUrl = val;
      break;
    case Field::Plot:
      m_current.plot = val;
      break;
    case Field::Cast:
      m_current.castList = val;
      break;
    case Field::Director:
      m_current.director = val;
      break;
    case Field::Genre:
      m_current.genre = val;
      break;
    case Field::ReleaseDate:
      m_current.releaseDate = val;
      break;
    case Field::BackdropPath:
      m_current.backdropPath = val;
      break;
    case Field::YoutubeTrailer:
      m_current.youtubeTrailer = val;
      break;
    case Field::EpisodeRunTime:
      m_current.episodeRunTime = val;
      break;
    default:
      break;
    }
    return true;
  }

  bool EndObject(rapidjson::SizeType) {
    if (m_current.seriesId <= 0)
      return true;

    m_batch.push_back(std::move(m_current));
    m_current = XtreamDatabase::Series{};

    if ((m_batch.size() % 1000) == 0 && m_cancel.load()) {
      m_cancelled = true;
      return false;
    }

    if (m_batch.size() >= kBatchSize && !FlushBatch())
      return false;

    return true;
  }

  bool EndArray(rapidjson::SizeType) {
    if (!FlushBatch())
      return false;
    return true;
  }

  bool HasError() const { return m_error; }
  bool WasCancelled() const { return m_cancelled; }
  size_t TotalInserted() const { return m_totalInserted; }

private:
  bool FlushBatch() {
    if (m_batch.empty())
      return true;

    if (m_cancel.load()) {
      m_cancelled = true;
      return false;
    }

    if (!m_flush(m_batch)) {
      m_error = true;
      return false;
    }

    m_totalInserted += m_batch.size();
    m_batch.clear();

    if (m_progress) {
      m_progress(-1, "Series: " + std::to_string(m_totalInserted) + " items");
    }

    return true;
  }

  enum class Field {
    None,
    SeriesId,
    Name,
    Cover,
    Plot,
    Cast,
    Director,
    Genre,
    ReleaseDate,
    Rating,
    Rating5based,
    BackdropPath,
    YoutubeTrailer,
    EpisodeRunTime,
    LastModified
  };

  FlushFn m_flush;
  const std::atomic<bool> &m_cancel;
  XtreamApiClient::ProgressFn m_progress;

  std::vector<XtreamDatabase::Series> m_batch;
  XtreamDatabase::Series m_current;
  Field m_field = Field::None;

  bool m_error = false;
  bool m_cancelled = false;
  size_t m_totalInserted = 0;
};

int64_t ParseFlexibleInt64(const rapidjson::Value &v) {
  if (v.IsNull())
    return 0;
  if (v.IsInt64())
    return v.GetInt64();
  if (v.IsInt())
    return v.GetInt();
  if (v.IsUint())
    return static_cast<int64_t>(v.GetUint());
  if (v.IsUint64())
    return static_cast<int64_t>(v.GetUint64());
  if (v.IsDouble())
    return static_cast<int64_t>(v.GetDouble());
  if (v.IsString()) {
    try {
      return std::stoll(v.GetString());
    } catch (...) {
      return 0;
    }
  }
  return 0;
}

int ParseFlexibleInt(const rapidjson::Value &v) {
  return static_cast<int>(ParseFlexibleInt64(v));
}

std::string ParseFlexibleString(const rapidjson::Value &v) {
  if (v.IsString())
    return v.GetString();
  if (v.IsInt64())
    return std::to_string(v.GetInt64());
  if (v.IsInt())
    return std::to_string(v.GetInt());
  if (v.IsUint64())
    return std::to_string(v.GetUint64());
  if (v.IsUint())
    return std::to_string(v.GetUint());
  return std::string();
}

// Парсит один episode-объект. seasonFallback используется, если поле
// "season" отсутствует (dict-формат, где номер сезона — ключ).
bool ParseEpisodeObject(const rapidjson::Value &v, int seasonFallback,
                        XtreamDatabase::Episode &out) {
  if (!v.IsObject())
    return false;

  if (v.HasMember("id"))
    out.id = ParseFlexibleString(v["id"]);
  if (out.id.empty())
    return false;

  if (v.HasMember("season"))
    out.seasonNumber = ParseFlexibleInt(v["season"]);
  else
    out.seasonNumber = seasonFallback;

  if (v.HasMember("episode_num"))
    out.episodeNum = ParseFlexibleInt(v["episode_num"]);
  if (v.HasMember("title"))
    out.title = ParseFlexibleString(v["title"]);
  if (v.HasMember("container_extension"))
    out.containerExt = ParseFlexibleString(v["container_extension"]);
  if (v.HasMember("custom_sid"))
    out.customSid = ParseFlexibleString(v["custom_sid"]);
  if (v.HasMember("added"))
    out.added = ParseFlexibleInt64(v["added"]);

  if (v.HasMember("info") && v["info"].IsObject()) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    v["info"].Accept(writer);
    out.infoJson.assign(buf.GetString(), buf.GetSize());
  }

  return true;
}

// MD §3.2: episodes может быть dict ({"1": [...]}) или list ([{...}]).
void NormalizeEpisodes(const rapidjson::Value &episodes,
                       std::vector<XtreamDatabase::Episode> &out) {
  if (episodes.IsObject()) {
    for (auto it = episodes.MemberBegin(); it != episodes.MemberEnd(); ++it) {
      int seasonNum = 0;
      if (it->name.IsString()) {
        try {
          seasonNum = std::stoi(it->name.GetString());
        } catch (...) {
          seasonNum = 0;
        }
      }

      if (!it->value.IsArray())
        continue;
      for (const auto &ep : it->value.GetArray()) {
        XtreamDatabase::Episode e;
        if (ParseEpisodeObject(ep, seasonNum, e))
          out.push_back(std::move(e));
      }
    }
  } else if (episodes.IsArray()) {
    for (const auto &ep : episodes.GetArray()) {
      XtreamDatabase::Episode e;
      if (ParseEpisodeObject(ep, 0, e))
        out.push_back(std::move(e));
    }
  }
}

void NormalizeSeasons(const rapidjson::Value &seasons, int64_t seriesId,
                      std::vector<XtreamDatabase::Season> &out) {
  if (!seasons.IsArray())
    return;

  for (const auto &s : seasons.GetArray()) {
    if (!s.IsObject())
      continue;

    XtreamDatabase::Season season;
    season.seriesId = seriesId;

    if (s.HasMember("season_number"))
      season.seasonNumber = ParseFlexibleInt(s["season_number"]);
    if (s.HasMember("name"))
      season.name = ParseFlexibleString(s["name"]);
    if (s.HasMember("cover"))
      season.coverUrl = ParseFlexibleString(s["cover"]);
    if (s.HasMember("plot"))
      season.plot = ParseFlexibleString(s["plot"]);
    if (s.HasMember("episode_count"))
      season.episodeCount = ParseFlexibleInt(s["episode_count"]);
    if (s.HasMember("air_date"))
      season.airDate = ParseFlexibleString(s["air_date"]);

    out.push_back(std::move(season));
  }
}

// Fallback: если seasons пуст, синтезировать из episodes.
// seasonNumber == 0 → "Specials" (MD §3.37).
void SynthesizeSeasonsFromEpisodes(
    const std::vector<XtreamDatabase::Episode> &episodes, int64_t seriesId,
    std::vector<XtreamDatabase::Season> &out) {
  if (!out.empty())
    return;

  std::map<int, int> counts;
  for (const auto &e : episodes)
    counts[e.seasonNumber]++;

  for (const auto &kv : counts) {
    XtreamDatabase::Season s;
    s.seriesId = seriesId;
    s.seasonNumber = kv.first;
    s.name =
        (kv.first == 0) ? "Specials" : ("Season " + std::to_string(kv.first));
    s.episodeCount = kv.second;
    out.push_back(std::move(s));
  }
}

std::string NormalizeContainerExt(const std::string &ext,
                                  const char *fallback) {
  if (ext.empty())
    return std::string(fallback);

  std::string lower = ext;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // Базовая валидация: только [a-z0-9].
  for (char c : lower) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
      return std::string(fallback);
    }
  }
  return lower;
}

// startUtc → "YYYY-MM-DD:HH-MM" (UTC). Пустая строка при ошибке.
std::string FormatCatchupStart(int64_t startUtc) {
  if (startUtc <= 0)
    return std::string();

  std::time_t t = static_cast<std::time_t>(startUtc);
  struct tm tm_utc{};
#if defined(_WIN32)
  if (gmtime_s(&tm_utc, &t) != 0)
    return std::string();
#else
  if (gmtime_r(&t, &tm_utc) == nullptr)
    return std::string();
#endif

  char buf[24];
  const int n = std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d:%02d-%02d",
                              tm_utc.tm_year + 1900, tm_utc.tm_mon + 1,
                              tm_utc.tm_mday, tm_utc.tm_hour, tm_utc.tm_min);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(buf))
    return std::string();
  return std::string(buf, static_cast<size_t>(n));
}
} // namespace

XtreamApiClient::XtreamApiClient() = default;
XtreamApiClient::~XtreamApiClient() = default;

void XtreamApiClient::Cancel() { m_cancel.store(true); }

XtreamApiClient::Endpoint
XtreamApiClient::ResolveEndpoint(const Playlist &pl) const {
  Endpoint ep;

  const auto &meta = pl.getXtreamMeta();
  if (meta.hasEndpoint()) {
    ep.base = meta.baseUrl;
    ep.user = meta.username;
    ep.pass = meta.password;
    ep.ok = true;
    return ep;
  }

  const std::string &src = pl.getSource();
  if (src.empty())
    return ep;

  const wxString wxSrc = wxString::FromUTF8(src);
  const wxString base = GetOriginWithParentPath(wxSrc);
  if (base.IsEmpty())
    return ep;

  const UrlCredentials creds = ExtractQueryCredentials(wxSrc);
  if (!creds.valid || creds.user.IsEmpty() || creds.pass.IsEmpty())
    return ep;

  ep.base = U8(base);
  ep.user = U8(creds.user);
  ep.pass = U8(creds.pass);
  ep.ok = true;
  return ep;
}

void XtreamApiClient::SyncMetaToDb(const Playlist &pl,
                                   XtreamDatabase &db) const {
  const auto &meta = pl.getXtreamMeta();
  if (!meta.hasUserInfo())
    return;

  db.SetMeta("user_info_json", meta.userInfoJson);
  if (!meta.serverInfoJson.empty())
    db.SetMeta("server_info_json", meta.serverInfoJson);
  if (!meta.baseUrl.empty())
    db.SetMeta("base_url", meta.baseUrl);
  if (!meta.username.empty())
    db.SetMeta("username", meta.username);
}

XtreamApiClient::HttpResponse XtreamApiClient::HttpGet(const Playlist &pl,
                                                       const std::string &url,
                                                       int timeoutSec) {
  HttpResponse result;

  constexpr int kMaxAttempts = 3;
  constexpr int kBackoffMs[kMaxAttempts - 1] = {2000, 4000};
  const std::string ua = pl.getUserAgent();

  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    if (m_cancel.load()) {
      result.error = "Cancelled";
      return result;
    }

    if (attempt > 0) {
      const int waitMs = kBackoffMs[attempt - 1] + JitterMs();
      LOG_INFO("XtreamApiClient: retry %d/%d in %d ms for %s", attempt,
               kMaxAttempts - 1, waitMs, SanitizeUrlForLog(url).c_str());
      std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
      if (m_cancel.load()) {
        result.error = "Cancelled";
        return result;
      }
    }

    AttemptResult ar = PerformHttpGet(url, ua, timeoutSec, m_cancel);

    // HTML / не-JSON на 2xx → без retry
    if (ar.ok && ar.httpCode >= 200 && ar.httpCode < 300 &&
        LooksLikeNonJson(ar.body, ar.contentType)) {
      result.ok = false;
      result.httpCode = ar.httpCode;
      result.error = "Server returned non-JSON response";
      LOG_WARN("XtreamApiClient: non-JSON from %s (Content-Type: %s)",
               SanitizeUrlForLog(url).c_str(), ar.contentType.c_str());
      return result;
    }

    bool shouldRetry = false;
    if (!ar.ok && ar.retryable) {
      shouldRetry = true;
    } else if (ar.ok && ar.httpCode >= 500) {
      shouldRetry = true;
    } else if (ar.ok && ar.httpCode == 429) {
      shouldRetry = true;
    }

    if (!shouldRetry || attempt == kMaxAttempts - 1) {
      result.ok = ar.ok && ar.httpCode >= 200 && ar.httpCode < 300;
      result.httpCode = ar.httpCode;
      result.body = std::move(ar.body);
      result.error = ar.error;
      return result;
    }
  }

  return result;
}

XtreamApiClient::AuthResult XtreamApiClient::Auth(const Playlist &pl) {
  m_cancel.store(false);

  AuthResult r;
  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    r.error = "Cannot resolve endpoint: missing credentials";
    LOG_WARN("XtreamApiClient::Auth: no endpoint for playlist '%s'",
             pl.getTitle().c_str());
    return r;
  }

  const wxString apiUrl =
      wxString::FromUTF8(ep.base) +
      "/player_api.php?username=" + UrlEncode(wxString::FromUTF8(ep.user)) +
      "&password=" + UrlEncode(wxString::FromUTF8(ep.pass));

  HttpResponse resp = HttpGet(pl, U8(apiUrl), 15);
  if (!resp.ok) {
    r.error = resp.error.empty() ? "Network error" : resp.error;
    LOG_WARN("XtreamApiClient::Auth: request failed for '%s': %s",
             pl.getTitle().c_str(), r.error.c_str());
    return r;
  }

  rapidjson::Document d;
  if (d.Parse(resp.body.c_str()).HasParseError() || !d.IsObject()) {
    r.error = "player_api.php did not return valid JSON";
    return r;
  }

  if (!d.HasMember("user_info") || !d["user_info"].IsObject()) {
    r.error = "player_api.php response missing user_info";
    return r;
  }

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

  if (!auth) {
    std::string msg;
    if (ui.HasMember("message") && ui["message"].IsString())
      msg = ui["message"].GetString();
    if (msg.empty() && ui.HasMember("status") && ui["status"].IsString())
      msg = ui["status"].GetString();
    r.error = msg.empty() ? "Invalid username or password" : msg;
    LOG_WARN("XtreamApiClient::Auth: rejected for '%s': %s",
             pl.getTitle().c_str(), r.error.c_str());
    return r;
  }

  SerializeValue(ui, r.userInfoJson);
  if (d.HasMember("server_info") && d["server_info"].IsObject())
    SerializeValue(d["server_info"], r.serverInfoJson);

  r.baseUrl = ep.base;
  r.username = ep.user;
  r.password = ep.pass;
  r.ok = true;
  return r;
}

std::vector<XtreamApiClient::Category>
XtreamApiClient::GetCategories(const Playlist &pl, CategoryType type) {
  m_cancel.store(false);

  std::vector<Category> out;
  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::GetCategories: no endpoint for '%s'",
             pl.getTitle().c_str());
    return out;
  }

  const wxString apiUrl =
      wxString::FromUTF8(ep.base) +
      "/player_api.php?username=" + UrlEncode(wxString::FromUTF8(ep.user)) +
      "&password=" + UrlEncode(wxString::FromUTF8(ep.pass)) +
      "&action=" + CategoryEndpoint(type);

  HttpResponse resp = HttpGet(pl, U8(apiUrl), 30);
  if (!resp.ok)
    return out;

  rapidjson::Document d;
  if (d.Parse(resp.body.c_str()).HasParseError() || !d.IsArray())
    return out;

  for (const auto &item : d.GetArray()) {
    if (!item.IsObject())
      continue;
    Category c;
    if (item.HasMember("category_id"))
      c.id = static_cast<int>(item["category_id"].GetInt());
    if (item.HasMember("category_name") && item["category_name"].IsString())
      c.name = item["category_name"].GetString();
    if (c.id > 0)
      out.push_back(std::move(c));
  }

  return out;
}

bool XtreamApiClient::RefreshVodCategory(const Playlist &pl, int categoryId,
                                         XtreamDatabase &db,
                                         XtreamApiClient::ProgressFn progress,
                                         const std::atomic<bool> &cancelFlag) {
  m_cancel.store(false);

  if (categoryId <= 0) {
    LOG_WARN("XtreamApiClient::RefreshVodCategory: invalid categoryId");
    return false;
  }

  SyncMetaToDb(pl, db);

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::RefreshVodCategory: no endpoint for '%s'",
             pl.getTitle().c_str());
    return false;
  }

  const wxString apiUrl =
      wxString::FromUTF8(ep.base) +
      "/player_api.php?username=" + UrlEncode(wxString::FromUTF8(ep.user)) +
      "&password=" + UrlEncode(wxString::FromUTF8(ep.pass)) +
      "&action=get_vod_streams&category_id=" +
      wxString::Format("%d", categoryId);

  if (progress)
    progress(-1, "VOD: downloading");

  HttpResponse resp = HttpGet(pl, U8(apiUrl), 60);
  if (!resp.ok) {
    LOG_WARN("XtreamApiClient::RefreshVodCategory: HTTP failed for "
             "category %d: %s",
             categoryId, resp.error.c_str());
    return false;
  }

  if (cancelFlag.load()) {
    LOG_INFO("XtreamApiClient::RefreshVodCategory: cancelled before parse");
    return false;
  }

  if (!db.BeginTransaction()) {
    LOG_ERROR("XtreamApiClient::RefreshVodCategory: cannot start transaction");
    return false;
  }

  if (!db.ClearVodCategoryMap(categoryId)) {
    LOG_ERROR("XtreamApiClient::RefreshVodCategory: ClearVodCategoryMap "
              "failed");
    db.Rollback();
    return false;
  }

  VodStreamSaxHandler handler(
      [&db, categoryId, &cancelFlag](
          const std::vector<XtreamDatabase::VodStream> &batch) -> bool {
        return db.UpsertVodBatch(categoryId, batch, cancelFlag);
      },
      cancelFlag, progress);

  rapidjson::Reader reader;
  rapidjson::MemoryStream stream(resp.body.data(), resp.body.size());
  rapidjson::ParseResult parseResult = reader.Parse(stream, handler);

  if (!parseResult) {
    LOG_ERROR("XtreamApiClient::RefreshVodCategory: JSON parse error at "
              "offset %zu (category %d): %s",
              parseResult.Offset(), categoryId,
              rapidjson::GetParseError_En(parseResult.Code()));
    db.Rollback();
    return false;
  }

  if (handler.WasCancelled()) {
    LOG_INFO("XtreamApiClient::RefreshVodCategory: cancelled (category %d, "
             "inserted %zu)",
             categoryId, handler.TotalInserted());
    db.Rollback();
    return false;
  }

  if (handler.HasError()) {
    LOG_ERROR("XtreamApiClient::RefreshVodCategory: DB error (category %d)",
              categoryId);
    db.Rollback();
    return false;
  }

  // meta в той же транзакции (MD §3.19)
  db.SetMeta("last_update_vod_" + std::to_string(categoryId),
             std::to_string(std::time(nullptr)));

  if (!db.Commit()) {
    LOG_ERROR("XtreamApiClient::RefreshVodCategory: COMMIT failed (category "
              "%d)",
              categoryId);
    db.Rollback();
    return false;
  }

  db.SetMeta("has_vod", "1");

  if (progress)
    progress(100, "VOD: " + std::to_string(handler.TotalInserted()) +
                      " items (done)");

  LOG_INFO("XtreamApiClient::RefreshVodCategory: category %d, %zu items",
           categoryId, handler.TotalInserted());
  return true;
}

bool XtreamApiClient::RefreshSeriesCategory(
    const Playlist &pl, int categoryId, XtreamDatabase &db,
    XtreamApiClient::ProgressFn progress, const std::atomic<bool> &cancelFlag) {
  m_cancel.store(false);

  if (categoryId <= 0) {
    LOG_WARN("XtreamApiClient::RefreshSeriesCategory: invalid categoryId");
    return false;
  }

  SyncMetaToDb(pl, db);

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::RefreshSeriesCategory: no endpoint for '%s'",
             pl.getTitle().c_str());
    return false;
  }

  const wxString apiUrl =
      wxString::FromUTF8(ep.base) +
      "/player_api.php?username=" + UrlEncode(wxString::FromUTF8(ep.user)) +
      "&password=" + UrlEncode(wxString::FromUTF8(ep.pass)) +
      "&action=get_series&category_id=" + wxString::Format("%d", categoryId);

  if (progress)
    progress(-1, "Series: downloading");

  HttpResponse resp = HttpGet(pl, U8(apiUrl), 60);
  if (!resp.ok) {
    LOG_WARN("XtreamApiClient::RefreshSeriesCategory: HTTP failed for "
             "category %d: %s",
             categoryId, resp.error.c_str());
    return false;
  }

  if (cancelFlag.load()) {
    LOG_INFO("XtreamApiClient::RefreshSeriesCategory: cancelled before parse");
    return false;
  }

  if (!db.BeginTransaction()) {
    LOG_ERROR(
        "XtreamApiClient::RefreshSeriesCategory: cannot start transaction");
    return false;
  }

  if (!db.ClearSeriesCategoryMap(categoryId)) {
    LOG_ERROR("XtreamApiClient::RefreshSeriesCategory: "
              "ClearSeriesCategoryMap failed");
    db.Rollback();
    return false;
  }

  SeriesSaxHandler handler(
      [&db, categoryId,
       &cancelFlag](const std::vector<XtreamDatabase::Series> &batch) -> bool {
        return db.UpsertSeriesBatch(categoryId, batch, cancelFlag);
      },
      cancelFlag, progress);

  rapidjson::Reader reader;
  rapidjson::MemoryStream stream(resp.body.data(), resp.body.size());
  rapidjson::ParseResult parseResult = reader.Parse(stream, handler);

  if (!parseResult) {
    LOG_ERROR("XtreamApiClient::RefreshSeriesCategory: JSON parse error at "
              "offset %zu (category %d): %s",
              parseResult.Offset(), categoryId,
              rapidjson::GetParseError_En(parseResult.Code()));
    db.Rollback();
    return false;
  }

  if (handler.WasCancelled()) {
    LOG_INFO("XtreamApiClient::RefreshSeriesCategory: cancelled (category "
             "%d, inserted %zu)",
             categoryId, handler.TotalInserted());
    db.Rollback();
    return false;
  }

  if (handler.HasError()) {
    LOG_ERROR("XtreamApiClient::RefreshSeriesCategory: DB error (category %d)",
              categoryId);
    db.Rollback();
    return false;
  }

  db.SetMeta("last_update_series_" + std::to_string(categoryId),
             std::to_string(std::time(nullptr)));

  if (!db.Commit()) {
    LOG_ERROR("XtreamApiClient::RefreshSeriesCategory: COMMIT failed "
              "(category %d)",
              categoryId);
    db.Rollback();
    return false;
  }

  db.SetMeta("has_series", "1");

  if (progress)
    progress(100, "Series: " + std::to_string(handler.TotalInserted()) +
                      " items (done)");

  LOG_INFO("XtreamApiClient::RefreshSeriesCategory: category %d, %zu items",
           categoryId, handler.TotalInserted());
  return true;
}

bool XtreamApiClient::FetchVodInfo(const Playlist &pl, int64_t streamId,
                                   XtreamDatabase &db, std::string &outJson,
                                   const std::atomic<bool> &cancelFlag) {
  m_cancel.store(false);
  outJson.clear();

  if (streamId <= 0)
    return false;

  SyncMetaToDb(pl, db);

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::FetchVodInfo: no endpoint for '%s'",
             pl.getTitle().c_str());
    return false;
  }

  const wxString apiUrl =
      wxString::FromUTF8(ep.base) +
      "/player_api.php?username=" + UrlEncode(wxString::FromUTF8(ep.user)) +
      "&password=" + UrlEncode(wxString::FromUTF8(ep.pass)) +
      "&action=get_vod_info&vod_id=" +
      wxString::Format("%lld", static_cast<long long>(streamId));

  HttpResponse resp = HttpGet(pl, U8(apiUrl), 30);
  if (!resp.ok) {
    LOG_WARN("XtreamApiClient::FetchVodInfo: HTTP failed for %lld: %s",
             static_cast<long long>(streamId), resp.error.c_str());
    return false;
  }

  if (cancelFlag.load()) {
    LOG_INFO("XtreamApiClient::FetchVodInfo: cancelled");
    return false;
  }

  rapidjson::Document d;
  if (d.Parse(resp.body.c_str()).HasParseError() || !d.IsObject()) {
    LOG_WARN("XtreamApiClient::FetchVodInfo: invalid JSON for %lld",
             static_cast<long long>(streamId));
    return false;
  }

  const rapidjson::Value *target = &d;
  if (d.HasMember("info") && d["info"].IsObject())
    target = &d["info"];

  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
  target->Accept(writer);
  outJson.assign(buf.GetString(), buf.GetSize());

  db.InsertVodInfo(streamId, outJson, static_cast<int64_t>(std::time(nullptr)));

  LOG_INFO("XtreamApiClient::FetchVodInfo: %lld (%zu bytes)",
           static_cast<long long>(streamId), outJson.size());
  return true;
}

bool XtreamApiClient::FetchSeriesInfo(const Playlist &pl, int64_t seriesId,
                                      XtreamDatabase &db,
                                      XtreamApiClient::ProgressFn progress,
                                      const std::atomic<bool> &cancelFlag) {
  m_cancel.store(false);

  if (seriesId <= 0)
    return false;

  SyncMetaToDb(pl, db);

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::FetchSeriesInfo: no endpoint for '%s'",
             pl.getTitle().c_str());
    return false;
  }

  const wxString apiUrl =
      wxString::FromUTF8(ep.base) +
      "/player_api.php?username=" + UrlEncode(wxString::FromUTF8(ep.user)) +
      "&password=" + UrlEncode(wxString::FromUTF8(ep.pass)) +
      "&action=get_series_info&series_id=" +
      wxString::Format("%lld", static_cast<long long>(seriesId));

  if (progress)
    progress(-1, "Series info: downloading");

  HttpResponse resp = HttpGet(pl, U8(apiUrl), 60);
  if (!resp.ok) {
    LOG_WARN("XtreamApiClient::FetchSeriesInfo: HTTP failed for %lld: %s",
             static_cast<long long>(seriesId), resp.error.c_str());
    return false;
  }

  if (cancelFlag.load()) {
    LOG_INFO("XtreamApiClient::FetchSeriesInfo: cancelled before parse");
    return false;
  }

  rapidjson::Document d;
  if (d.Parse(resp.body.c_str()).HasParseError() || !d.IsObject()) {
    LOG_WARN("XtreamApiClient::FetchSeriesInfo: invalid JSON for %lld",
             static_cast<long long>(seriesId));
    return false;
  }

  std::vector<XtreamDatabase::Season> seasons;
  std::vector<XtreamDatabase::Episode> episodes;

  if (d.HasMember("seasons"))
    NormalizeSeasons(d["seasons"], seriesId, seasons);

  if (d.HasMember("episodes"))
    NormalizeEpisodes(d["episodes"], episodes);

  SynthesizeSeasonsFromEpisodes(episodes, seriesId, seasons);

  if (cancelFlag.load()) {
    LOG_INFO("XtreamApiClient::FetchSeriesInfo: cancelled after parse");
    return false;
  }

  if (!db.BeginTransaction()) {
    LOG_ERROR("XtreamApiClient::FetchSeriesInfo: cannot start transaction");
    return false;
  }

  if (!db.InsertSeriesInfo(seriesId, seasons, episodes, cancelFlag)) {
    LOG_ERROR("XtreamApiClient::FetchSeriesInfo: InsertSeriesInfo failed for "
              "%lld",
              static_cast<long long>(seriesId));
    db.Rollback();
    return false;
  }

  if (!db.Commit()) {
    LOG_ERROR("XtreamApiClient::FetchSeriesInfo: COMMIT failed for %lld",
              static_cast<long long>(seriesId));
    db.Rollback();
    return false;
  }

  if (progress)
    progress(100,
             "Series info: " + std::to_string(episodes.size()) + " episodes");

  LOG_INFO("XtreamApiClient::FetchSeriesInfo: %lld (%zu seasons, %zu episodes)",
           static_cast<long long>(seriesId), seasons.size(), episodes.size());
  return true;
}

std::string
XtreamApiClient::BuildVodUrl(const Playlist &pl, int64_t streamId,
                             const std::string &containerExt) const {
  if (streamId <= 0) {
    LOG_WARN("XtreamApiClient::BuildVodUrl: invalid streamId");
    return std::string();
  }

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::BuildVodUrl: no endpoint for '%s'",
             pl.getTitle().c_str());
    return std::string();
  }

  const std::string ext = NormalizeContainerExt(containerExt, "mp4");

  wxString url = wxString::FromUTF8(ep.base) + "/movie/" +
                 UrlEncode(wxString::FromUTF8(ep.user)) + "/" +
                 UrlEncode(wxString::FromUTF8(ep.pass)) + "/" +
                 wxString::Format("%lld", static_cast<long long>(streamId)) +
                 "." + wxString::FromUTF8(ext);

  return U8(url);
}

std::string
XtreamApiClient::BuildSeriesEpisodeUrl(const Playlist &pl,
                                       const std::string &episodeId,
                                       const std::string &containerExt) const {
  if (episodeId.empty()) {
    LOG_WARN("XtreamApiClient::BuildSeriesEpisodeUrl: empty episodeId");
    return std::string();
  }

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::BuildSeriesEpisodeUrl: no endpoint for '%s'",
             pl.getTitle().c_str());
    return std::string();
  }

  const std::string ext = NormalizeContainerExt(containerExt, "ts");

  wxString url = wxString::FromUTF8(ep.base) + "/series/" +
                 UrlEncode(wxString::FromUTF8(ep.user)) + "/" +
                 UrlEncode(wxString::FromUTF8(ep.pass)) + "/" +
                 UrlEncode(wxString::FromUTF8(episodeId)) + "." +
                 wxString::FromUTF8(ext);

  return U8(url);
}

std::string XtreamApiClient::BuildCatchupUrl(const Playlist &pl,
                                             int64_t streamId, int64_t startUtc,
                                             int durationMin,
                                             bool useQueryStyle) const {
  if (streamId <= 0 || startUtc <= 0 || durationMin <= 0) {
    LOG_WARN("XtreamApiClient::BuildCatchupUrl: invalid args "
             "(streamId=%lld, startUtc=%lld, durationMin=%d)",
             static_cast<long long>(streamId), static_cast<long long>(startUtc),
             durationMin);
    return std::string();
  }

  const Endpoint ep = ResolveEndpoint(pl);
  if (!ep.ok) {
    LOG_WARN("XtreamApiClient::BuildCatchupUrl: no endpoint for '%s'",
             pl.getTitle().c_str());
    return std::string();
  }

  const std::string startStr = FormatCatchupStart(startUtc);
  if (startStr.empty()) {
    LOG_WARN("XtreamApiClient::BuildCatchupUrl: cannot format start time");
    return std::string();
  }

  const wxString wxUser = UrlEncode(wxString::FromUTF8(ep.user));
  const wxString wxPass = UrlEncode(wxString::FromUTF8(ep.pass));
  const wxString wxStart = UrlEncode(wxString::FromUTF8(startStr));

  wxString url;
  if (useQueryStyle) {
    url = wxString::FromUTF8(ep.base) +
          "/streaming/timeshift.php?username=" + wxUser +
          "&password=" + wxPass + "&stream=" +
          wxString::Format("%lld", static_cast<long long>(streamId)) +
          "&start=" + wxStart +
          "&duration=" + wxString::Format("%d", durationMin);
  } else {
    url = wxString::FromUTF8(ep.base) + "/timeshift/" + wxUser + "/" + wxPass +
          "/" + wxString::Format("%d", durationMin) + "/" + wxStart + "/" +
          wxString::Format("%lld", static_cast<long long>(streamId)) + ".ts";
  }

  return U8(url);
}
} // namespace xtream
